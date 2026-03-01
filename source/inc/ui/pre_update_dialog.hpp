#pragma once
#include "../config.hpp"
#include <gtkmm.h>
#include <vector>
#include <string>
#include <functional>
#include <sstream>

namespace Progressions::UI {

// ── Result of the pre-update dialog ──────────────────────────────────────────
struct PreUpdateChoice {
    std::vector<bool>                              command_enabled;   // per command
    std::vector<std::vector<std::string>>          skip_packages;     // per command
    bool                                           reboot_after = false;
    bool                                           confirmed    = false;
};

// ── Modal dialog shown before starting an update ─────────────────────────────
class PreUpdateDialog : public Gtk::Window {
public:
    // ── Callback: called with result when dialog closes ───────────────────────
    using DoneCallback = std::function<void(PreUpdateChoice)>;

    explicit PreUpdateDialog(Gtk::Window& parent, const Config& cfg, DoneCallback cb)
        : cfg_(cfg), done_cb_(std::move(cb))
    {
        set_transient_for(parent);
        set_modal(true);
        set_resizable(true);
        set_default_size(480, 600);
        set_title("Configure Update");

        // ── Header bar ────────────────────────────────────────────────────────
        auto hb = Gtk::make_managed<Gtk::HeaderBar>();
        hb->set_show_title_buttons(false);
        auto title_lbl = Gtk::make_managed<Gtk::Label>("Configure Update");
        title_lbl->add_css_class("title-3");
        hb->set_title_widget(*title_lbl);
        set_titlebar(*hb);

        // ── Root ─────────────────────────────────────────────────────────────
        auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        set_child(*root);

        // ── Scrollable content ────────────────────────────────────────────────
        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_vexpand(true);
        root->append(*scroll);

        auto content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 20);
        content->set_margin(20);
        scroll->set_child(*content);

        // ── Section: Commands ─────────────────────────────────────────────────
        auto cmd_header = Gtk::make_managed<Gtk::Label>("Commands to Run");
        cmd_header->add_css_class("title-3");
        cmd_header->set_halign(Gtk::Align::START);
        content->append(*cmd_header);

        auto cmd_sub = Gtk::make_managed<Gtk::Label>(
            "Enable or disable individual steps for this update.");
        cmd_sub->add_css_class("caption");
        cmd_sub->set_halign(Gtk::Align::START);
        cmd_sub->set_wrap(true);
        content->append(*cmd_sub);

        // ── Build command rows ─────────────────────────────────────────────────
        auto cmds_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        content->append(*cmds_box);

        for (std::size_t i = 0; i < cfg_.commands.size(); ++i) {
            const auto& cc = cfg_.commands[i];

            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
            row->add_css_class("card");
            cmds_box->append(*row);

            // ── Top row: icon + name + switch ─────────────────────────────────
            auto top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
            top->set_valign(Gtk::Align::CENTER);
            row->append(*top);

            auto icon = Gtk::make_managed<Gtk::Image>();
            icon->set_from_icon_name(cc.icon_name);
            icon->set_pixel_size(24);
            top->append(*icon);

            auto info_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
            info_box->set_hexpand(true);
            auto name_lbl = Gtk::make_managed<Gtk::Label>(cc.name);
            name_lbl->add_css_class("title-3");
            name_lbl->set_halign(Gtk::Align::START);
            info_box->append(*name_lbl);
            auto desc_lbl = Gtk::make_managed<Gtk::Label>(cc.description);
            desc_lbl->add_css_class("caption");
            desc_lbl->set_halign(Gtk::Align::START);
            desc_lbl->set_wrap(true);
            info_box->append(*desc_lbl);
            top->append(*info_box);

            auto sw = Gtk::make_managed<Gtk::Switch>();
            sw->set_active(cc.enabled);
            sw->set_valign(Gtk::Align::CENTER);
            enabled_switches_.push_back(sw);
            top->append(*sw);

            // ── Command preview ────────────────────────────────────────────────
            auto cmd_preview = Gtk::make_managed<Gtk::Label>(cc.command);
            cmd_preview->add_css_class("caption-mono");
            cmd_preview->set_halign(Gtk::Align::START);
            cmd_preview->set_ellipsize(Pango::EllipsizeMode::END);
            row->append(*cmd_preview);

            // ── "Skip packages" expander (only for yay-type commands) ──────────
            if (cc.supports_skip) {
                auto expander = Gtk::make_managed<Gtk::Expander>("Skip packages (comma-separated)");
                expander->add_css_class("caption");
                row->append(*expander);

                auto skip_entry = Gtk::make_managed<Gtk::Entry>();
                skip_entry->set_placeholder_text("e.g. linux,mesa,vulkan-mesa-layers");
                skip_entry->set_margin(4);
                expander->set_child(*skip_entry);
                skip_entries_.push_back({i, skip_entry});
            } else {
                skip_entries_.push_back({i, nullptr});
            }

            // ── Enable/disable child widgets when switch toggled ───────────────
            sw->property_active().signal_changed().connect([row, cmd_preview, sw]() {
                bool active = sw->get_active();
                cmd_preview->set_sensitive(active);
            });
        }

