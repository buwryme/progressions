#pragma once
#include "../config.hpp"
#include <gtkmm.h>
#include <functional>
#include <vector>
#include <sstream>
#include <string>

namespace Progressions::UI {

// ── Window for editing commands and application settings ─────────────────────
class SettingsWindow : public Gtk::Window {
public:
    using SavedCallback = std::function<void(Config)>;

    explicit SettingsWindow(Gtk::Window& parent, Config cfg, SavedCallback cb)
        : cfg_(std::move(cfg)), saved_cb_(std::move(cb))
    {
        set_transient_for(parent);
        set_modal(true);
        set_resizable(true);
        set_default_size(620, 680);
        set_title("Progressions — Settings");

        // ── Header bar ────────────────────────────────────────────────────────
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

        // ── Root ─────────────────────────────────────────────────────────────
        auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        set_child(*root);

        // ── Scrollable area ───────────────────────────────────────────────────
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

        auto cmd_desc = Gtk::make_managed<Gtk::Label>(
            "Commands run in order. Drag to reorder. Each command may optionally "
            "require polkit (pkexec) for elevated privileges.");
        cmd_desc->add_css_class("caption");
        cmd_desc->set_halign(Gtk::Align::START);
        cmd_desc->set_wrap(true);
        content->append(*cmd_desc);

        cmds_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        content->append(*cmds_box_);

        // Populate existing command rows
        for (std::size_t i = 0; i < cfg_.commands.size(); ++i)
            append_command_row(cfg_.commands[i]);

        // ── Section: App Settings ─────────────────────────────────────────────
        auto sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
        content->append(*sep);

        auto app_hdr = Gtk::make_managed<Gtk::Label>("Application Settings");
        app_hdr->add_css_class("title-2");
        app_hdr->set_halign(Gtk::Align::START);
        content->append(*app_hdr);

        auto settings_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
        settings_card->add_css_class("card");
        content->append(*settings_card);

        // ── Reboot delay ──────────────────────────────────────────────────────
        auto delay_row = make_setting_row("Reboot Countdown (seconds)",
                                          "Seconds to wait before auto-rebooting.");
        auto delay_spin = Gtk::make_managed<Gtk::SpinButton>();
        delay_spin->set_range(1, 60);
        delay_spin->set_increments(1, 5);
        delay_spin->set_value(cfg_.settings.reboot_delay_seconds);
        delay_spin->set_valign(Gtk::Align::CENTER);
        delay_row->append(*delay_spin);
        settings_card->append(*delay_row);

        settings_card->append(*Gtk::make_managed<Gtk::Separator>(
            Gtk::Orientation::HORIZONTAL));

        // ── Confirm before start ──────────────────────────────────────────────
        auto confirm_row = make_setting_row("Confirm Before Starting",
                                            "Show a dialog to configure the update before it runs.");
        auto confirm_sw = Gtk::make_managed<Gtk::Switch>();
        confirm_sw->set_active(cfg_.settings.confirm_before_start);
        confirm_sw->set_valign(Gtk::Align::CENTER);
        confirm_row->append(*confirm_sw);
        settings_card->append(*confirm_row);

        settings_card->append(*Gtk::make_managed<Gtk::Separator>(
            Gtk::Orientation::HORIZONTAL));

        // ── Auto scroll output ────────────────────────────────────────────────
        auto scroll_row = make_setting_row("Auto-scroll Output",
                                           "Keep the output terminal scrolled to the latest line.");
        auto scroll_sw = Gtk::make_managed<Gtk::Switch>();
        scroll_sw->set_active(cfg_.settings.scroll_to_bottom);
        scroll_sw->set_valign(Gtk::Align::CENTER);
        scroll_row->append(*scroll_sw);
        settings_card->append(*scroll_row);

        // ── Bottom buttons ────────────────────────────────────────────────────
        auto btn_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        btn_bar->set_margin(20);
        btn_bar->set_margin_top(12);
        btn_bar->set_halign(Gtk::Align::END);

        auto cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
        cancel_btn->add_css_class("secondary");
        cancel_btn->signal_clicked().connect([this]() { close(); });
        btn_bar->append(*cancel_btn);

        auto reset_btn = Gtk::make_managed<Gtk::Button>("Reset to Defaults");
        reset_btn->add_css_class("secondary");
        reset_btn->signal_clicked().connect([this]() {
            cfg_ = Config::defaults();
            // Refresh command rows
            while (auto* ch = cmds_box_->get_first_child())
                cmds_box_->remove(*ch);
            row_widgets_.clear();
            for (auto& c : cfg_.commands) append_command_row(c);
        });
        btn_bar->append(*reset_btn);

        auto save_btn = Gtk::make_managed<Gtk::Button>();
        auto save_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto save_icon = Gtk::make_managed<Gtk::Image>();
        save_icon->set_from_icon_name("document-save-symbolic");
        save_box->append(*save_icon);
        save_box->append(*Gtk::make_managed<Gtk::Label>("Save Settings"));
        save_btn->set_child(*save_box);
        save_btn->add_css_class("primary");
        save_btn->signal_clicked().connect([this, delay_spin, confirm_sw, scroll_sw]() {
            collect_settings(delay_spin, confirm_sw, scroll_sw);
            cfg_.save();
            saved_cb_(cfg_);
            close();
        });
        btn_bar->append(*save_btn);

        root->append(*btn_bar);
    }

private:
    Config                    cfg_;
    SavedCallback             saved_cb_;
    Gtk::Box*                 cmds_box_ = nullptr;

