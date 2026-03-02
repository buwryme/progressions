#pragma once
#include "../config.hpp"
#include <gtkmm.h>
#include <giomm.h>
#include <functional>
#include <vector>
#include <sstream>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <climits>

namespace Progressions::UI {

class SettingsWindow : public Gtk::Window {
public:
    using SavedCallback = std::function<void(Config)>;

    explicit SettingsWindow(Gtk::Window& parent, Config cfg, SavedCallback cb)
        : cfg_(std::move(cfg)), saved_cb_(std::move(cb))
    {
        set_transient_for(parent);
        set_modal(true);
        set_resizable(true);
        set_default_size(620, 720); // Slightly taller for automation
        set_title("Progressions — Settings");

        auto hb = Gtk::make_managed<Gtk::HeaderBar>();
        hb->set_show_title_buttons(false);
        auto title_lbl = Gtk::make_managed<Gtk::Label>("Settings");
        title_lbl->add_css_class("title-3");
        hb->set_title_widget(*title_lbl);

        auto add_btn = Gtk::make_managed<Gtk::Button>();
        auto add_icon = Gtk::make_managed<Gtk::Image>();
        add_icon->set_from_icon_name("list-add-symbolic");
        add_btn->set_child(*add_icon);
        add_btn->add_css_class("flat-icon");
        add_btn->set_tooltip_text("Add command");
        add_btn->signal_clicked().connect([this]() { add_command_row(); });
        hb->pack_end(*add_btn);
        set_titlebar(*hb);

        auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        set_child(*root);

        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_vexpand(true);
        root->append(*scroll);

        auto content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 24);
        content->set_margin(20);
        scroll->set_child(*content);

        // ── Section: Commands ─────────────────────────────────────────────────
        auto cmd_hdr = Gtk::make_managed<Gtk::Label>("Update Commands");
        cmd_hdr->add_css_class("title-2");
        cmd_hdr->set_halign(Gtk::Align::START);
        content->append(*cmd_hdr);

        cmds_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        content->append(*cmds_box_);

        for (std::size_t i = 0; i < cfg_.commands.size(); ++i)
            append_command_row(cfg_.commands[i]);

        // ── Section: Automation ───────────────────────────────────────────────
        content->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
        auto auto_hdr = Gtk::make_managed<Gtk::Label>("Automation");
        auto_hdr->add_css_class("title-2");
        auto_hdr->set_halign(Gtk::Align::START);
        content->append(*auto_hdr);

        auto auto_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
        auto_card->add_css_class("card");
        content->append(*auto_card);

        auto sysd_row = make_setting_row("Systemd Integration",
            "Enable automatic background updates via a ~/.config/systemd/user/ timer (no root needed).");
        auto sysd_sw = Gtk::make_managed<Gtk::Switch>();
        sysd_sw->set_active(cfg_.settings.systemd_configured);
        sysd_sw->set_valign(Gtk::Align::CENTER);
        sysd_row->append(*sysd_sw);
        auto_card->append(*sysd_row);

        auto freq_row = make_setting_row("Update Frequency", "How often to check for updates in the background.");
        auto freq_combo = Gtk::make_managed<Gtk::ComboBoxText>();
        freq_combo->append("daily", "Daily");
        freq_combo->append("weekly", "Weekly");
        freq_combo->append("monthly", "Monthly");
        freq_combo->set_active_id(cfg_.settings.update_interval);
        freq_combo->set_valign(Gtk::Align::CENTER);
        freq_row->append(*freq_combo);
        auto_card->append(*freq_row);

        // ── Section: App Settings ─────────────────────────────────────────────
        content->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
        auto app_hdr = Gtk::make_managed<Gtk::Label>("Application Settings");
        app_hdr->add_css_class("title-2");
        app_hdr->set_halign(Gtk::Align::START);
        content->append(*app_hdr);

        auto settings_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
        settings_card->add_css_class("card");
        content->append(*settings_card);

        auto delay_row = make_setting_row("Reboot Countdown (seconds)", "Seconds to wait before auto-rebooting.");
        auto delay_spin = Gtk::make_managed<Gtk::SpinButton>();
        delay_spin->set_range(1, 60);
        delay_spin->set_increments(1, 5);
        delay_spin->set_value(cfg_.settings.reboot_delay_seconds);
        delay_spin->set_valign(Gtk::Align::CENTER);
        delay_row->append(*delay_spin);
        settings_card->append(*delay_row);

