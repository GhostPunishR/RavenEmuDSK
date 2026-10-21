// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>
namespace ravenemudsk_ra {
// The one User-Agent RavenEmu sends to RetroAchievements, its image server and
// GitHub: product and version, system, emulation engine and achievement SDK,
// e.g. "RavenEmu/0.2.0 (Windows 10.0.26100; x64) RavenCore/0.7.1 rcheevos/12.5".
std::string user_agent();
// The system part alone, e.g. "Windows 10.0.26100; x64".
std::string system_description();
} // namespace ravenemudsk_ra
