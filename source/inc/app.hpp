#pragma once
#include "ui/main_window.hpp"
#include "command_runner.hpp"
#include "config.hpp"

#include <gtkmm.h>
#include <giomm.h>
#include <libnotify/notify.h>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <format>
#include <chrono>
#include <ctime>
#include <optional>
#include <unistd.h>

namespace Progressions {

// ─────────────────────────────────────────────────────────────────────────────
//  App
//
//  Single-instance design
//  ─────────────────────
//  GApplication with HANDLES_COMMAND_LINE (no NON_UNIQUE) ensures only one
//  process runs.  A second launch talks to the primary over D-Bus; the primary's
//  on_command_line() fires and the secondary exits cleanly.
//
//  Background / lifecycle
//  ──────────────────────
//  hold() is called once in on_startup() so the process stays alive even when
//  every window is hidden.  do_quit() calls release() + quit() for a clean exit.
//
//  Tray (StatusNotifierItem)
//  ─────────────────────────
//  A 1×1 off-screen anchor window is kept permanently realised so that
//  Gtk::PopoverMenu always has a valid parent widget to map onto.
//    • Left-click  → Activate      → show main window
//    • Right-click → ContextMenu   → GtkPopover with action buttons:
//                                    1. Update Right Now
//                                    2. Change Update Frequency (sub-popover)
//                                    3. Open Interface
//                                    4. Quit
//
//  Headless / systemd
//  ──────────────────
//  --headless runs updates asynchronously via CommandRunner on the GLib main
//  loop.  Notifications are sent via libnotify.  Systemd *user* units are
//  written to ~/.config/systemd/user/ — no pkexec needed.
// ─────────────────────────────────────────────────────────────────────────────
class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> instance() {
        static auto app =
            Glib::make_refptr_for_instance<App>(new App());
        return app;
    }

    ~App() override {
        unregister_sni();
        notify_uninit();
    }

    // Called by SettingsWindow after the user saves automation settings
    void apply_systemd_settings(bool enabled, const std::string& interval) {
        if (enabled) install_systemd_units(interval);
        else         remove_systemd_units();
    }

protected:
    App()
    : Gtk::Application("com.progressions.updater",
                        Gio::Application::Flags::HANDLES_COMMAND_LINE)
    {
        add_main_option_entry(
            Gio::Application::OptionType::BOOL,
            "headless", 'H',
            "Run scheduled updates without opening a window");
    }

    // ── on_startup ── fires once on the PRIMARY instance only ─────────────────
    void on_startup() override {
        Gtk::Application::on_startup();
        notify_init("Progressions");

        // Wire up named GActions
        auto make_action = [&](const char* name, auto slot) {
            auto a = Gio::SimpleAction::create(name);
            a->signal_activate().connect([slot](const Glib::VariantBase&){ slot(); });
            add_action(a);
        };
        make_action("open",   [this]{ show_main_window(); });
        make_action("update", [this]{ start_headless_updates(); });
        make_action("quit",   [this]{ do_quit(); });

        // Keep process alive even with all windows hidden
        hold();

        setup_tray_anchor();
        setup_sni();
    }

    // ── on_command_line ── called for EVERY launch (primary or remote) ─────────
    int on_command_line(
        const Glib::RefPtr<Gio::ApplicationCommandLine>& cmd) override
    {
        auto opts     = cmd->get_options_dict();
        bool headless = (opts && opts->contains("headless"));

        cmd->set_exit_status(0);

        if (headless) {
            start_headless_updates();
        } else {
            show_main_window();

            // First-run: offer to enable systemd timers (non-blocking dialog)
            auto cfg = Config::load();
            if (!cfg.settings.systemd_configured)
                show_setup_prompt();
        }
        return 0;
    }

    void on_activate() override {
        show_main_window();
    }

private:
    // ────────────────────────────────────── state ─────────────────────────────
    UI::MainWindow*                     win_        = nullptr;
    Gtk::Window*                        anchor_win_ = nullptr;
    Glib::RefPtr<Gio::DBus::Connection> bus_;
    guint                               bus_own_id_ = 0;
    guint                               sni_reg_id_ = 0;

    // These MUST be members — GDBus holds raw pointers into both of them.
    // Stack-allocating either one inside register_sni_impl() causes SIGSEGV
    // when the watcher or tray host later calls through the vtable or reads
    // the interface info, because both will have already been destroyed.
    Glib::RefPtr<Gio::DBus::NodeInfo>         sni_node_;
    std::optional<Gio::DBus::InterfaceVTable> sni_vtable_;

