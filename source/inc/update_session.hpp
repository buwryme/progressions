#pragma once
#include "types.hpp"
#include "config.hpp"
#include "command_runner.hpp"
#include "output_parser.hpp"
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <sigc++/sigc++.h>

namespace Progressions {

// ── One command slot within a session (may differ from config due to skips) ───
struct SessionCommand {
    CommandConfig    cfg;
    std::string      effective_command; // may have --ignore appended
    CommandStatus    status = CommandStatus::Pending;
    int              exit_code = -1;
    std::vector<std::string> skip_packages;
    bool             skip_this = false;
};

// ── Orchestrates running multiple commands in sequence ────────────────────────
class UpdateSession {
public:
    // ── Signals ───────────────────────────────────────────────────────────────
    sigc::signal<void(std::size_t idx)>             signal_command_started;
    sigc::signal<void(std::size_t idx, int ec)>     signal_command_finished;
    sigc::signal<void(std::string line, ParsedLine)>signal_output;
    sigc::signal<void(SpeedInfo)>                   signal_speed_update;
    sigc::signal<void()>                            signal_all_done;
    sigc::signal<void(std::string)>                 signal_error;

    // ── Build from config ─────────────────────────────────────────────────────
    void prepare(const Config& cfg,
                 const std::vector<bool>&              enabled_overrides,
                 const std::vector<std::vector<std::string>>& skip_packages_per_cmd)
    {
        commands_.clear();
        for (std::size_t i = 0; i < cfg.commands.size(); ++i) {
            SessionCommand sc;
            sc.cfg             = cfg.commands[i];
            sc.skip_this       = !cfg.commands[i].enabled ||
                                  (i < enabled_overrides.size() && !enabled_overrides[i]);
            sc.status          = sc.skip_this ? CommandStatus::Skipped : CommandStatus::Pending;
            sc.skip_packages   = (i < skip_packages_per_cmd.size()) ? skip_packages_per_cmd[i]
                                                                      : std::vector<std::string>{};
            sc.effective_command = build_effective_command(sc);
            commands_.push_back(std::move(sc));
        }
        current_idx_ = 0;
        aborted_     = false;
    }

    void start() {
        aborted_ = false;
        advance();
    }

    void abort() {
        aborted_ = true;
        runner_.kill();
        // Mark remaining pending commands as skipped
        for (auto& sc : commands_)
            if (sc.status == CommandStatus::Running || sc.status == CommandStatus::Pending)
                sc.status = CommandStatus::Skipped;
        signal_all_done.emit();
    }

    [[nodiscard]] const std::vector<SessionCommand>& commands() const noexcept { return commands_; }
    [[nodiscard]] std::size_t current_index() const noexcept { return current_idx_; }
    [[nodiscard]] bool finished() const noexcept { return current_idx_ >= commands_.size(); }
    [[nodiscard]] bool is_running_session() const noexcept { return !aborted_ && !finished(); }

    // ── Overall progress (0–1) ────────────────────────────────────────────────
    [[nodiscard]] double overall_progress() const noexcept {
        if (commands_.empty()) return 1.0;
        int done = 0;
        for (const auto& sc : commands_)
            if (sc.status == CommandStatus::Done || sc.status == CommandStatus::Failed ||
                sc.status == CommandStatus::Skipped)
                ++done;
        return double(done) / double(commands_.size());
    }

    [[nodiscard]] int active_count() const noexcept {
        int n = 0;
        for (const auto& sc : commands_)
            if (!sc.skip_this) ++n;
        return n;
    }

private:
    std::vector<SessionCommand> commands_;
    std::size_t current_idx_ = 0;
    bool        aborted_     = false;
    CommandRunner runner_;
    OutputParser  parser_;

    static std::string build_effective_command(const SessionCommand& sc) {
        if (sc.skip_this) return "";
        std::string cmd = sc.cfg.command;
        if (sc.cfg.supports_skip && !sc.skip_packages.empty()) {
            cmd += " --ignore ";
            for (std::size_t i = 0; i < sc.skip_packages.size(); ++i) {
                if (i > 0) cmd += ",";
                cmd += sc.skip_packages[i];
            }
        }
        return cmd;
    }

    void advance() {
        if (aborted_) return;
        // Skip over "skipped" commands
        while (current_idx_ < commands_.size() &&
               commands_[current_idx_].skip_this) {
            ++current_idx_;
        }
        if (current_idx_ >= commands_.size()) {
            signal_all_done.emit();
            return;
        }

        auto& sc   = commands_[current_idx_];
        sc.status  = CommandStatus::Running;
        signal_command_started.emit(current_idx_);

        // Connect runner signals
        runner_.signal_output_line.connect([this](const std::string& line) {
            auto parsed = parser_.parse(line);
            if (parsed.speed.bytes_per_sec > 0 || parsed.speed.total_pkg > 0)
                signal_speed_update.emit(parsed.speed);
            signal_output.emit(line, parsed);
        });

        runner_.signal_finished.connect([this](int ec) {
            on_command_done(ec);
        });

        runner_.signal_error.connect([this](const std::string& err) {
            signal_error.emit(err);
            on_command_done(-1);
        });

        runner_.run(sc.effective_command);
    }

    void on_command_done(int ec) {
        if (current_idx_ >= commands_.size()) return;
        auto& sc   = commands_[current_idx_];
        sc.exit_code = ec;
        sc.status    = (ec == 0) ? CommandStatus::Done : CommandStatus::Failed;
        signal_command_finished.emit(current_idx_, ec);

        ++current_idx_;

        // Reconnect runner for next command (fresh instance)
        runner_.signal_output_line.clear();
        runner_.signal_finished.clear();
        runner_.signal_error.clear();

        Glib::signal_timeout().connect_once([this] { advance(); }, 300);
    }
};

} // namespace Progressions
