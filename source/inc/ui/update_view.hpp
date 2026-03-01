#pragma once
#include "../types.hpp"
#include "../config.hpp"
#include "../update_session.hpp"
#include "../output_parser.hpp"
#include "reboot_dialog.hpp"
#include "pre_update_dialog.hpp"
#include <gtkmm.h>
#include <format>
#include <chrono>
#include <deque>
#include <ctime>

namespace Progressions::UI {

// ── Window shown while an update is running ───────────────────────────────────
class UpdateView : public Gtk::Window {
private:
    Gdk::RGBA get_color_from_name(const std::string& name) {
        Gdk::RGBA color;
        // This looks up the color defined by the user's current GTK theme
        if (!get_style_context()->lookup_color(name, color)) {
            // Fallback if the theme is weird
            color.set_rgba(0.5, 0.5, 0.5, 1.0);
        }
        return color;
    }
public:
    explicit UpdateView(Gtk::Window& parent,
                        const Config& cfg,
                        const PreUpdateChoice& choice)
        : session_(std::make_unique<UpdateSession>()),
          cfg_(cfg), reboot_after_(choice.reboot_after),
          parent_(&parent)
    {
        session_->prepare(cfg_, choice.command_enabled, choice.skip_packages);
        set_transient_for(parent);
        set_modal(true);
        set_resizable(true);
        set_default_size(680, 700);
        set_title("Progressions — Updating");

        // ── Header bar ────────────────────────────────────────────────────────
        hb_ = Gtk::make_managed<Gtk::HeaderBar>();
        hb_->set_show_title_buttons(false);
        header_title_ = Gtk::make_managed<Gtk::Label>("Preparing…");
        header_title_->add_css_class("title-3");
        hb_->set_title_widget(*header_title_);

        abort_btn_ = Gtk::make_managed<Gtk::Button>("Cancel");
        abort_btn_->add_css_class("secondary");
        abort_btn_->signal_clicked().connect([this]() { session_->abort(); });
        hb_->pack_end(*abort_btn_);
        set_titlebar(*hb_);

        // ── Root ─────────────────────────────────────────────────────────────
        auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        set_child(*root);

        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_vexpand(true);
        root->append(*scroll);

        auto content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
        content->set_margin(20);
        scroll->set_child(*content);

        // ── Overall progress card ─────────────────────────────────────────────
        auto overall_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        overall_card->add_css_class("card");
        content->append(*overall_card);

        auto oa_hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        overall_card->append(*oa_hdr);

        auto oa_icon = Gtk::make_managed<Gtk::Image>();
        oa_icon->set_from_icon_name("emblem-synchronizing-symbolic");
        oa_icon->set_pixel_size(20);
        oa_hdr->append(*oa_icon);

        auto oa_title_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        oa_title_box->set_hexpand(true);
        overall_title_ = Gtk::make_managed<Gtk::Label>("Overall Progress");
        overall_title_->add_css_class("title-3");
        overall_title_->set_halign(Gtk::Align::START);
        overall_step_ = Gtk::make_managed<Gtk::Label>("Starting…");
        overall_step_->add_css_class("caption");
        overall_step_->set_halign(Gtk::Align::START);
        oa_title_box->append(*overall_title_);
        oa_title_box->append(*overall_step_);
        oa_hdr->append(*oa_title_box);

        overall_pct_label_ = Gtk::make_managed<Gtk::Label>("0%");
        overall_pct_label_->add_css_class("speed-badge");
        oa_hdr->append(*overall_pct_label_);

        overall_bar_ = Gtk::make_managed<Gtk::ProgressBar>();
        overall_card->append(*overall_bar_);

        // ── Current command card ──────────────────────────────────────────────
        cur_card_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        cur_card_->add_css_class("card");
        content->append(*cur_card_);

        auto cur_hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        cur_card_->append(*cur_hdr);

        cur_icon_ = Gtk::make_managed<Gtk::Image>();
        cur_icon_->set_from_icon_name("system-run-symbolic");
        cur_icon_->set_pixel_size(20);
        cur_hdr->append(*cur_icon_);

        auto cur_info = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        cur_info->set_hexpand(true);
        cur_cmd_title_ = Gtk::make_managed<Gtk::Label>("—");
        cur_cmd_title_->add_css_class("title-3");
        cur_cmd_title_->set_halign(Gtk::Align::START);
        cur_cmd_str_ = Gtk::make_managed<Gtk::Label>("");
        cur_cmd_str_->add_css_class("caption-mono");
        cur_cmd_str_->set_halign(Gtk::Align::START);
        cur_cmd_str_->set_ellipsize(Pango::EllipsizeMode::END);
        cur_info->append(*cur_cmd_title_);
        cur_info->append(*cur_cmd_str_);
        cur_hdr->append(*cur_info);

        cur_status_pill_ = Gtk::make_managed<Gtk::Label>("Pending");
        cur_status_pill_->add_css_class("status-pill");
        cur_status_pill_->add_css_class("pending");
        cur_hdr->append(*cur_status_pill_);

        cur_bar_ = Gtk::make_managed<Gtk::ProgressBar>();
        cur_bar_->add_css_class("thin");
        cur_card_->append(*cur_bar_);

        // ── Stats row (speed, packages) ───────────────────────────────────────
        auto stats_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        cur_card_->append(*stats_row);

        speed_badge_ = Gtk::make_managed<Gtk::Label>("");
        speed_badge_->add_css_class("speed-badge");
        speed_badge_->set_visible(false);
        stats_row->append(*speed_badge_);

        pkg_badge_ = Gtk::make_managed<Gtk::Label>("");
        pkg_badge_->add_css_class("stat-badge");
        pkg_badge_->set_visible(false);
        stats_row->append(*pkg_badge_);

        cur_pkg_label_ = Gtk::make_managed<Gtk::Label>("");
        cur_pkg_label_->add_css_class("caption");
        cur_pkg_label_->set_ellipsize(Pango::EllipsizeMode::END);
        stats_row->append(*cur_pkg_label_);

        // ── Commands overview mini-list ───────────────────────────────────────
        auto cmd_list_hdr = Gtk::make_managed<Gtk::Label>("Steps");
        cmd_list_hdr->add_css_class("title-3");
        cmd_list_hdr->set_halign(Gtk::Align::START);
        content->append(*cmd_list_hdr);

        cmd_list_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        content->append(*cmd_list_box_);

        for (const auto& sc : session_->commands()) {
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
            row->add_css_class("command-row");
            cmd_list_box_->append(*row);

            auto ic = Gtk::make_managed<Gtk::Image>();
            ic->set_from_icon_name(sc.cfg.icon_name);
            ic->set_pixel_size(18);
            row->append(*ic);

            auto nl = Gtk::make_managed<Gtk::Label>(sc.cfg.name);
            nl->add_css_class("body");
            nl->set_hexpand(true);
            nl->set_halign(Gtk::Align::START);
            row->append(*nl);

            auto pl = Gtk::make_managed<Gtk::Label>(
                sc.skip_this ? "Skipped" : "Pending");
            pl->add_css_class("status-pill");
            pl->add_css_class(sc.skip_this ? "skipped" : "pending");
            row->append(*pl);

            step_rows_.push_back({row, ic, nl, pl});
        }

        // ── Output terminal ───────────────────────────────────────────────────
        auto out_hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        auto out_title = Gtk::make_managed<Gtk::Label>("Output");
        out_title->add_css_class("title-3");
        out_title->set_hexpand(true);
        out_hdr->append(*out_title);
        // Copy-to-clipboard button
        auto copy_btn = Gtk::make_managed<Gtk::Button>();
        auto copy_icon = Gtk::make_managed<Gtk::Image>();
        copy_icon->set_from_icon_name("edit-copy-symbolic");
        copy_btn->set_child(*copy_icon);
        copy_btn->add_css_class("flat-icon");
        copy_btn->set_tooltip_text("Copy all output");
        copy_btn->signal_clicked().connect([this]() {
            auto buf = text_view_->get_buffer();
            auto text = buf->get_text();
            get_clipboard()->set_text(text);
        });
        out_hdr->append(*copy_btn);
        content->append(*out_hdr);

        auto term_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        term_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        term_scroll->set_min_content_height(200);
        term_scroll->set_vexpand(false);
        content->append(*term_scroll);

        term_vadj_ = Glib::RefPtr<Gtk::Adjustment>();

        text_view_ = Gtk::make_managed<Gtk::TextView>();
        text_view_->set_editable(false);
        text_view_->set_cursor_visible(false);
        text_view_->add_css_class("terminal-view");
        text_view_->set_monospace(true);
        text_view_->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
        term_scroll->set_child(*text_view_);
        term_vadj_ = term_scroll->get_vadjustment();

        // Set up text tags using System Named Colors
        auto buf = text_view_->get_buffer();
        tag_accent_  = buf->create_tag("accent");
        tag_accent_->property_foreground_rgba() = get_color_from_name("accent_color");

        tag_success_ = buf->create_tag("success");
        tag_success_->property_foreground_rgba() = get_color_from_name("success_color");

        tag_warning_ = buf->create_tag("warning");
        tag_warning_->property_foreground_rgba() = get_color_from_name("warning_color");

        tag_error_   = buf->create_tag("error");
        tag_error_->property_foreground_rgba() = get_color_from_name("error_color");

        tag_dim_     = buf->create_tag("dim");
        auto __dim_color = get_color_from_name("window_fg_color");
        __dim_color.set_alpha(0.5);
        tag_dim_->property_foreground_rgba() = __dim_color;

        // ── Connect session signals (store connections for safe cleanup) ────────
        session_conns_.push_back(session_->signal_command_started.connect(
            [this](std::size_t idx) { on_command_started(idx); }));
        session_conns_.push_back(session_->signal_command_finished.connect(
            [this](std::size_t idx, int ec) { on_command_finished(idx, ec); }));
        session_conns_.push_back(session_->signal_output.connect(
            [this](const std::string& line, const ParsedLine& parsed) {
                on_output_line(line, parsed);
            }));
        session_conns_.push_back(session_->signal_speed_update.connect(
            [this](const SpeedInfo& si) { on_speed_update(si); }));
        session_conns_.push_back(session_->signal_all_done.connect(
            [this]() { on_all_done(); }));
        session_conns_.push_back(session_->signal_error.connect(
            [this](const std::string& err) { append_output("[ERROR] " + err, nullptr); }));

        // ── Pulse timer for indeterminate progress ────────────────────────────
        pulse_timer_ = Glib::signal_timeout().connect(
            [this]() -> bool {
                if (cur_progress_ < 0) cur_bar_->pulse();
                return true;
            }, 80);

        // ── Start! ────────────────────────────────────────────────────────────
        session_->start();
        start_time_ = std::chrono::steady_clock::now();
        update_elapsed_timer_ = Glib::signal_timeout().connect_seconds(
            [this]() -> bool { update_elapsed(); return !session_->finished(); }, 1);
    }