    // ── Windows ───────────────────────────────────────────────────────────────
    void ensure_window() {
        if (win_) return;
        load_css();
        win_ = new UI::MainWindow();
        add_window(*win_);

        win_->signal_close_request().connect([this]() -> bool {
            win_->hide();
            return true;
        }, false);
    }

    void show_main_window() {
        ensure_window();
        win_->present();
    }

    void do_quit() {
        if (win_) win_->hide();
        release();
        quit();
    }

    // ── Tray anchor ───────────────────────────────────────────────────────────
    void setup_tray_anchor() {
        anchor_win_ = new Gtk::Window();
        anchor_win_->set_default_size(1, 1);
        anchor_win_->set_decorated(false);
        anchor_win_->set_opacity(0.0);
        anchor_win_->set_resizable(false);
        anchor_win_->set_title("ProgressionsTrayAnchor");
        add_window(*anchor_win_);

        anchor_win_->signal_close_request().connect(
            []() -> bool { return true; }, false);
    }

    // ── Tray context menu ─────────────────────────────────────────────────────
    void show_tray_menu() {
        anchor_win_->present();

        auto* pop = Gtk::make_managed<Gtk::Popover>();
        pop->set_parent(*anchor_win_);
        pop->set_has_arrow(false);

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        box->set_margin_top(6);
        box->set_margin_bottom(6);
        box->set_margin_start(4);
        box->set_margin_end(4);
        pop->set_child(*box);

        auto make_btn = [&](const Glib::ustring& label) -> Gtk::Button* {
            auto* btn = Gtk::make_managed<Gtk::Button>(label);
            btn->add_css_class("flat");
            btn->set_halign(Gtk::Align::FILL);
            btn->set_hexpand(true);
            return btn;
        };

        // Button 1 — Update Right Now
        auto* btn_update = make_btn("↻  Update Right Now");
        btn_update->signal_clicked().connect([this, pop]() {
            pop->popdown();
            anchor_win_->hide();
            start_headless_updates();
        });
        box->append(*btn_update);

        // Button 2 — Change Update Frequency (lazy sub-popover)
        auto* btn_freq = make_btn("⏱  Change Update Frequency  ▶");
        box->append(*btn_freq);

        auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
        sep->set_margin_top(4);
        sep->set_margin_bottom(4);
        box->append(*sep);

        // Button 3 — Open Interface
        auto* btn_open = make_btn("⊞  Open Interface");
        btn_open->signal_clicked().connect([this, pop]() {
            pop->popdown();
            anchor_win_->hide();
            show_main_window();
        });
        box->append(*btn_open);

        // Button 4 — Quit
        auto* btn_quit = make_btn("✕  Quit");
        btn_quit->signal_clicked().connect([this, pop]() {
            pop->popdown();
            do_quit();
        });
        box->append(*btn_quit);

        // Frequency sub-popover — built lazily when the button is clicked
        btn_freq->signal_clicked().connect([this, pop, btn_freq]() {
            auto* sub = Gtk::make_managed<Gtk::Popover>();
            sub->set_parent(*btn_freq);
            sub->set_has_arrow(true);

            auto* sbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
            sbox->set_margin_top(6);
            sbox->set_margin_bottom(6);
            sbox->set_margin_start(4);
            sbox->set_margin_end(4);
            sub->set_child(*sbox);

            auto add_freq = [&](const Glib::ustring& label, const std::string& iv) {
                auto* b = Gtk::make_managed<Gtk::Button>(label);
                b->add_css_class("flat");
                b->set_halign(Gtk::Align::FILL);
                b->set_hexpand(true);
                b->signal_clicked().connect([this, pop, sub, iv]() {
                    sub->popdown();
                    pop->popdown();
                    anchor_win_->hide();
                    set_update_frequency(iv);
                });
                sbox->append(*b);
            };

            add_freq("Daily",   "daily");
            add_freq("Weekly",  "weekly");
            add_freq("Monthly", "monthly");

            sub->popup();
        });

        pop->popup();
        pop->signal_closed().connect([this]() { anchor_win_->hide(); });
    }

    void set_update_frequency(const std::string& interval) {
        auto cfg = Config::load();
        cfg.settings.update_interval    = interval;
        cfg.settings.systemd_configured = true;
        cfg.save();
        install_systemd_units(interval);
    }

