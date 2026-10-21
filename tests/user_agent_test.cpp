// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/user_agent.hpp>
#include <ravenemu/bridge.h>
#include <cstdlib>
#include <iostream>
#include <regex>
namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (ok) return;
    std::cerr << what << "\n";
    ++failures;
}
} // namespace
// RetroAchievements reads "Product/version", the first "(...)" as the system
// (cut at ';') and each later "name/version" as an extra component.
int main() {
    const std::string agent = ravenemudsk_ra::user_agent();
    std::cout << agent << "\n";
    const std::regex shape(R"(^RavenEmu/(\d+\.\d+\.\d+) \(([^()]+)\) RavenCore/(\S+) rcheevos/\d+\.\d+(\.\d+)?$)");
    std::smatch part;
    check(std::regex_match(agent, part, shape), "Product, system, engine and SDK, in this order");
    if (part.size() > 3) {
        check(part[1] == EXPECTED_VERSION, "The product version comes from project(VERSION)");
        check(part[2] == ravenemudsk_ra::system_description(), "The system part");
        check(part[3] == ravenemu_version(), "The engine version is the linked RavenCore's");
    }
    for (const unsigned char c : agent) check(c >= 0x20 && c < 0x7f, "A header carries printable ASCII only");
    check(ravenemudsk_ra::system_description().find_first_of("()") == std::string::npos, "No parenthesis in the system part");
    check(ravenemudsk_ra::user_agent() == agent, "One stable value for every request");
    if (failures) return EXIT_FAILURE;
    std::cout << "User-Agent: product, system, engine and SDK versions\n";
    return EXIT_SUCCESS;
}
