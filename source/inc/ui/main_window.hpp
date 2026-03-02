#pragma once
#include "../config.hpp"
#include "../update_session.hpp"
#include "../types.hpp"
#include "pre_update_dialog.hpp"
#include "settings_window.hpp"
#include "update_view.hpp"
#include <gtkmm.h>
#include <format>
#include <chrono>
#include <ctime>

namespace Progressions::UI {

// ── Main application window ───────────────────────────────────────────────────
class MainWindow : public Gtk::ApplicationWindow {
public:
    explicit MainWindow() {
        set_title("Progressions");
        set_default_size(520, 640);
        set_resizable(true);

        cfg_ = Config::load();

        build_ui();
        refresh_command_list();
    }

private:
    Config          cfg_;

    // ── Widget pointers ───────────────────────────────────────────────────────
    Gtk::Box*       commands_box_      = nullptr;
    Gtk::Label*     last_updated_lbl_  = nullptr;
    Gtk::Button*    update_btn_        = nullptr;
    Gtk::Label*     status_dot_        = nullptr;

    // ── Build full UI ─────────────────────────────────────────────────────────
    void build_ui() {
        // ── Header bar ────────────────────────────────────────────────────────
        auto hb = Gtk::make_managed<Gtk::HeaderBar>();
        hb->set_show_title_buttons(true);
        set_titlebar(*hb);
        // App icon + title widget
        auto title_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto app_icon  = Gtk::make_managed<Gtk::Image>();
        app_icon->set_from_icon_name("software-update-available");
        app_icon->set_pixel_size(18);
        title_box->append(*app_icon);
        auto app_title = Gtk::make_managed<Gtk::Label>("Progressions");
        app_title->add_css_class("title-3");
        title_box->append(*app_title);
        hb->set_title_widget(*title_box);

        // Settings button
        auto settings_btn = Gtk::make_managed<Gtk::Button>();
        auto settings_icon = Gtk::make_managed<Gtk::Image>();
        settings_icon->set_from_icon_name("preferences-system-symbolic");
        settings_btn->set_child(*settings_icon);
        settings_btn->add_css_class("flat-icon");
        settings_btn->set_tooltip_text("Settings");
        settings_btn->signal_clicked().connect([this]() { open_settings(); });
        hb->pack_end(*settings_btn);

        set_titlebar(*hb);

        // ── Scrollable root ───────────────────────────────────────────────────
        auto outer_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        outer_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        set_child(*outer_scroll);

        auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 24);
        root->set_margin(20);
        outer_scroll->set_child(*root);

        // ── Hero area ─────────────────────────────────────────────────────────
        auto hero = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        hero->add_css_class("card");
        hero->set_margin_bottom(4);
        root->append(*hero);

        auto hero_top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        hero->append(*hero_top);

        auto hero_icon = Gtk::make_managed<Gtk::Image>();
        hero_icon->set_from_icon_name("software-update-available");
        hero_icon->set_pixel_size(52);
        hero_top->append(*hero_icon);

        auto hero_info = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        hero_info->set_hexpand(true);
        hero_info->set_valign(Gtk::Align::CENTER);

        auto hero_title = Gtk::make_managed<Gtk::Label>("System Update");
        hero_title->add_css_class("title-1");
        hero_title->set_halign(Gtk::Align::START);
        hero_info->append(*hero_title);

        last_updated_lbl_ = Gtk::make_managed<Gtk::Label>(format_last_updated());
        last_updated_lbl_->add_css_class("caption");
        last_updated_lbl_->set_halign(Gtk::Align::START);
        hero_info->append(*last_updated_lbl_);
        hero_top->append(*hero_info);

        // Status dot (green = healthy / yellow = needs update)
        status_dot_ = Gtk::make_managed<Gtk::Label>("●");
        status_dot_->set_markup("<span foreground='#34C759'>●</span>");
        status_dot_->set_valign(Gtk::Align::START);
        hero_top->append(*status_dot_);

        // ── Command count summary ─────────────────────────────────────────────
        auto cmds_summary = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        hero->append(*cmds_summary);

        auto active_count = [this]() {
            int n = 0;
            for (auto& c : cfg_.commands) if (c.enabled) ++n;
            return n;
        };

        auto sum_lbl = Gtk::make_managed<Gtk::Label>(
            std::format("{} commands configured, {} enabled",
                        cfg_.commands.size(), active_count()));
        sum_lbl->add_css_class("caption");
        cmds_summary->append(*sum_lbl);

        // ── Update button ─────────────────────────────────────────────────────
        update_btn_ = Gtk::make_managed<Gtk::Button>();
        auto btn_content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        btn_content->set_halign(Gtk::Align::CENTER);
        auto btn_icon = Gtk::make_managed<Gtk::Image>();
        btn_icon->set_from_icon_name("software-update-available-symbolic");
        btn_icon->set_pixel_size(18);
        auto btn_lbl = Gtk::make_managed<Gtk::Label>("Update Now");
        btn_lbl->add_css_class("title-3");
        btn_content->append(*btn_icon);
        btn_content->append(*btn_lbl);
        update_btn_->set_child(*btn_content);
        update_btn_->add_css_class("primary");
        update_btn_->set_margin_top(4);
        update_btn_->signal_clicked().connect([this]() { on_update_clicked(); });
        root->append(*update_btn_);