    // ── StatusNotifierItem D-Bus interface ────────────────────────────────────
    static constexpr const char* SNI_XML = R"XML(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <method name="Activate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="ContextMenu">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="Scroll">
      <arg name="delta"       type="i" direction="in"/>
      <arg name="orientation" type="s" direction="in"/>
    </method>
    <property name="Category"   type="s" access="read"/>
    <property name="Id"         type="s" access="read"/>
    <property name="Title"      type="s" access="read"/>
    <property name="Status"     type="s" access="read"/>
    <property name="WindowId"   type="i" access="read"/>
    <property name="IconName"   type="s" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu"       type="o" access="read"/>
    <signal name="NewIcon"/>
    <signal name="NewStatus">
      <arg name="status" type="s"/>
    </signal>
  </interface>
</node>
)XML";

    void setup_sni() {
        bus_own_id_ = Gio::DBus::own_name(
            Gio::DBus::BusType::SESSION,
            "org.kde.StatusNotifierItem-progressions",
            sigc::mem_fun(*this, &App::on_bus_acquired),
            sigc::mem_fun(*this, &App::on_name_acquired),
            sigc::mem_fun(*this, &App::on_name_lost)
        );
    }

    void on_bus_acquired(const Glib::RefPtr<Gio::DBus::Connection>& conn,
                         const Glib::ustring&) {
        bus_ = conn;
        register_sni_impl();
    }

    void on_name_acquired(const Glib::RefPtr<Gio::DBus::Connection>&,
                          const Glib::ustring& name) {
        if (!bus_) return;
        try {
            bus_->call(
                "/StatusNotifierWatcher",
                "org.kde.StatusNotifierWatcher",
                "RegisterStatusNotifierItem",
                Glib::VariantContainerBase::create_tuple(
                    Glib::Variant<Glib::ustring>::create(name)),
                [](const Glib::RefPtr<Gio::AsyncResult>&) {},
                "org.kde.StatusNotifierWatcher",
                -1);
        } catch (...) {
            // No SNI watcher running (e.g. plain GNOME without extension).
            // The app continues normally; the tray icon just won't appear.
        }
    }

    void on_name_lost(const Glib::RefPtr<Gio::DBus::Connection>&,
                      const Glib::ustring&) {}

    void register_sni_impl() {
        if (!bus_) return;

        try {
            // Parse XML and keep the NodeInfo alive as a member.
            // lookup_interface() returns a raw pointer INTO the NodeInfo —
            // if NodeInfo were stack-local it would dangle immediately.
            sni_node_ = Gio::DBus::NodeInfo::create_for_xml(SNI_XML);
            auto iface = sni_node_->lookup_interface("org.kde.StatusNotifierItem");
            if (!iface) {
                std::cerr << "SNI: interface not found in XML\n";
                return;
            }

            // Emplace the vtable into the member optional.
            // A stack-local InterfaceVTable would be destroyed here and GDBus
            // would be left holding a dangling pointer → SIGSEGV on any
            // subsequent method call or property read from the tray host.
            sni_vtable_.emplace(
                sigc::mem_fun(*this, &App::on_sni_method_call),
                sigc::mem_fun(*this, &App::on_sni_get_property)
            );

            sni_reg_id_ = bus_->register_object(
                "/StatusNotifierItem", iface, *sni_vtable_);

        } catch (const std::exception& e) {
            std::cerr << "SNI registration failed: " << e.what() << "\n";
        }
    }

    void on_sni_method_call(
        const Glib::RefPtr<Gio::DBus::Connection>&,
        const Glib::ustring& /*sender*/,
        const Glib::ustring& /*object_path*/,
        const Glib::ustring& /*interface_name*/,
        const Glib::ustring& method_name,
        const Glib::VariantContainerBase& /*parameters*/,
        const Glib::RefPtr<Gio::DBus::MethodInvocation>& invocation)
    {
        if (method_name == "Activate") {
            show_main_window();
        } else if (method_name == "ContextMenu" ||
                   method_name == "SecondaryActivate") {
            show_tray_menu();
        }
        invocation->return_value({});
    }

    void on_sni_get_property(
        Glib::VariantBase& property,
        const Glib::RefPtr<Gio::DBus::Connection>&,
        const Glib::ustring& /*sender*/,
        const Glib::ustring& /*object_path*/,
        const Glib::ustring& /*interface_name*/,
        const Glib::ustring& prop_name)
    {
        if      (prop_name == "Category")
            property = Glib::Variant<Glib::ustring>::create("ApplicationStatus");
        else if (prop_name == "Id")
            property = Glib::Variant<Glib::ustring>::create("progressions");
        else if (prop_name == "Title")
            property = Glib::Variant<Glib::ustring>::create("Progressions");
        else if (prop_name == "Status")
            property = Glib::Variant<Glib::ustring>::create("Active");
        else if (prop_name == "WindowId")
            property = Glib::Variant<gint32>::create(0);
        else if (prop_name == "IconName")
            property = Glib::Variant<Glib::ustring>::create("system-software-update");
        else if (prop_name == "ItemIsMenu")
            property = Glib::Variant<bool>::create(false);
        else if (prop_name == "Menu")
            property = Glib::Variant<Glib::DBusObjectPathString>::create("/NO_DBUSMENU");
    }

    void unregister_sni() {
        if (bus_ && sni_reg_id_) {
            bus_->unregister_object(sni_reg_id_);
            sni_reg_id_ = 0;
        }
        if (bus_own_id_) {
            Gio::DBus::unown_name(bus_own_id_);
            bus_own_id_ = 0;
        }
    }

    // ── Headless background updates ────────────────────────────────────────────
    void start_headless_updates() {
        auto cfg = Config::load();

        std::vector<std::pair<std::string, std::string>> cmds;
        for (const auto& cmd : cfg.commands)
            if (cmd.enabled)
                cmds.push_back({cmd.name, cmd.command});

        if (cmds.empty()) {
            notify("Progressions", "No commands enabled.", "dialog-information-symbolic");
            return;
        }

        hold();
        run_commands_async(cmds, 0);
    }

    void run_commands_async(
        const std::vector<std::pair<std::string, std::string>>& cmds,
        size_t idx)
    {
        if (idx >= cmds.size()) {
            save_last_updated();
            notify("Progressions", "Updates completed.", "emblem-ok-symbolic");
            release();
            return;
        }

        const auto& [name, cmd] = cmds[idx];
        auto runner = new CommandRunner();

        runner->signal_finished.connect([this, name, runner, idx, &cmds](int exit_code) {
            if (exit_code == 0)
                notify("Progressions", std::format("✓ {}", name), "emblem-ok-symbolic");
            else
                notify("Progressions", std::format("✗ {} (exit {})", name, exit_code), "dialog-error-symbolic");
            delete runner;
            run_commands_async(cmds, idx + 1);
        });

        runner->signal_error.connect([this, name, runner, idx, &cmds](const std::string& err) {
            notify("Progressions", std::format("✗ {} - {}", name, err), "dialog-error-symbolic");
            delete runner;
            run_commands_async(cmds, idx + 1);
        });

        if (!runner->run(cmd)) {
            notify("Progressions", std::format("Could not spawn: {}", name), "dialog-error-symbolic");
            delete runner;
            run_commands_async(cmds, idx + 1);
        }
    }

    // ── Systemd user units ────────────────────────────────────────────────────
    std::string get_executable_path() {
        char exe[PATH_MAX] = {};
        ssize_t len = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (len > 0) { exe[len] = '\0'; return std::string(exe); }

        const char* path_env = std::getenv("PATH");
        if (!path_env) return "progressions";

        std::string path_str(path_env);
        size_t pos = 0;
        while ((pos = path_str.find(':', pos)) != std::string::npos) {
            path_str[pos] = '\0';
            std::string candidate = std::string(path_str.c_str()) + "/progressions";
            if (std::filesystem::exists(candidate)) return candidate;
            path_str[pos] = ':';
            pos++;
        }
        return "progressions";
    }

    void install_systemd_units(const std::string& interval) {
        const char* home = std::getenv("HOME");
        if (!home) {
            notify("Progressions", "Could not find home directory.", "dialog-error-symbolic");
            return;
        }

        std::filesystem::path udir =
            std::filesystem::path(home) / ".config" / "systemd" / "user";

        try { std::filesystem::create_directories(udir); }
        catch (const std::exception& e) {
            notify("Progressions",
                   std::format("Failed to create systemd directory: {}", e.what()),
                   "dialog-error-symbolic");
            return;
        }

        std::string exe_path = get_executable_path();
        const std::string cal =
            (interval == "weekly")  ? "weekly"  :
            (interval == "monthly") ? "monthly" : "daily";

        try {
            std::ofstream svc(udir / "progressions.service");
            svc << "[Unit]\n"
                << "Description=Progressions Background System Update\n"
                << "After=network-online.target\n"
                << "Wants=network-online.target\n\n"
                << "[Service]\n"
                << "Type=oneshot\n"
                << "ExecStart=" << exe_path << " --headless\n"
                << "KillMode=process\n"
                << "TimeoutStopSec=600\n"
                << "Environment=\"DBUS_SESSION_BUS_ADDRESS=unix:path=%t/bus\"\n\n"
                << "[Install]\n"
                << "WantedBy=default.target\n";
        } catch (const std::exception& e) {
            notify("Progressions",
                   std::format("Failed to write service file: {}", e.what()),
                   "dialog-error-symbolic");
            return;
        }

        try {
            std::ofstream tmr(udir / "progressions.timer");
            tmr << "[Unit]\n"
                << "Description=Progressions " << interval << " update timer\n\n"
                << "[Timer]\n"
                << "OnCalendar=" << cal << "\n"
                << "Persistent=true\n"
                << "RandomizedDelaySec=15m\n\n"
                << "[Install]\n"
                << "WantedBy=timers.target\n";
        } catch (const std::exception& e) {
            notify("Progressions",
                   std::format("Failed to write timer file: {}", e.what()),
                   "dialog-error-symbolic");
            return;
        }

        if (std::system("systemctl --user daemon-reload 2>/dev/null") != 0) {
            notify("Progressions", "Failed to reload systemd daemon.", "dialog-error-symbolic");
            return;
        }
        if (std::system("systemctl --user enable --now progressions.timer 2>/dev/null") != 0) {
            notify("Progressions", "Failed to enable progressions.timer.", "dialog-error-symbolic");
            return;
        }

        notify("Progressions",
               std::format("Automatic {} updates enabled.", interval),
               "emblem-ok-symbolic");
    }

    void remove_systemd_units() {
        (void)std::system("systemctl --user disable --now progressions.timer 2>/dev/null");

        const char* home = std::getenv("HOME");
        if (!home) return;

        std::filesystem::path udir =
            std::filesystem::path(home) / ".config" / "systemd" / "user";

        try {
            std::filesystem::remove(udir / "progressions.service");
            std::filesystem::remove(udir / "progressions.timer");
        } catch (...) {}

        (void)std::system("systemctl --user daemon-reload 2>/dev/null");
        notify("Progressions", "Automatic updates disabled.", "emblem-ok-symbolic");
    }

    // ── First-run onboarding ──────────────────────────────────────────────────
    void show_setup_prompt() {
        auto dlg = Gtk::AlertDialog::create("Enable background update automation?");
        dlg->set_detail(
            "Progressions can automatically update your system using a "
            "systemd user timer — no root needed. "
            "You can change the schedule in Settings any time.");
        dlg->set_buttons({"Enable Daily Updates", "Not Now"});
        dlg->set_default_button(0);
        dlg->set_cancel_button(1);

        dlg->choose(*win_,
            [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
                try {
                    bool accepted = (dlg->choose_finish(res) == 0);
                    auto cfg = Config::load();
                    cfg.settings.systemd_configured = true;
                    if (accepted) {
                        cfg.settings.update_interval = "daily";
                        cfg.save();
                        install_systemd_units("daily");
                    } else {
                        cfg.save();
                    }
                } catch (...) {}
            });
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    void notify(const std::string& title, const std::string& body,
                const std::string& icon) {
        NotifyNotification* n = notify_notification_new(
            title.c_str(), body.c_str(), icon.c_str());
        notify_notification_show(n, nullptr);
        g_object_unref(n);
    }

    void save_last_updated() {
        auto cfg = Config::load();
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&tt));
        cfg.last_updated = buf;
        cfg.save();
    }

    void load_css() {
        auto display = Gdk::Display::get_default();
        if (!display) return;

        std::string upath = Glib::get_user_config_dir() + "/gtk-4.0/gtk.css";
        if (std::filesystem::exists(upath)) {
            try {
                auto p2 = Gtk::CssProvider::create();
                p2->load_from_path(upath);
                Gtk::StyleContext::add_provider_for_display(
                    display, p2, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
            } catch (const std::exception& e) {
                std::cerr << "Warning: Failed to load user CSS: " << e.what() << "\n";
            }
        }
    }
};

} // namespace Progressions
