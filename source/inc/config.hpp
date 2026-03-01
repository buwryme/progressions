#pragma once
#include "types.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace Progressions {

// ── Per-command configuration ─────────────────────────────────────────────────
struct CommandConfig {
    std::string id;
    std::string name;
    std::string command;
    std::string icon_name    = "system-run-symbolic";
    std::string description;
    bool        enabled      = true;
    bool        supports_skip = false;  // shows "Skip packages" UI for this cmd
};

// ── Global application settings ───────────────────────────────────────────────
struct AppSettings {
    int  reboot_delay_seconds = 5;
    bool confirm_before_start = true;
    bool scroll_to_bottom     = true;
};

// ── Root config container ─────────────────────────────────────────────────────
struct Config {
    std::vector<CommandConfig> commands;
    AppSettings                settings;
    std::string                last_updated;

    // ── Path helpers ──────────────────────────────────────────────────────────
    static std::filesystem::path config_path() {
        const char* xdg  = std::getenv("XDG_CONFIG_HOME");
        const char* home = std::getenv("HOME");
        std::filesystem::path base = xdg
            ? std::filesystem::path(xdg)
            : (home ? std::filesystem::path(home) / ".config"
                    : std::filesystem::path("/tmp"));
        return base / "progressions" / "config.toml";
    }

    // ── Hard-coded defaults (first run) ───────────────────────────────────────
    static Config defaults() {
        Config c;
        c.commands = {
            {
                "pkg_upgrade", "Package Upgrade",
                "yay -Syuu --noconfirm",
                "software-update-available-symbolic",
                "Update all packages including AUR",
                true, true
            },
            {
                "rebuild_initramfs", "Rebuild initramfs",
                "pkexec mkinitcpio -P",
                "drive-harddisk-symbolic",
                "Rebuild initramfs for all presets",
                true, false
            },
            {
                "flatpak_upgrade", "Flatpak Upgrade",
                "flatpak upgrade -y",
                "package-x-generic-symbolic",
                "Update all Flatpak applications",
                true, false
            },
            {
                "system_refresh", "System Refresh",
                "bash -c '"
                "pkexec systemd-resolve --flush-caches 2>/dev/null; "
                "rm -rf ~/.cache/yay; "
                "rm -rf ~/.cache/go/build 2>/dev/null; "
                "pkexec bash -c \"sync && echo 3 > /proc/sys/vm/drop_caches\" 2>/dev/null; "
                "echo \"System refresh complete\"'",
                "view-refresh-symbolic",
                "Flush caches and refresh system state",
                true, false
            }
        };
        return c;
    }

    // ── String helpers ────────────────────────────────────────────────────────
private:
    static std::string trim(const std::string& s) {
        auto b = s.find_first_not_of(" \t\r\n");
        auto e = s.find_last_not_of(" \t\r\n");
        return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
    }

    static std::string unquote(const std::string& s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    }

    static std::string escape_toml(const std::string& s) {
        std::string r;
        r.reserve(s.size() + 8);
        for (char c : s) {
            if      (c == '"' ) r += "\\\"";
            else if (c == '\\') r += "\\\\";
            else                r += c;
        }
        return r;
    }

    static void set_cmd_field(CommandConfig& cmd,
                              const std::string& key,
                              const std::string& raw_val)
    {
        auto val  = unquote(trim(raw_val));
        auto bval = (val == "true");
        if      (key == "id")            cmd.id            = val;
        else if (key == "name")          cmd.name          = val;
        else if (key == "command")       cmd.command       = val;
        else if (key == "icon_name")     cmd.icon_name     = val;
        else if (key == "description")   cmd.description   = val;
        else if (key == "enabled")       cmd.enabled       = bval;
        else if (key == "supports_skip") cmd.supports_skip = bval;
    }

    static void set_settings_field(AppSettings& s,
                                   const std::string& key,
                                   const std::string& raw_val)
    {
        auto val  = trim(raw_val);
        if      (key == "reboot_delay")       s.reboot_delay_seconds = std::stoi(val);
        else if (key == "confirm_before_start") s.confirm_before_start = (val == "true");
        else if (key == "scroll_to_bottom")    s.scroll_to_bottom     = (val == "true");
    }

public:
    // ── Load from disk (or return defaults on failure) ────────────────────────
    static Config load() {
        auto path = config_path();
        if (!std::filesystem::exists(path)) return defaults();

        std::ifstream f(path);
        if (!f) return defaults();

        Config       c;
        CommandConfig current;
        bool in_cmd = false, in_settings = false;
        std::string line;

        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line.starts_with('#')) continue;

            if (line == "[[commands]]") {
                if (in_cmd && !current.id.empty()) c.commands.push_back(current);
                current    = {};
                in_cmd     = true;
                in_settings = false;
            } else if (line == "[settings]") {
                if (in_cmd && !current.id.empty()) { c.commands.push_back(current); in_cmd = false; }
                in_settings = true;
            } else if (auto eq = line.find('='); eq != std::string::npos) {
                auto key = trim(line.substr(0, eq));
                auto val = trim(line.substr(eq + 1));
                if      (in_cmd)      set_cmd_field(current, key, val);
                else if (in_settings) set_settings_field(c.settings, key, val);
                else if (key == "last_updated") c.last_updated = unquote(val);
            }
        }
        if (in_cmd && !current.id.empty()) c.commands.push_back(current);

        return c.commands.empty() ? defaults() : c;
    }

    // ── Save to disk ──────────────────────────────────────────────────────────
    void save() const {
        auto path = config_path();
        std::filesystem::create_directories(path.parent_path());

        std::ostringstream ss;
        ss << "# Progressions — System Update Manager\n"
           << "# https://github.com/you/progressions\n\n";

        ss << "last_updated = \"" << escape_toml(last_updated) << "\"\n\n";

        ss << "[settings]\n"
           << "reboot_delay        = " << settings.reboot_delay_seconds      << "\n"
           << "confirm_before_start = " << (settings.confirm_before_start ? "true" : "false") << "\n"
           << "scroll_to_bottom    = " << (settings.scroll_to_bottom ? "true" : "false") << "\n\n";

        for (const auto& cmd : commands) {
            ss << "[[commands]]\n"
               << "id           = \"" << escape_toml(cmd.id)          << "\"\n"
               << "name         = \"" << escape_toml(cmd.name)        << "\"\n"
               << "command      = \"" << escape_toml(cmd.command)     << "\"\n"
               << "icon_name    = \"" << escape_toml(cmd.icon_name)   << "\"\n"
               << "description  = \"" << escape_toml(cmd.description) << "\"\n"
               << "enabled      = "   << (cmd.enabled       ? "true" : "false") << "\n"
               << "supports_skip = "  << (cmd.supports_skip ? "true" : "false") << "\n\n";
        }

        std::ofstream out(path);
        out << ss.str();
    }
};

} // namespace Progressions