    ~UpdateView() override {
        pulse_timer_.disconnect();
        update_elapsed_timer_.disconnect();
        // Disconnect all session signal connections to prevent callbacks into deleted view
        for (auto& c : session_conns_) c.disconnect();
        if (session_ && session_->is_running_session()) session_->abort();
    }

private:
    std::unique_ptr<UpdateSession> session_;
    std::vector<sigc::connection>  session_conns_;
    Config            cfg_;
    bool              reboot_after_;
    Gtk::Window*      parent_;

    // ── Header ────────────────────────────────────────────────────────────────
    Gtk::HeaderBar*   hb_               = nullptr;
    Gtk::Label*       header_title_     = nullptr;
    Gtk::Button*      abort_btn_        = nullptr;

    // ── Overall progress ─────────────────────────────────────────────────────
    Gtk::ProgressBar* overall_bar_      = nullptr;
    Gtk::Label*       overall_title_    = nullptr;
    Gtk::Label*       overall_step_     = nullptr;
    Gtk::Label*       overall_pct_label_= nullptr;

    // ── Current command ───────────────────────────────────────────────────────
    Gtk::Box*         cur_card_         = nullptr;
    Gtk::Image*       cur_icon_         = nullptr;
    Gtk::Label*       cur_cmd_title_    = nullptr;
    Gtk::Label*       cur_cmd_str_      = nullptr;
    Gtk::Label*       cur_status_pill_  = nullptr;
    Gtk::ProgressBar* cur_bar_          = nullptr;
    Gtk::Label*       speed_badge_      = nullptr;
    Gtk::Label*       pkg_badge_        = nullptr;
    Gtk::Label*       cur_pkg_label_    = nullptr;
    double            cur_progress_     = -1.0;