        // ── Section: Commands ─────────────────────────────────────────────────
        auto cmds_section_hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        auto cmds_lbl = Gtk::make_managed<Gtk::Label>("Update Commands");
        cmds_lbl->add_css_class("title-2");
        cmds_lbl->set_hexpand(true);
        cmds_lbl->set_halign(Gtk::Align::START);
        cmds_section_hdr->append(*cmds_lbl);
        root->append(*cmds_section_hdr);

        commands_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        root->append(*commands_box_);

        // ── Footer ────────────────────────────────────────────────────────────
        auto footer = Gtk::make_managed<Gtk::Label>();
        footer->set_markup("<b>Made with love by buwryme</b>");
        footer->add_css_class("caption");
        footer->set_halign(Gtk::Align::CENTER);
        footer->set_margin_top(8);
        root->append(*footer);
    }

    // ── Rebuild the command list ──────────────────────────────────────────────
    void refresh_command_list() {
        // Remove old rows
        while (auto* child = commands_box_->get_first_child())
            commands_box_->remove(*child);

        for (std::size_t i = 0; i < cfg_.commands.size(); ++i) {
            const auto& cc = cfg_.commands[i];
            commands_box_->append(*make_command_row(cc, i));
        }

        if (cfg_.commands.empty()) {
            auto empty_lbl = Gtk::make_managed<Gtk::Label>(
                "No commands configured. Open Settings to add some.");
            empty_lbl->add_css_class("caption");
            empty_lbl->set_halign(Gtk::Align::CENTER);
            commands_box_->append(*empty_lbl);
        }

        last_updated_lbl_->set_text(format_last_updated());
    }

    // ── Build a single command row card ───────────────────────────────────────
    Gtk::Box* make_command_row(const CommandConfig& cc, std::size_t idx) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
        row->add_css_class("command-row");
        if (!cc.enabled) row->add_css_class("skipped");

        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(cc.icon_name);
        icon->set_pixel_size(24);
        row->append(*icon);

        auto info = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        info->set_hexpand(true);

        auto name_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto name_lbl = Gtk::make_managed<Gtk::Label>(cc.name);
        name_lbl->add_css_class("title-3");
        name_lbl->set_halign(Gtk::Align::START);
        name_box->append(*name_lbl);

        // Index badge
        auto idx_badge = Gtk::make_managed<Gtk::Label>(std::format("#{}", idx + 1));
        idx_badge->add_css_class("stat-badge");
        name_box->append(*idx_badge);
        info->append(*name_box);

        auto desc_lbl = Gtk::make_managed<Gtk::Label>(cc.description);
        desc_lbl->add_css_class("caption");
        desc_lbl->set_halign(Gtk::Align::START);
        desc_lbl->set_wrap(true);
        info->append(*desc_lbl);

        auto cmd_lbl = Gtk::make_managed<Gtk::Label>(cc.command);
        cmd_lbl->add_css_class("caption-mono");
        cmd_lbl->set_halign(Gtk::Align::START);
        cmd_lbl->set_ellipsize(Pango::EllipsizeMode::END);
        info->append(*cmd_lbl);
        row->append(*info);

        // Enabled indicator
        auto status_lbl = Gtk::make_managed<Gtk::Label>(cc.enabled ? "Enabled" : "Disabled");
        status_lbl->add_css_class("status-pill");
        status_lbl->add_css_class(cc.enabled ? "done" : "skipped");
        row->append(*status_lbl);

        return row;
    }

    // ── "Update Now" click ────────────────────────────────────────────────────
    void on_update_clicked() {
        if (cfg_.settings.confirm_before_start) {
            auto* dlg = new PreUpdateDialog(
                *this, cfg_,
                [this](PreUpdateChoice choice) {
                    if (!choice.confirmed) return;
                    launch_update(choice);
                });
            dlg->signal_hide().connect([dlg]() {
                Glib::signal_idle().connect_once([dlg]() { delete dlg; });
            });
            dlg->present();
        } else {
            // Start with all defaults enabled
            PreUpdateChoice choice;
            choice.confirmed    = true;
            choice.reboot_after = false;
            for (auto& cc : cfg_.commands)
                choice.command_enabled.push_back(cc.enabled);
            choice.skip_packages.resize(cfg_.commands.size());
            launch_update(choice);
        }
    }

    void launch_update(const PreUpdateChoice& choice) {
        update_btn_->set_sensitive(false);

        auto* view = new UpdateView(*this, cfg_, choice);
        view->signal_hide().connect([this, view]() {
            update_btn_->set_sensitive(true);
            cfg_ = Config::load();
            refresh_command_list();
            // Schedule deletion to avoid deleting from within signal handler
            Glib::signal_idle().connect_once([view]() { delete view; });
        });
        view->present();
    }

    // ── Settings ──────────────────────────────────────────────────────────────
    void open_settings() {
        auto* w = new SettingsWindow(
            *this, cfg_,
            [this](Config new_cfg) {
                cfg_ = std::move(new_cfg);
                refresh_command_list();
            });
        w->signal_hide().connect([w]() {
            Glib::signal_idle().connect_once([w]() { delete w; });
        });
        w->present();
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    [[nodiscard]] std::string format_last_updated() const {
        if (cfg_.last_updated.empty())
            return "Never updated";
        return "Last updated: " + cfg_.last_updated;
    }
};

} // namespace Progressions::UI