    // Per-row widgets for reading back values
    struct CmdRowWidgets {
        Gtk::Entry* name_entry    = nullptr;
        Gtk::Entry* cmd_entry     = nullptr;
        Gtk::Entry* icon_entry    = nullptr;
        Gtk::Entry* desc_entry    = nullptr;
        Gtk::Switch* enabled_sw   = nullptr;
        Gtk::Switch* skip_sw      = nullptr;
        std::string  id;
    };
    std::vector<CmdRowWidgets> row_widgets_;

    static Gtk::Box* make_setting_row(const std::string& title,
                                      const std::string& subtitle) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        row->set_valign(Gtk::Align::CENTER);

        auto info = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
        info->set_hexpand(true);
        auto t = Gtk::make_managed<Gtk::Label>(title);
        t->add_css_class("body");
        t->set_halign(Gtk::Align::START);
        auto s = Gtk::make_managed<Gtk::Label>(subtitle);
        s->add_css_class("caption");
        s->set_halign(Gtk::Align::START);
        s->set_wrap(true);
        info->append(*t);
        info->append(*s);
        row->append(*info);
        return row;
    }

    void append_command_row(const CommandConfig& cc) {
        auto card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        card->add_css_class("card");
        cmds_box_->append(*card);

        // ── Row header ────────────────────────────────────────────────────────
        auto header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        header->set_valign(Gtk::Align::CENTER);
        card->append(*header);

        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(cc.icon_name);
        icon->set_pixel_size(20);
        header->append(*icon);

        auto name_entry = Gtk::make_managed<Gtk::Entry>();
        name_entry->set_text(cc.name);
        name_entry->set_hexpand(true);
        name_entry->set_placeholder_text("Command name");
        header->append(*name_entry);

        // Enabled switch
        auto en_sw = Gtk::make_managed<Gtk::Switch>();
        en_sw->set_active(cc.enabled);
        en_sw->set_valign(Gtk::Align::CENTER);
        header->append(*en_sw);

        // Delete button
        auto del_btn = Gtk::make_managed<Gtk::Button>();
        auto del_icon = Gtk::make_managed<Gtk::Image>();
        del_icon->set_from_icon_name("list-remove-symbolic");
        del_btn->set_child(*del_icon);
        del_btn->add_css_class("flat-icon");
        del_btn->set_tooltip_text("Remove command");
        del_btn->signal_clicked().connect([this, card]() {
            // Find and remove the row from cmds_box_
            cmds_box_->remove(*card);
            // Remove from row_widgets_
            // (we'll re-collect on save anyway)
        });
        header->append(*del_btn);

        // ── Command entry ─────────────────────────────────────────────────────
        auto cmd_grid = Gtk::make_managed<Gtk::Grid>();
        cmd_grid->set_row_spacing(6);
        cmd_grid->set_column_spacing(12);
        card->append(*cmd_grid);

        auto attach_labeled = [&](const std::string& lbl, Gtk::Widget& w, int row) {
            auto l = Gtk::make_managed<Gtk::Label>(lbl);
            l->add_css_class("caption");
            l->set_halign(Gtk::Align::END);
            l->set_valign(Gtk::Align::CENTER);
            cmd_grid->attach(*l, 0, row, 1, 1);
            cmd_grid->attach(w, 1, row, 1, 1);
        };

        auto cmd_entry = Gtk::make_managed<Gtk::Entry>();
        cmd_entry->set_text(cc.command);
        cmd_entry->set_hexpand(true);
        cmd_entry->add_css_class("caption-mono");
        attach_labeled("Command", *cmd_entry, 0);

        auto icon_entry = Gtk::make_managed<Gtk::Entry>();
        icon_entry->set_text(cc.icon_name);
        icon_entry->set_hexpand(true);
        attach_labeled("Icon name", *icon_entry, 1);

        auto desc_entry = Gtk::make_managed<Gtk::Entry>();
        desc_entry->set_text(cc.description);
        desc_entry->set_hexpand(true);
        attach_labeled("Description", *desc_entry, 2);

        // ── Supports skip switch ──────────────────────────────────────────────
        auto skip_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        auto skip_lbl = Gtk::make_managed<Gtk::Label>("Supports package skip");
        skip_lbl->add_css_class("caption");
        skip_lbl->set_hexpand(true);
        auto skip_sw = Gtk::make_managed<Gtk::Switch>();
        skip_sw->set_active(cc.supports_skip);
        skip_row->append(*skip_lbl);
        skip_row->append(*skip_sw);
        card->append(*skip_row);

        row_widgets_.push_back({name_entry, cmd_entry, icon_entry, desc_entry,
                                en_sw, skip_sw, cc.id});
    }