        auto confirm_row = make_setting_row("Confirm Before Starting", "Show a dialog to configure the update before it runs.");
        auto confirm_sw = Gtk::make_managed<Gtk::Switch>();
        confirm_sw->set_active(cfg_.settings.confirm_before_start);
        confirm_sw->set_valign(Gtk::Align::CENTER);
        confirm_row->append(*confirm_sw);
        settings_card->append(*confirm_row);

        auto scroll_row = make_setting_row("Auto-scroll Output", "Keep the output terminal scrolled to the latest line.");
        auto scroll_sw = Gtk::make_managed<Gtk::Switch>();
        scroll_sw->set_active(cfg_.settings.scroll_to_bottom);
        scroll_sw->set_valign(Gtk::Align::CENTER);
        scroll_row->append(*scroll_sw);
        settings_card->append(*scroll_row);

        // ── Bottom buttons ────────────────────────────────────────────────────
        auto btn_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        btn_bar->set_margin(20);
        btn_bar->set_halign(Gtk::Align::END);

        auto cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
        cancel_btn->signal_clicked().connect([this]() { close(); });
        btn_bar->append(*cancel_btn);

        auto save_btn = Gtk::make_managed<Gtk::Button>("Save Settings");
        save_btn->add_css_class("primary");
        save_btn->signal_clicked().connect([this, delay_spin, confirm_sw, scroll_sw, sysd_sw, freq_combo]() {
            bool old_systemd       = cfg_.settings.systemd_configured;
            std::string old_itvl   = cfg_.settings.update_interval;
            collect_settings(delay_spin, confirm_sw, scroll_sw, sysd_sw, freq_combo);
            cfg_.save();

            // Re-install / remove systemd units if relevant settings changed
            bool changed = (cfg_.settings.systemd_configured != old_systemd)
                        || (cfg_.settings.update_interval    != old_itvl);
            if (changed) apply_systemd_changes();

            saved_cb_(cfg_);
            close();
        });
        btn_bar->append(*save_btn);
        root->append(*btn_bar);
    }

private:
    Config                    cfg_;
    SavedCallback              saved_cb_;
    Gtk::Box* cmds_box_ = nullptr;

    struct CmdRowWidgets {
        Gtk::Entry* name_entry; Gtk::Entry* cmd_entry; Gtk::Entry* icon_entry;
        Gtk::Entry* desc_entry; Gtk::Switch* enabled_sw; Gtk::Switch* skip_sw;
        std::string id;
    };
    std::vector<CmdRowWidgets> row_widgets_;