        // ── Section: Reboot ───────────────────────────────────────────────────
        auto sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
        sep->set_margin_top(8);
        sep->set_margin_bottom(8);
        content->append(*sep);

        auto reboot_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        reboot_card->add_css_class("card");
        content->append(*reboot_card);

        auto reboot_icon = Gtk::make_managed<Gtk::Image>();
        reboot_icon->set_from_icon_name("system-restart-symbolic");
        reboot_icon->set_pixel_size(24);
        reboot_card->append(*reboot_icon);

        auto reboot_info = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
        reboot_info->set_hexpand(true);
        auto reboot_title = Gtk::make_managed<Gtk::Label>("Restart after update");
        reboot_title->add_css_class("title-3");
        reboot_title->set_halign(Gtk::Align::START);
        auto reboot_desc = Gtk::make_managed<Gtk::Label>(
            "Automatically restart when all commands complete.");
        reboot_desc->add_css_class("caption");
        reboot_desc->set_halign(Gtk::Align::START);
        reboot_desc->set_wrap(true);
        reboot_info->append(*reboot_title);
        reboot_info->append(*reboot_desc);
        reboot_card->append(*reboot_info);

        reboot_switch_ = Gtk::make_managed<Gtk::Switch>();
        reboot_switch_->set_valign(Gtk::Align::CENTER);
        reboot_card->append(*reboot_switch_);

        // ── Bottom button row ─────────────────────────────────────────────────
        auto btn_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        btn_bar->set_margin(20);
        btn_bar->set_margin_top(12);
        btn_bar->set_halign(Gtk::Align::END);

        auto cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
        cancel_btn->add_css_class("secondary");
        cancel_btn->signal_clicked().connect([this]() {
            PreUpdateChoice ch;
            ch.confirmed = false;
            done_cb_(ch);
            close();
        });
        btn_bar->append(*cancel_btn);

        auto go_btn = Gtk::make_managed<Gtk::Button>();
        auto go_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto go_icon = Gtk::make_managed<Gtk::Image>();
        go_icon->set_from_icon_name("media-playback-start-symbolic");
        auto go_lbl = Gtk::make_managed<Gtk::Label>("Start Update");
        go_box->append(*go_icon);
        go_box->append(*go_lbl);
        go_btn->set_child(*go_box);
        go_btn->add_css_class("primary");
        go_btn->signal_clicked().connect([this]() { emit_confirm(); });
        btn_bar->append(*go_btn);

        root->append(*btn_bar);
    }

private:
    const Config&   cfg_;
    DoneCallback    done_cb_;

    std::vector<Gtk::Switch*>                             enabled_switches_;
    std::vector<std::pair<std::size_t, Gtk::Entry*>>      skip_entries_;
    Gtk::Switch*                                          reboot_switch_ = nullptr;

    void emit_confirm() {
        PreUpdateChoice ch;
        ch.confirmed    = true;
        ch.reboot_after = reboot_switch_->get_active();

        for (auto* sw : enabled_switches_)
            ch.command_enabled.push_back(sw->get_active());

        ch.skip_packages.resize(cfg_.commands.size());
        for (auto& [idx, entry] : skip_entries_) {
            if (!entry) continue;
            auto raw = entry->get_text();
            if (raw.empty()) continue;
            std::vector<std::string> pkgs;
            std::istringstream ss(raw);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                // trim
                auto b = tok.find_first_not_of(' ');
                auto e = tok.find_last_not_of(' ');
                if (b != std::string::npos)
                    pkgs.push_back(tok.substr(b, e - b + 1));
            }
            ch.skip_packages[idx] = std::move(pkgs);
        }

        done_cb_(ch);
        close();
    }
};

} // namespace Progressions::UI