    // ── Command step rows ─────────────────────────────────────────────────────
    Gtk::Box*         cmd_list_box_     = nullptr;
    struct StepRow { Gtk::Box* row; Gtk::Image* icon; Gtk::Label* name; Gtk::Label* pill; };
    std::vector<StepRow> step_rows_;

    // ── Terminal output ───────────────────────────────────────────────────────
    Gtk::TextView*    text_view_        = nullptr;
    Glib::RefPtr<Gtk::Adjustment> term_vadj_;
    Glib::RefPtr<Gtk::TextTag> tag_accent_, tag_success_, tag_warning_,
                                tag_error_,  tag_dim_;

    // ── Timers ────────────────────────────────────────────────────────────────
    sigc::connection  pulse_timer_;
    sigc::connection  update_elapsed_timer_;
    std::chrono::steady_clock::time_point start_time_;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void set_step_pill_class(Gtk::Label* pill, CommandStatus s) {
        for (auto cls : {"pending","running","done","failed","skipped"})
            pill->remove_css_class(cls);
        switch (s) {
            case CommandStatus::Pending:  pill->add_css_class("pending");  break;
            case CommandStatus::Running:  pill->add_css_class("running");  break;
            case CommandStatus::Done:     pill->add_css_class("done");     break;
            case CommandStatus::Failed:   pill->add_css_class("failed");   break;
            case CommandStatus::Skipped:  pill->add_css_class("skipped");  break;
        }
        pill->set_text(status_label(s));
    }

