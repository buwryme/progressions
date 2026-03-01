#pragma once
#include "ui/main_window.hpp"
#include <gtkmm-4.0/gtkmm.h>
#include <gtkmm-4.0/gtkmm/application.h>

namespace Progressions {

// ── Gtk::Application subclass ─────────────────────────────────────────────────
class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> instance() {
        return Glib::RefPtr<App>(new App());
    }

protected:
    App() : Gtk::Application("com.progressions.updater",
                              Gio::Application::Flags::NON_UNIQUE) {}

    void on_startup() override {
        Gtk::Application::on_startup();
        load_css();
    }

    void on_activate() override {
        auto* win = new UI::MainWindow();
        win->signal_hide().connect([win]() { delete win; });
        add_window(*win);
        win->present();
    }

private:
    void load_css() {
        auto provider = Gtk::CssProvider::create();

        // Construct the path: ~/.config/gtk-4.0/gtk.css
        std::string config_path = Glib::get_user_config_dir() + "/gtk-4.0/gtk.css";
        auto file = Gio::File::create_for_path(config_path);

        try {
            if (file->query_exists()) {
                provider->load_from_file(file);

                // Apply to the default display
                Gtk::StyleContext::add_provider_for_display(
                    Gdk::Display::get_default(),
                    provider,
                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            }
        } catch (const Glib::Error& ex) {
            // Silently fail or log if the CSS has syntax errors
            g_warning("Failed to load user CSS: %s", ex.what());
        }
    }
};

} // namespace Progressions