    static Gtk::Box* make_setting_row(const std::string& title, const std::string& subtitle) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        auto info = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
        info->set_hexpand(true);
        auto t = Gtk::make_managed<Gtk::Label>(title); t->set_halign(Gtk::Align::START);
        auto s = Gtk::make_managed<Gtk::Label>(subtitle); s->add_css_class("caption"); s->set_halign(Gtk::Align::START); s->set_wrap(true);
        info->append(*t); info->append(*s); row->append(*info);
        return row;
    }

    void append_command_row(const CommandConfig& cc) {
        auto card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        card->add_css_class("card");
        cmds_box_->append(*card);

        auto header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        card->append(*header);

        auto name_entry = Gtk::make_managed<Gtk::Entry>(); name_entry->set_text(cc.name); name_entry->set_hexpand(true);
        header->append(*name_entry);

        auto en_sw = Gtk::make_managed<Gtk::Switch>(); en_sw->set_active(cc.enabled); header->append(*en_sw);

        auto del_btn = Gtk::make_managed<Gtk::Button>();
        auto del_icon = Gtk::make_managed<Gtk::Image>(); del_icon->set_from_icon_name("list-remove-symbolic");
        del_btn->set_child(*del_icon); del_btn->signal_clicked().connect([this, card]() { cmds_box_->remove(*card); });
        header->append(*del_btn);

        auto cmd_grid = Gtk::make_managed<Gtk::Grid>(); cmd_grid->set_row_spacing(6); cmd_grid->set_column_spacing(12); card->append(*cmd_grid);

        auto cmd_entry = Gtk::make_managed<Gtk::Entry>(); cmd_entry->set_text(cc.command); cmd_entry->set_hexpand(true);
        cmd_grid->attach(*Gtk::make_managed<Gtk::Label>("Command"), 0, 0, 1, 1); cmd_grid->attach(*cmd_entry, 1, 0, 1, 1);

        auto icon_entry = Gtk::make_managed<Gtk::Entry>(); icon_entry->set_text(cc.icon_name);
        cmd_grid->attach(*Gtk::make_managed<Gtk::Label>("Icon"), 0, 1, 1, 1); cmd_grid->attach(*icon_entry, 1, 1, 1, 1);

        auto desc_entry = Gtk::make_managed<Gtk::Entry>(); desc_entry->set_text(cc.description);
        cmd_grid->attach(*Gtk::make_managed<Gtk::Label>("Desc"), 0, 2, 1, 1); cmd_grid->attach(*desc_entry, 1, 2, 1, 1);

        auto skip_sw = Gtk::make_managed<Gtk::Switch>(); skip_sw->set_active(cc.supports_skip);
        auto skip_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        skip_box->append(*Gtk::make_managed<Gtk::Label>("Supports skip")); skip_box->append(*skip_sw);
        card->append(*skip_box);

        row_widgets_.push_back({name_entry, cmd_entry, icon_entry, desc_entry, en_sw, skip_sw, cc.id});
    }

    void add_command_row() {
        CommandConfig blank; blank.id = "custom_" + std::to_string(row_widgets_.size());
        blank.name = "New Command"; blank.command = "echo hello"; append_command_row(blank);
    }

    // ── Install or remove systemd user units ──────────────────────────────────
    void apply_systemd_changes() {
        const char* home = std::getenv("HOME");
        if (!home) return;

        std::filesystem::path udir =
            std::filesystem::path(home) / ".config" / "systemd" / "user";

        if (!cfg_.settings.systemd_configured) {
            // Remove
            std::system("systemctl --user disable --now progressions.timer 2>/dev/null");
            std::filesystem::remove(udir / "progressions.service");
            std::filesystem::remove(udir / "progressions.timer");
            std::system("systemctl --user daemon-reload");
            return;
        }

        std::filesystem::create_directories(udir);

        char exe[PATH_MAX] = {};
        if (::readlink("/proc/self/exe", exe, sizeof(exe)-1) < 0)
            std::strcpy(exe, "/usr/local/bin/progressions");

        const std::string& interval = cfg_.settings.update_interval;
        const std::string on_cal =
            (interval == "weekly")  ? "weekly"  :
            (interval == "monthly") ? "monthly" : "daily";

        std::ofstream(udir / "progressions.service")
            << "[Unit]\nDescription=Progressions Background System Update\n"
            << "After=network-online.target\nWants=network-online.target\n\n"
            << "[Service]\nType=oneshot\nExecStart=" << exe << " --headless\n"
            << "KillMode=process\nTimeoutStopSec=600\n\n"
            << "[Install]\nWantedBy=default.target\n";

        std::ofstream(udir / "progressions.timer")
            << "[Unit]\nDescription=Progressions " << interval << " timer\n\n"
            << "[Timer]\nOnCalendar=" << on_cal << "\nPersistent=true\n"
            << "RandomizedDelaySec=15m\n\n"
            << "[Install]\nWantedBy=timers.target\n";

        std::system("systemctl --user daemon-reload");
        std::system("systemctl --user enable --now progressions.timer");
    }

    void collect_settings(Gtk::SpinButton* delay_spin, Gtk::Switch* confirm_sw, Gtk::Switch* scroll_sw, Gtk::Switch* sysd_sw, Gtk::ComboBoxText* freq_combo) {
        cfg_.settings.reboot_delay_seconds = int(delay_spin->get_value());
        cfg_.settings.confirm_before_start = confirm_sw->get_active();
        cfg_.settings.scroll_to_bottom     = scroll_sw->get_active();
        cfg_.settings.systemd_configured   = sysd_sw->get_active();
        cfg_.settings.update_interval      = freq_combo->get_active_id();

        cfg_.commands.clear();
        for (auto& rw : row_widgets_) {
            CommandConfig cc;
            cc.id = rw.id; cc.name = rw.name_entry->get_text(); cc.command = rw.cmd_entry->get_text();
            cc.icon_name = rw.icon_entry->get_text(); cc.description = rw.desc_entry->get_text();
            cc.enabled = rw.enabled_sw->get_active(); cc.supports_skip = rw.skip_sw->get_active();
            if (!cc.name.empty() && !cc.command.empty()) cfg_.commands.push_back(cc);
        }
    }
};

} // namespace Progressions::UI