    void set_cur_pill_class(const std::string& cls, const std::string& text) {
        for (auto c : {"pending","running","done","failed","skipped"})
            cur_status_pill_->remove_css_class(c);
        cur_status_pill_->add_css_class(cls);
        cur_status_pill_->set_text(text);
    }

    void append_output(const std::string& line, Glib::RefPtr<Gtk::TextTag> tag) {
        auto buf  = text_view_->get_buffer();
        auto iter = buf->end();
        if (!buf->get_text().empty()) buf->insert(iter, "\n");
        iter = buf->end();
        if (tag) buf->insert_with_tag(iter, line, tag);
        else     buf->insert(iter, line);

        // auto-scroll
        if (cfg_.settings.scroll_to_bottom) {
            Glib::signal_idle().connect_once([this]() {
                term_vadj_->set_value(term_vadj_->get_upper());
            });
        }
    }

    void update_elapsed() {
        auto now  = std::chrono::steady_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        auto min  = secs / 60, sec = secs % 60;
        header_title_->set_text(std::format("Updating — {:02}:{:02}", min, sec));
    }

    // ── Session signal handlers ───────────────────────────────────────────────
    void on_command_started(std::size_t idx) {
        if (idx >= session_->commands().size()) return;
        const auto& sc = session_->commands()[idx];

        // Update current command card
        cur_icon_->set_from_icon_name(sc.cfg.icon_name);
        cur_cmd_title_->set_text(sc.cfg.name);
        cur_cmd_str_->set_text(sc.effective_command);
        cur_progress_ = -1.0;
        cur_bar_->set_fraction(0);
        set_cur_pill_class("running", "Running…");

        // Step rows
        if (idx < step_rows_.size()) {
            step_rows_[idx].row->remove_css_class("pending");
            step_rows_[idx].row->add_css_class("running");
            set_step_pill_class(step_rows_[idx].pill, CommandStatus::Running);
        }

        // Overall progress
        auto total = session_->commands().size();
        overall_step_->set_text(std::format("Step {} of {} — {}", idx+1, total, sc.cfg.name));
        refresh_overall_bar();

        // Separator line in output
        auto sep_line = std::format("── {} ────────────────────────", sc.cfg.name);
        append_output(sep_line, tag_accent_);
    }

    void on_command_finished(std::size_t idx, int ec) {
        if (idx >= session_->commands().size()) return;
        bool ok = (ec == 0);

        if (idx < step_rows_.size()) {
            auto status = ok ? CommandStatus::Done : CommandStatus::Failed;
            for (auto cls : {"running","pending"})
                step_rows_[idx].row->remove_css_class(cls);
            step_rows_[idx].row->add_css_class(ok ? "done" : "failed");
            set_step_pill_class(step_rows_[idx].pill, status);
        }

        cur_bar_->set_fraction(1.0);
        cur_bar_->remove_css_class("warning");
        cur_bar_->add_css_class(ok ? "success" : "error");
        cur_progress_ = 1.0;
        set_cur_pill_class(ok ? "done" : "failed", ok ? "Done" : "Failed");

        refresh_overall_bar();
        append_output(std::format("── Finished (exit {}) ─────────────────", ec),
                      ok ? tag_success_ : tag_error_);
    }