    void add_command_row() {
        CommandConfig blank;
        blank.id          = "custom_" + std::to_string(row_widgets_.size());
        blank.name        = "New Command";
        blank.command     = "echo hello";
        blank.icon_name   = "system-run-symbolic";
        blank.description = "";
        blank.enabled     = true;
        blank.supports_skip = false;
        append_command_row(blank);
    }

    void collect_settings(Gtk::SpinButton* delay_spin,
                          Gtk::Switch*     confirm_sw,
                          Gtk::Switch*     scroll_sw)
    {
        cfg_.settings.reboot_delay_seconds  = int(delay_spin->get_value());
        cfg_.settings.confirm_before_start  = confirm_sw->get_active();
        cfg_.settings.scroll_to_bottom      = scroll_sw->get_active();

        cfg_.commands.clear();
        for (auto& rw : row_widgets_) {
            // Only collect rows still present in cmds_box_
            CommandConfig cc;
            cc.id             = rw.id;
            cc.name           = rw.name_entry->get_text();
            cc.command        = rw.cmd_entry->get_text();
            cc.icon_name      = rw.icon_entry->get_text();
            cc.description    = rw.desc_entry->get_text();
            cc.enabled        = rw.enabled_sw->get_active();
            cc.supports_skip  = rw.skip_sw->get_active();
            if (!cc.name.empty() && !cc.command.empty())
                cfg_.commands.push_back(cc);
        }
    }
};

} // namespace Progressions::UI
