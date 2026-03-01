#pragma once
#include "types.hpp"
#include <regex>
#include <string>
#include <optional>

namespace Progressions {

// ── Structured data extracted from a single output line ───────────────────────
struct ParsedLine {
    enum class Kind {
        Plain,            // generic informational line
        SectionHeader,    // ":: Synchronizing …"
        DownloadProgress, // "(N/M) package  1.2 MiB  456 KiB/s"
        InstallProgress,  // "(N/M) installing package"
        OverallProgress,  // ":: Running post-transaction hooks… (N/M)"
        Percentage,       // any "[####] XX%"
        Done,             // ":: Transaction completed"
        Error,            // "error:" lines
    };

    Kind        kind    = Kind::Plain;
    std::string text;             // original/cleaned line
    double      progress = -1.0; // 0–1 when known
    SpeedInfo   speed;            // speed + package info
};

// ── Stateless line parser ─────────────────────────────────────────────────────
class OutputParser {
public:
    [[nodiscard]] ParsedLine parse(const std::string& raw) const {
        ParsedLine p;
        p.text = raw;

        // Blank / trivial
        if (raw.empty()) return p;

        // Section headers from pacman/yay
        if (raw.starts_with(":: ")) {
            p.kind = ParsedLine::Kind::SectionHeader;
            if (raw.find("Transaction completed") != std::string::npos ||
                raw.find("upgrade complete")     != std::string::npos ||
                raw.find("done")                  != std::string::npos)
                p.kind = ParsedLine::Kind::Done;
            return p;
        }

        // Error lines
        if (raw.starts_with("error:") || raw.starts_with("Error:") ||
            raw.find("error:") != std::string::npos) {
            p.kind = ParsedLine::Kind::Error;
            return p;
        }

        // "(N/M) [action] package" — install/upgrade progress
        static const std::regex rx_install{
            R"(\((\d+)/(\d+)\)\s+(installing|upgrading|removing|checking|loading)\s+(.+))"
        };
        if (std::smatch m; std::regex_search(raw, m, rx_install)) {
            p.kind = ParsedLine::Kind::InstallProgress;
            p.speed.current_pkg = std::stoi(m[1]);
            p.speed.total_pkg   = std::stoi(m[2]);
            p.speed.package_name = m[4];
            if (p.speed.total_pkg > 0)
                p.progress = double(p.speed.current_pkg) / double(p.speed.total_pkg);
            return p;
        }

        // Download progress line: " pkgname  1.23 MiB  456 KiB/s  00:01  [###] 75%"
        static const std::regex rx_download{
            R"(\s*(\S+)\s+[\d.]+\s+\w+\s+([\d.]+)\s*(B|KiB|MiB|GiB)/s)"
        };
        if (std::smatch m; std::regex_search(raw, m, rx_download)) {
            p.kind = ParsedLine::Kind::DownloadProgress;
            p.speed.package_name  = m[1];
            double val            = std::stod(m[2]);
            std::string unit      = m[3];
            p.speed.bytes_per_sec = unit_to_bytes(val, unit);
        }

        // Any "XX%" percentage on the line
        static const std::regex rx_pct{R"(\b(\d{1,3})%)"};
        if (std::smatch m; std::regex_search(raw, m, rx_pct)) {
            int pct = std::stoi(m[1]);
            if (pct >= 0 && pct <= 100)
                p.progress = pct / 100.0;
            if (p.kind == ParsedLine::Kind::Plain)
                p.kind = ParsedLine::Kind::Percentage;
        }

        // Flatpak download: "Downloading: org.app.Name"
        static const std::regex rx_flatpak{R"(Downloading:\s+(\S+))"};
        if (std::smatch m; std::regex_search(raw, m, rx_flatpak)) {
            p.kind = ParsedLine::Kind::DownloadProgress;
            p.speed.package_name = m[1];
        }

        // mkinitcpio hooks: "  -> Running build hook: [hookname]"
        static const std::regex rx_hook{R"(Running build hook: \[(.+)\])"};
        if (std::smatch m; std::regex_search(raw, m, rx_hook)) {
            p.kind = ParsedLine::Kind::InstallProgress;
            p.speed.package_name = std::string("Hook: ") + m[1].str();
        }

        return p;
    }

    // ── Classify a line for colour hints ─────────────────────────────────────
    enum class LineColor { Default, Accent, Success, Warning, Error, Dim };

    [[nodiscard]] static LineColor classify_color(const ParsedLine& p) {
        switch (p.kind) {
            case ParsedLine::Kind::SectionHeader:    return LineColor::Accent;
            case ParsedLine::Kind::Done:             return LineColor::Success;
            case ParsedLine::Kind::Error:            return LineColor::Error;
            case ParsedLine::Kind::DownloadProgress: return LineColor::Accent;
            case ParsedLine::Kind::InstallProgress:  return LineColor::Default;
            default: break;
        }
        const auto& t = p.text;
        if (t.starts_with("==>") || t.starts_with("warning:")) return LineColor::Warning;
        if (t.starts_with("  ->"))                              return LineColor::Dim;
        return LineColor::Default;
    }

private:
    static double unit_to_bytes(double val, const std::string& unit) {
        if (unit == "KiB") return val * 1024.0;
        if (unit == "MiB") return val * 1024.0 * 1024.0;
        if (unit == "GiB") return val * 1024.0 * 1024.0 * 1024.0;
        return val; // B
    }
};

} // namespace Progressions