    void on_output_line(const std::string& line, const ParsedLine& parsed) {
        // Pick tag
        Glib::RefPtr<Gtk::TextTag> tag;
        switch (OutputParser::classify_color(parsed)) {
            case OutputParser::LineColor::Accent:  tag = tag_accent_;  break;
            case OutputParser::LineColor::Success: tag = tag_success_; break;
            case OutputParser::LineColor::Warning: tag = tag_warning_; break;
            case OutputParser::LineColor::Error:   tag = tag_error_;   break;
            case OutputParser::LineColor::Dim:     tag = tag_dim_;     break;
            default: break;
        }
        append_output(line, tag);

        // Update progress if we have it
        if (parsed.progress >= 0) {
            cur_progress_ = parsed.progress;
            cur_bar_->set_fraction(cur_progress_);
        }

        // Update package label
        if (!parsed.speed.package_name.empty()) {
            cur_pkg_label_->set_text(parsed.speed.package_name);
            cur_pkg_label_->set_visible(true);
        }
    }

    void on_speed_update(const SpeedInfo& si) {
        if (si.bytes_per_sec > 0) {
            speed_badge_->set_text("↓ " + si.format_speed());
            speed_badge_->set_visible(true);
        }
        if (si.total_pkg > 0) {
            pkg_badge_->set_text(si.format_packages());
            pkg_badge_->set_visible(true);
            // Update current bar fraction if we have package count
            if (cur_progress_ < 0 && si.total_pkg > 0) {
                cur_progress_ = double(si.current_pkg) / double(si.total_pkg);
                cur_bar_->set_fraction(cur_progress_);
            }
        }
    }

    void refresh_overall_bar() {
        double p = session_->overall_progress();
        overall_bar_->set_fraction(p);
        int pct = int(p * 100);
        overall_pct_label_->set_text(std::format("{}%", pct));
    }

    void on_all_done() {
        pulse_timer_.disconnect();
        update_elapsed_timer_.disconnect();
        abort_btn_->set_label("Close");
        abort_btn_->remove_css_class("secondary");
        abort_btn_->add_css_class("primary");
        abort_btn_->signal_clicked().connect([this]() { close(); });

        overall_bar_->set_fraction(1.0);
        overall_bar_->add_css_class("success");
        overall_step_->set_text("All steps completed.");
        overall_pct_label_->set_text("100%");
        header_title_->set_text("Update Complete ✓");

        // Check for failures
        bool any_failed = false;
        for (const auto& sc : session_->commands())
            if (sc.status == CommandStatus::Failed) { any_failed = true; break; }

        append_output(
            any_failed ? "\n⚠ Some steps failed. Check output above."
                       : "\n✓ Update completed successfully.",
            any_failed ? tag_warning_ : tag_success_);

        // Update last_updated timestamp
        auto t = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(t);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&now_t));
        cfg_.last_updated = buf;
        cfg_.save();

        // Reboot if requested
        if (reboot_after_ && !any_failed) {
            auto* dlg = new RebootDialog(*parent_, cfg_.settings.reboot_delay_seconds);
            dlg->signal_hide().connect([dlg]() {
                Glib::signal_idle().connect_once([dlg]() { delete dlg; });
            });
            dlg->present();
        } else if (reboot_after_) {
            // Ask user since there were failures
            auto dlg = Gtk::AlertDialog::create(
                "Update had errors. Do you still want to restart?");
            dlg->set_buttons({"Restart Anyway", "Cancel"});
            dlg->set_default_button(1);
            dlg->set_cancel_button(1);
            dlg->choose(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                    if (dlg->choose_finish(result) == 0) {
                        auto* rdlg = new RebootDialog(*parent_, cfg_.settings.reboot_delay_seconds);
                        rdlg->signal_hide().connect([rdlg]() {
                            Glib::signal_idle().connect_once([rdlg]() { delete rdlg; });
                        });
                        rdlg->present();
                    }
                } catch (const Glib::Error& e) {
                    throw std::runtime_error(e.what());
                }
            });
        }
    }
};

} // namespace Progressions::UI
