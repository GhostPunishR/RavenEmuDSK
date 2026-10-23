// SPDX-License-Identifier: AGPL-3.0-only
// Version parsing for the update check; dependency-free and unit tested.
#include <ravenemudsk/updates/updates.hpp>
#include <charconv>
namespace ravenemudsk_ui {
std::optional<Version> parse_version(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
    Version v;
    int* parts[] = {&v.major, &v.minor, &v.patch};
    for (int i = 0; i < 3; ++i) {
        const char* begin = text.data();
        const char* end = text.data() + text.size();
        const auto [next, error] = std::from_chars(begin, end, *parts[i]);
        if (error != std::errc{} || next == begin || *parts[i] < 0 || next - begin > 5) return std::nullopt;
        text.remove_prefix(static_cast<std::size_t>(next - begin));
        if (i < 2) {
            if (text.empty() || text.front() != '.') return std::nullopt;
            text.remove_prefix(1);
        }
    }
    return text.empty() ? std::optional<Version>(v) : std::nullopt;
}
bool is_newer(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major > b.major;
    if (a.minor != b.minor) return a.minor > b.minor;
    return a.patch > b.patch;
}
std::string to_string(const Version& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}
std::optional<std::string> latest_tag(std::string_view json) {
    const auto key = json.find("\"tag_name\"");
    if (key == std::string_view::npos) return std::nullopt;
    auto pos = key + 10;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' ||
                                 json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    const auto end = json.find('"', pos + 1);
    if (end == std::string_view::npos || end - pos - 1 > 32) return std::nullopt;
    return std::string(json.substr(pos + 1, end - pos - 1));
}
std::string release_page(const Version& v) {
    return "https://github.com/GhostPunishR/RavenEmuDSK/releases/tag/v" + to_string(v);
}
} // namespace ravenemudsk_ui
