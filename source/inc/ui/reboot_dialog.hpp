#pragma once
#include "../config.hpp"
#include <gtkmm-4.0/gtkmm.h>
#include <format>

namespace Progressions::UI {

// ── Modal dialog: countdown then reboots (or user cancels) ───────────────────
class RebootDialog : public Gtk::Window {
public:
    explicit RebootDialog(Gtk::Window& parent, int delay_seconds = 5) {
        set_transient_for(parent);
        set_modal(true);
        set_resizable(false);
        set_default_size(380, 1);
        set_title("Restart System");
        add_css_class("dialog");

        // ── Header bar ────────────────────────────────────────────────────────
        auto hb = Gtk::make_managed<Gtk::HeaderBar>();
        hb->set_show_title_buttons(false);
        set_titlebar(*hb);

        // ── Root layout ───────────────────────────────────────────────────────
        auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 24);
        root->set_margin(28);
        set_child(*root);

        // ── Icon ─────────────────────────────────────────────────────────────
        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name("system-restart-symbolic");
        icon->set_pixel_size(64);
        icon->set_halign(Gtk::Align::CENTER);
        root->append(*icon);

        // ── Title ─────────────────────────────────────────────────────────────
        auto title = Gtk::make_managed<Gtk::Label>("Restarting…");
        title->add_css_class("title-1");
        title->set_halign(Gtk::Align::CENTER);
        root->append(*title);

        // ── Body text ─────────────────────────────────────────────────────────
        body_label_ = Gtk::make_managed<Gtk::Label>();
        body_label_->add_css_class("body");
        body_label_->set_halign(Gtk::Align::CENTER);
        body_label_->set_justify(Gtk::Justification::CENTER);
        body_label_->set_wrap(true);
        root->append(*body_label_);

        // ── Countdown circle ─────────────────────────────────────────────────
        countdown_label_ = Gtk::make_managed<Gtk::Label>();
        countdown_label_->add_css_class("countdown-label");
        countdown_label_->set_halign(Gtk::Align::CENTER);
        root->append(*countdown_label_);

        // ── Progress bar for countdown ────────────────────────────────────────
        countdown_bar_ = Gtk::make_managed<Gtk::ProgressBar>();
        countdown_bar_->add_css_class("warning");
        root->append(*countdown_bar_);

        // ── Buttons ──────────────────────────────────────────────────────────
        auto btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        btn_box->set_halign(Gtk::Align::CENTER);

        cancel_btn_ = Gtk::make_managed<Gtk::Button>("Cancel Restart");
        cancel_btn_->add_css_class("secondary");
        cancel_btn_->signal_clicked().connect([this]() { cancel(); });
        btn_box->append(*cancel_btn_);

        reboot_now_btn_ = Gtk::make_managed<Gtk::Button>("Restart Now");
        reboot_now_btn_->add_css_class("destructive-action");
        reboot_now_btn_->signal_clicked().connect([this]() { do_reboot(); });
        btn_box->append(*reboot_now_btn_);

        root->append(*btn_box);

        // ── Start countdown ──────────────────────────────────────────────────
        remaining_   = delay_seconds;
        total_delay_ = delay_seconds;
        update_ui();

        timer_conn_ = Glib::signal_timeout().connect_seconds(
            [this]() -> bool {
                --remaining_;
                update_ui();
                if (remaining_ <= 0) {
                    do_reboot();
                    return false;
                }
                return true;
            }, 1);
    }

    ~RebootDialog() override { timer_conn_.disconnect(); }

private:
    Gtk::Label*       body_label_      = nullptr;
    Gtk::Label*       countdown_label_ = nullptr;
    Gtk::ProgressBar* countdown_bar_   = nullptr;
    Gtk::Button*      cancel_btn_      = nullptr;
    Gtk::Button*      reboot_now_btn_  = nullptr;
    sigc::connection  timer_conn_;
    int remaining_   = 5;
    int total_delay_ = 5;

    void update_ui() {
        body_label_->set_text(std::format(
            "Your system will restart in {} second{}.\n"
            "Save any open work before continuing.",
            remaining_, remaining_ == 1 ? "" : "s"));
        countdown_label_->set_text(std::to_string(remaining_));
        countdown_bar_->set_fraction(double(remaining_) / double(total_delay_));
    }

    void cancel() {
        timer_conn_.disconnect();
        close();
    }

    void do_reboot() {
        timer_conn_.disconnect();
        // Request reboot via systemd
        Glib::spawn_command_line_async("pkexec systemctl reboot");
        close();
    }
};

} // namespace Progressions::UI
