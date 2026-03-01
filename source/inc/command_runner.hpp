#pragma once
#include "types.hpp"
#include <glibmm-2.68/glibmm.h>
#include <giomm.h>
#include <sigc++/sigc++.h>
#include <string>
#include <functional>
#include <csignal>
#include <sys/wait.h>

namespace Progressions {

// ── Runs a shell command asynchronously, emitting line-by-line output ─────────
class CommandRunner {
public:
    // ── Signals ───────────────────────────────────────────────────────────────
    sigc::signal<void(std::string)>  signal_output_line;  // each output line
    sigc::signal<void(int)>          signal_finished;      // exit code
    sigc::signal<void(std::string)>  signal_error;         // spawn error

    CommandRunner() = default;
    ~CommandRunner() { kill(); }

    // ── Non-copyable, movable ─────────────────────────────────────────────────
    CommandRunner(const CommandRunner&)            = delete;
    CommandRunner& operator=(const CommandRunner&) = delete;

    // ── Start running a shell command (via bash -c) ───────────────────────────
    // Returns false if already running.
    bool run(const std::string& command) {
        if (running_) return false;
        running_  = true;
        exit_code_ = -1;

        const std::vector<std::string> argv{"/bin/bash", "-c", command + " 2>&1"};
        int stdout_fd = -1;

        try {
            Glib::spawn_async_with_pipes(
                ".",
                argv,
                /*envp=*/{},
                Glib::SpawnFlags::SEARCH_PATH | Glib::SpawnFlags::DO_NOT_REAP_CHILD,
                /*child_setup=*/{},
                &pid_,
                /*stdin=*/nullptr,
                &stdout_fd,
                /*stderr=*/nullptr
            );
        } catch (const Glib::SpawnError& e) {
            running_ = false;
            signal_error.emit(e.what());
            return false;
        }

        // ── Watch child exit ──────────────────────────────────────────────────
        child_watch_conn_ = Glib::signal_child_watch().connect(
            [this](Glib::Pid, int status) { on_child_exited(status); }, pid_);

        // ── Read stdout via IOChannel / signal_io ─────────────────────────────
        io_channel_ = Glib::IOChannel::create_from_fd(stdout_fd);
        io_channel_->set_flags(Glib::IOFlags::NONBLOCK);
        io_channel_->set_encoding("");   // binary/raw
        io_channel_->set_buffered(true);

        io_conn_ = Glib::signal_io().connect(
            sigc::mem_fun(*this, &CommandRunner::on_io_ready),
            io_channel_,
            Glib::IOCondition::IO_IN | Glib::IOCondition::IO_HUP |
            Glib::IOCondition::IO_ERR | Glib::IOCondition::IO_NVAL
        );

        return true;
    }

    // ── Send SIGTERM to running process ───────────────────────────────────────
    void kill() {
        if (!running_) return;
        io_conn_.disconnect();
        child_watch_conn_.disconnect();
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);
            Glib::spawn_close_pid(pid_);
        }
        if (io_channel_) { io_channel_->close(); io_channel_.reset(); }
        running_ = false;
    }

    [[nodiscard]] bool is_running() const noexcept { return running_; }
    [[nodiscard]] int  exit_code()  const noexcept { return exit_code_; }

private:
    bool   running_   = false;
    int    exit_code_ = -1;
    Glib::Pid pid_    = 0;

    Glib::RefPtr<Glib::IOChannel> io_channel_;
    sigc::connection              io_conn_;
    sigc::connection              child_watch_conn_;
    std::string                   line_buf_;  // partial line buffer

    // ── Called by signal_io when data available ───────────────────────────────
    bool on_io_ready(Glib::IOCondition cond) {
        bool keep = true;

        if ((cond & Glib::IOCondition::IO_IN) == Glib::IOCondition::IO_IN) {
            // Read all available data
            std::string chunk;
            while (true) {
                Glib::ustring ustr;
                auto status = io_channel_->read_line(ustr);
                if (status == Glib::IOStatus::NORMAL) {
                    // Strip trailing CR/LF
                    std::string s = ustr.raw();
                    while (!s.empty() && (s.back()=='\n'||s.back()=='\r')) s.pop_back();
                    // Flush accumulated line_buf_ with this line
                    signal_output_line.emit(line_buf_ + s);
                    line_buf_.clear();
                } else if (status == Glib::IOStatus::AGAIN) {
                    break;    // no more data right now
                } else {
                    // EOF or error
                    keep = false;
                    break;
                }
            }
        }

        auto hangup_flags = Glib::IOCondition::IO_HUP | Glib::IOCondition::IO_ERR | Glib::IOCondition::IO_NVAL;
        if ((cond & hangup_flags) != Glib::IOCondition(0)) {
            keep = false;
        }

        if (!keep) {
            if (io_channel_) { io_channel_->close(); io_channel_.reset(); }
            // finished signal fires from child_watch
        }
        return keep;
    }

    // ── Called by signal_child_watch ──────────────────────────────────────────
    void on_child_exited(int status) {
        io_conn_.disconnect();
        if (io_channel_) { io_channel_->close(); io_channel_.reset(); }

        if (WIFEXITED(status))      exit_code_ = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) exit_code_ = 128 + WTERMSIG(status);
        else                          exit_code_ = -1;

        Glib::spawn_close_pid(pid_);
        running_ = false;
        signal_finished.emit(exit_code_);
    }
};

} // namespace Progressions
