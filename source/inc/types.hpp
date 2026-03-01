#pragma once
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <chrono>
#include <format>
#include <regex>

namespace Progressions {

// ── Speed / Progress info parsed from command output ─────────────────────────
struct SpeedInfo {
    double bytes_per_sec = 0.0;
    std::string package_name;
    int  current_pkg = 0;
    int  total_pkg   = 0;
    double progress  = -1.0;   // -1 → indeterminate

    [[nodiscard]] std::string format_speed() const {
        if (bytes_per_sec <= 0) return "";
        if (bytes_per_sec < 1024.0)
            return std::format("{:.0f} B/s", bytes_per_sec);
        if (bytes_per_sec < 1024.0 * 1024.0)
            return std::format("{:.1f} KiB/s", bytes_per_sec / 1024.0);
        return std::format("{:.1f} MiB/s", bytes_per_sec / 1'048'576.0);
    }

    [[nodiscard]] std::string format_packages() const {
        if (total_pkg <= 0) return "";
        return std::format("Package {}/{}", current_pkg, total_pkg);
    }
};

// ── Status of a single command in a session ───────────────────────────────────
enum class CommandStatus { Pending, Running, Done, Failed, Skipped };

inline std::string status_label(CommandStatus s) {
    switch (s) {
        case CommandStatus::Pending:  return "Pending";
        case CommandStatus::Running:  return "Running…";
        case CommandStatus::Done:     return "Done";
        case CommandStatus::Failed:   return "Failed";
        case CommandStatus::Skipped:  return "Skipped";
    }
    return "";
}

inline std::string status_icon(CommandStatus s) {
    switch (s) {
        case CommandStatus::Pending:  return "emblem-default-symbolic";
        case CommandStatus::Running:  return "emblem-synchronizing-symbolic";
        case CommandStatus::Done:     return "emblem-ok-symbolic";
        case CommandStatus::Failed:   return "dialog-error-symbolic";
        case CommandStatus::Skipped:  return "action-unavailable-symbolic";
    }
    return "emblem-default-symbolic";
}

} // namespace Progressions
