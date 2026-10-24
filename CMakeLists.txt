cmake_minimum_required(VERSION 3.22)
project(RavenEmuDSK VERSION 0.3.0 LANGUAGES C CXX)
include(CTest)
include(FetchContent)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libcurl includes Windows headers too. Apply this before any dependency or
# application target so min/max macros cannot break C++ standard functions.
if(WIN32)
  add_compile_definitions(NOMINMAX)
endif()

# All source dependencies are pinned. RavenCore stays a separate reusable engine.
set(RAVENEMU_BUILD_LIBRETRO OFF CACHE BOOL "" FORCE)
set(RAVENEMU_INSTALL OFF CACHE BOOL "" FORCE)
set(RAVENEMUDSK_LOCAL_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ravencore
  GIT_REPOSITORY https://github.com/GhostPunishR/RavenCore.git
  GIT_TAG 15675cc3af4f9b07f2e10cc5a7ca6234ea1c77cc
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(ravencore)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG f5befd2d29e66809cd1110a152e375a7f1981f06
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(imgui)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(stb)
FetchContent_Declare(rcheevos
  GIT_REPOSITORY https://github.com/RetroAchievements/rcheevos.git
  GIT_TAG 1433173220a7eaede6a9ed7a18e94117be1821e0
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(rcheevos)
set(BUILD_TESTING ${RAVENEMUDSK_LOCAL_TESTING} CACHE BOOL "" FORCE)
file(GLOB RC_SOURCES CONFIGURE_DEPENDS
  "${rcheevos_SOURCE_DIR}/src/*.c"
  "${rcheevos_SOURCE_DIR}/src/rcheevos/*.c"
  "${rcheevos_SOURCE_DIR}/src/rapi/*.c"
  "${rcheevos_SOURCE_DIR}/src/rhash/*.c")
list(FILTER RC_SOURCES EXCLUDE REGEX "/rc_(libretro|client_external)\\.c$")
include(cmake/rcheevos_safety.cmake)
add_library(ravenemudsk_rcheevos STATIC ${RC_SOURCES})
target_include_directories(ravenemudsk_rcheevos PRIVATE "${rcheevos_SOURCE_DIR}/src")
target_include_directories(ravenemudsk_rcheevos PUBLIC "${rcheevos_SOURCE_DIR}/include")
target_compile_definitions(ravenemudsk_rcheevos PUBLIC RC_DISABLE_LUA RC_CLIENT_SUPPORTS_HASH)

find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
  set(RAVENEMUDSK_SDL SDL2::SDL2)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  set(RAVENEMUDSK_SDL PkgConfig::SDL2)
endif()
find_package(CURL REQUIRED)

add_library(ravenemudsk_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(ravenemudsk_imgui PUBLIC "${imgui_SOURCE_DIR}")
target_link_libraries(ravenemudsk_imgui PUBLIC ${RAVENEMUDSK_SDL})

add_library(ravenemudsk_ui_library STATIC
  src/library/library.cpp src/settings/settings.cpp src/library/storage.cpp src/updates/updates.cpp)
target_include_directories(ravenemudsk_ui_library PUBLIC include)
target_compile_features(ravenemudsk_ui_library PUBLIC cxx_std_20)
target_link_libraries(ravenemudsk_ui_library PRIVATE ravenemu_common)

add_library(ravenemudsk_game_input STATIC src/input/game_input.cpp src/input/keyboard_bindings.cpp src/input/pad_bindings.cpp
  src/input/motion.cpp src/settings/game_preferences.cpp)
target_include_directories(ravenemudsk_game_input PUBLIC include)
target_link_libraries(ravenemudsk_game_input PUBLIC ${RAVENEMUDSK_SDL} ravenemu_c_api ravenemudsk_ui_library)
add_library(ravenemudsk_game_state STATIC src/saves/game_state.cpp)
target_include_directories(ravenemudsk_game_state PUBLIC include)
target_link_libraries(ravenemudsk_game_state PRIVATE ravenemu_common ravenemu_c_api)
# The wireless link cable: the game's link port over the local network, with
# RavenEmu Android or another computer (own protocol, own mDNS).
find_package(Threads REQUIRED)
add_library(ravenemudsk_wireless STATIC src/wireless/protocol.cpp src/wireless/net.cpp src/wireless/mdns.cpp
  src/wireless/connection.cpp src/wireless/wireless_link.cpp)
target_include_directories(ravenemudsk_wireless PUBLIC include)
target_compile_features(ravenemudsk_wireless PUBLIC cxx_std_20)
target_link_libraries(ravenemudsk_wireless PUBLIC ravenemu_c_api Threads::Threads)
if(WIN32)
  target_link_libraries(ravenemudsk_wireless PUBLIC ws2_32)
endif()
# The User-Agent of every request: product, system, engine and SDK versions.
add_library(ravenemudsk_user_agent STATIC src/achievements/user_agent.cpp)
target_include_directories(ravenemudsk_user_agent PUBLIC include)
target_compile_definitions(ravenemudsk_user_agent PRIVATE RAVENEMUDSK_VERSION="${PROJECT_VERSION}")
target_link_libraries(ravenemudsk_user_agent PRIVATE ravenemu_c_api ravenemudsk_rcheevos)

add_library(ravenemudsk_session_store STATIC src/achievements/session_store.cpp src/achievements/profile_store.cpp
  src/achievements/outbox.cpp)
target_include_directories(ravenemudsk_session_store PUBLIC include)
target_link_libraries(ravenemudsk_session_store PRIVATE ravenemu_common)
if(WIN32)
  target_link_libraries(ravenemudsk_session_store PRIVATE advapi32)
elseif(APPLE)
  target_link_libraries(ravenemudsk_session_store PRIVATE "-framework Security" "-framework CoreFoundation")
elseif(UNIX)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
  endif()
  if(TARGET PkgConfig::LIBSECRET)
    target_compile_definitions(ravenemudsk_session_store PRIVATE RAVENEMUDSK_HAVE_LIBSECRET)
    target_link_libraries(ravenemudsk_session_store PRIVATE PkgConfig::LIBSECRET)
  else()
    message(STATUS "libsecret absent: RetroAchievements sessions will not persist on Linux")
  endif()
endif()

# The RetroAchievements client and what it reports to the screens.
add_library(ravenemudsk_achievements STATIC
  src/achievements/client.cpp src/achievements/progress.cpp src/achievements/notifications.cpp
  src/achievements/achievement_list.cpp src/achievements/library_progress.cpp)
target_include_directories(ravenemudsk_achievements PUBLIC include)
target_link_libraries(ravenemudsk_achievements PUBLIC ravenemudsk_rcheevos ravenemudsk_session_store
  ravenemudsk_user_agent ravenemu_c_api PRIVATE CURL::libcurl)

# Interface pieces shared by the launcher and the game: theme, widgets, icons, images.
add_library(ravenemudsk_ui_kit STATIC
  src/ui/theme.cpp src/ui/widgets.cpp src/ui/icons.cpp src/input/pad_glyphs.cpp src/achievements/retro_widgets.cpp
  src/achievements/badge_cache.cpp src/ui/image_loader.cpp src/updates/update_check.cpp)
target_include_directories(ravenemudsk_ui_kit PUBLIC include PRIVATE "${stb_SOURCE_DIR}")
target_compile_definitions(ravenemudsk_ui_kit PRIVATE RAVENEMUDSK_HAVE_CURL)
target_link_libraries(ravenemudsk_ui_kit PUBLIC ravenemudsk_imgui ravenemudsk_achievements ravenemudsk_game_input
  ravenemudsk_ui_library PRIVATE CURL::libcurl)
if(WIN32)
  target_link_libraries(ravenemudsk_ui_kit PRIVATE winhttp)
endif()

# Drawn over the game: pause menu, RetroAchievements banner, sounds, and the
# sound kept in step with the picture.
add_library(ravenemudsk_game_ui STATIC src/player/game_overlay.cpp src/audio/game_sound.cpp src/audio/av_sync.cpp)
target_link_libraries(ravenemudsk_game_ui PUBLIC ravenemudsk_ui_kit)

# The launcher, one file per screen (src/launcher); the program sees app_shell.hpp.
add_library(ravenemudsk_launcher STATIC
  src/launcher/common.cpp src/launcher/chrome.cpp src/launcher/library_view.cpp src/launcher/profile_view.cpp
  src/launcher/tv_view.cpp src/launcher/updates_view.cpp src/launcher/settings_view.cpp
  src/launcher/game_detail.cpp src/launcher/launcher_frame.cpp src/launcher/launcher.cpp
  src/launcher/preferences.cpp)
target_compile_definitions(ravenemudsk_launcher PRIVATE RAVENEMUDSK_VERSION="${PROJECT_VERSION}")
target_link_libraries(ravenemudsk_launcher PUBLIC ravenemudsk_ui_kit)
if(WIN32)
  target_link_libraries(ravenemudsk_launcher PRIVATE comdlg32 shell32 ole32)
endif()

add_executable(RavenEmuDSK src/desktop.cpp src/player/game_session.cpp)
target_compile_features(RavenEmuDSK PRIVATE cxx_std_20)
target_compile_definitions(RavenEmuDSK PRIVATE RAVENEMUDSK_VERSION="${PROJECT_VERSION}")
target_link_libraries(RavenEmuDSK PRIVATE ravenemudsk_launcher ravenemudsk_game_ui ravenemudsk_game_state
  ravenemudsk_wireless)
if(WIN32)
  target_compile_definitions(RavenEmuDSK PRIVATE SDL_MAIN_HANDLED)
  set_target_properties(RavenEmuDSK PROPERTIES WIN32_EXECUTABLE TRUE)
  add_custom_command(TARGET RavenEmuDSK POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:RavenEmuDSK> $<TARGET_FILE_DIR:RavenEmuDSK>
    COMMAND_EXPAND_LISTS VERBATIM)
endif()

# Use the unpadded RavenEmu icon for both the UI and SDL window icons.
# Bundle the original PNG so its transparent corners survive offline builds.
set(RAVENEMUDSK_BRAND_LOGO "${CMAKE_CURRENT_SOURCE_DIR}/assets/ravenemu-logo.png")
if(NOT EXISTS "${RAVENEMUDSK_BRAND_LOGO}")
  message(FATAL_ERROR "Missing RavenEmu artwork: assets/ravenemu-logo.png")
endif()
add_custom_command(TARGET RavenEmuDSK POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RavenEmuDSK>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${RAVENEMUDSK_BRAND_LOGO}"
    "$<TARGET_FILE_DIR:RavenEmuDSK>/assets/ravenemu-logo.png"
  VERBATIM)

# The program file itself shows the provided logo: a build-time tool turns
# assets/ravenemu-logo.png into ravenemu.ico, embedded as a Windows resource.
# macOS gets its .icns in tools/package_unix.py.
add_executable(ravenemudsk_make_icon tools/make_icon.cpp)
target_include_directories(ravenemudsk_make_icon PRIVATE "${stb_SOURCE_DIR}")
set(RAVENEMUDSK_ICON "${CMAKE_CURRENT_BINARY_DIR}/ravenemu.ico")
add_custom_command(OUTPUT "${RAVENEMUDSK_ICON}"
  COMMAND ravenemudsk_make_icon "${RAVENEMUDSK_BRAND_LOGO}" "${RAVENEMUDSK_ICON}"
  DEPENDS ravenemudsk_make_icon "${RAVENEMUDSK_BRAND_LOGO}"
  COMMENT "RavenEmu icon from assets/ravenemu-logo.png" VERBATIM)
if(WIN32)
  set(RAVENEMUDSK_RC "${CMAKE_CURRENT_BINARY_DIR}/ravenemu.rc")
  file(GENERATE OUTPUT "${RAVENEMUDSK_RC}" CONTENT "IDI_RAVENEMU ICON \"${RAVENEMUDSK_ICON}\"\n")
  target_sources(RavenEmuDSK PRIVATE "${RAVENEMUDSK_RC}" "${RAVENEMUDSK_ICON}")
  set_source_files_properties("${RAVENEMUDSK_RC}" PROPERTIES OBJECT_DEPENDS "${RAVENEMUDSK_ICON}")
else()
  add_custom_target(ravenemudsk_icon ALL DEPENDS "${RAVENEMUDSK_ICON}")
endif()

# Bundle the provided RetroAchievements artwork from this repository, not a
# network dependency. The image remains available in offline builds.
set(RAVENEMUDSK_RA_LOGO "${CMAKE_CURRENT_SOURCE_DIR}/assets/ra-logo.png")
if(NOT EXISTS "${RAVENEMUDSK_RA_LOGO}")
  message(FATAL_ERROR "Missing RetroAchievements artwork: assets/ra-logo.png")
endif()
add_custom_command(TARGET RavenEmuDSK POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RavenEmuDSK>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${RAVENEMUDSK_RA_LOGO}"
    "$<TARGET_FILE_DIR:RavenEmuDSK>/assets/ra-logo.png"
  VERBATIM)

if(BUILD_TESTING)
  add_executable(ravenemudsk_notifications_test tests/notifications_test.cpp)
  target_link_libraries(ravenemudsk_notifications_test PRIVATE ravenemudsk_achievements)
  add_test(NAME retro_notifications COMMAND ravenemudsk_notifications_test)
  add_executable(ravenemudsk_game_sound_test tests/game_sound_test.cpp)
  target_compile_definitions(ravenemudsk_game_sound_test PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(ravenemudsk_game_sound_test PRIVATE ravenemudsk_game_ui)
  add_test(NAME game_sound COMMAND ravenemudsk_game_sound_test)
  add_executable(ravenemudsk_av_sync_test tests/av_sync_test.cpp)
  target_compile_definitions(ravenemudsk_av_sync_test PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(ravenemudsk_av_sync_test PRIVATE ravenemudsk_game_ui)
  add_test(NAME av_sync COMMAND ravenemudsk_av_sync_test)
  add_executable(ravenemudsk_profile_store_test tests/profile_store_test.cpp)
  target_link_libraries(ravenemudsk_profile_store_test PRIVATE ravenemudsk_session_store)
  add_test(NAME retro_profile_store COMMAND ravenemudsk_profile_store_test)
  add_executable(ravenemudsk_outbox_test tests/outbox_test.cpp)
  target_link_libraries(ravenemudsk_outbox_test PRIVATE ravenemudsk_session_store)
  add_test(NAME retro_outbox COMMAND ravenemudsk_outbox_test)
  add_executable(ravenemudsk_badge_cache_test tests/badge_cache_test.cpp)
  target_compile_definitions(ravenemudsk_badge_cache_test PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(ravenemudsk_badge_cache_test PRIVATE ravenemudsk_ui_kit)
  add_test(NAME retro_badge_cache COMMAND ravenemudsk_badge_cache_test "${RAVENEMUDSK_RA_LOGO}")
  add_executable(ravenemudsk_progress_test tests/progress_test.cpp)
  target_link_libraries(ravenemudsk_progress_test PRIVATE ravenemudsk_achievements)
  add_test(NAME retro_progress COMMAND ravenemudsk_progress_test)
  add_executable(ravenemudsk_achievement_list_test tests/achievement_list_test.cpp)
  target_link_libraries(ravenemudsk_achievement_list_test PRIVATE ravenemudsk_achievements)
  add_test(NAME retro_achievement_list COMMAND ravenemudsk_achievement_list_test)
  add_executable(ravenemudsk_library_progress_test tests/library_progress_test.cpp)
  target_link_libraries(ravenemudsk_library_progress_test PRIVATE ravenemudsk_achievements)
  add_test(NAME retro_library_progress COMMAND ravenemudsk_library_progress_test)
  add_executable(ravenemudsk_screenshot_test tests/screenshot_test.cpp)
  target_compile_definitions(ravenemudsk_screenshot_test PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(ravenemudsk_screenshot_test PRIVATE ravenemudsk_ui_kit)
  add_test(NAME screenshot COMMAND ravenemudsk_screenshot_test)
  add_executable(ravenemudsk_icons_test tests/icons_test.cpp)
  target_compile_definitions(ravenemudsk_icons_test PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(ravenemudsk_icons_test PRIVATE ravenemudsk_ui_kit)
  add_test(NAME icons COMMAND ravenemudsk_icons_test)
  add_executable(ravenemudsk_library_test tests/library_test.cpp)
  target_link_libraries(ravenemudsk_library_test PRIVATE ravenemudsk_ui_library)
  add_test(NAME library COMMAND ravenemudsk_library_test)
  add_executable(ravenemudsk_storage_test tests/storage_test.cpp)
  target_link_libraries(ravenemudsk_storage_test PRIVATE ravenemudsk_ui_library)
  add_test(NAME storage COMMAND ravenemudsk_storage_test)
  add_executable(ravenemudsk_game_input_test tests/game_input_test.cpp)
  target_compile_definitions(ravenemudsk_game_input_test PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(ravenemudsk_game_input_test PRIVATE ravenemudsk_game_input)
  add_test(NAME game_input COMMAND ravenemudsk_game_input_test)
  add_executable(ravenemudsk_motion_test tests/motion_test.cpp)
  target_compile_definitions(ravenemudsk_motion_test PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(ravenemudsk_motion_test PRIVATE ravenemudsk_game_input)
  add_test(NAME motion COMMAND ravenemudsk_motion_test)
  # The launcher's screens, driven through their internal header.
  foreach(view home_grid profile_view)
    add_executable(ravenemudsk_${view}_test tests/${view}_test.cpp)
    target_compile_definitions(ravenemudsk_${view}_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(ravenemudsk_${view}_test PRIVATE ravenemudsk_launcher)
    add_test(NAME ${view} COMMAND ravenemudsk_${view}_test)
  endforeach()
  set_tests_properties(home_grid PROPERTIES TIMEOUT 30 ENVIRONMENT "RAVENEMUDSK_UI_SMOKE=1")
  set_tests_properties(profile_view PROPERTIES TIMEOUT 30 ENVIRONMENT "RAVENEMUDSK_UI_SMOKE=1;SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
  add_executable(ravenemudsk_ra_game_test tests/ra_game_test.cpp)
  target_link_libraries(ravenemudsk_ra_game_test PRIVATE ravenemudsk_achievements)
  add_test(NAME retro_game_integration COMMAND ravenemudsk_ra_game_test)
  set_tests_properties(retro_game_integration PROPERTIES TIMEOUT 30)
  add_executable(ravenemudsk_ra_features_test tests/ra_features_test.cpp)
  target_link_libraries(ravenemudsk_ra_features_test PRIVATE ravenemudsk_achievements ravenemudsk_game_state)
  add_test(NAME retro_features COMMAND ravenemudsk_ra_features_test)
  set_tests_properties(retro_features PROPERTIES TIMEOUT 30)
  add_executable(ravenemudsk_game_overlay_test tests/game_overlay_test.cpp)
  target_compile_definitions(ravenemudsk_game_overlay_test PRIVATE SDL_MAIN_HANDLED)
  target_link_libraries(ravenemudsk_game_overlay_test PRIVATE ravenemudsk_game_ui)
  add_test(NAME game_overlay COMMAND ravenemudsk_game_overlay_test)
  set_tests_properties(game_overlay PROPERTIES TIMEOUT 30
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;RAVENEMUDSK_UI_SMOKE=1")
  add_executable(ravenemudsk_wireless_protocol_test tests/wireless_protocol_test.cpp)
  target_link_libraries(ravenemudsk_wireless_protocol_test PRIVATE ravenemudsk_wireless)
  add_test(NAME wireless_protocol COMMAND ravenemudsk_wireless_protocol_test)
  add_executable(ravenemudsk_mdns_test tests/mdns_test.cpp)
  target_link_libraries(ravenemudsk_mdns_test PRIVATE ravenemudsk_wireless)
  add_test(NAME wireless_mdns COMMAND ravenemudsk_mdns_test)
  add_executable(ravenemudsk_wireless_link_test tests/wireless_link_test.cpp)
  target_link_libraries(ravenemudsk_wireless_link_test PRIVATE ravenemudsk_wireless)
  add_test(NAME wireless_link COMMAND ravenemudsk_wireless_link_test)
  set_tests_properties(wireless_link PROPERTIES TIMEOUT 60)
  add_executable(ravenemudsk_user_agent_test tests/user_agent_test.cpp)
  target_compile_definitions(ravenemudsk_user_agent_test PRIVATE EXPECTED_VERSION="${PROJECT_VERSION}")
  target_link_libraries(ravenemudsk_user_agent_test PRIVATE ravenemudsk_user_agent ravenemu_c_api)
  add_test(NAME user_agent COMMAND ravenemudsk_user_agent_test)
  add_executable(ravenemudsk_session_test tests/session_test.cpp)
  target_link_libraries(ravenemudsk_session_test PRIVATE ravenemudsk_achievements)
  add_test(NAME retro_session COMMAND ravenemudsk_session_test)
  set_tests_properties(retro_session PROPERTIES TIMEOUT 30)
  if(WIN32)
    add_executable(ravenemudsk_windows_session_test tests/windows_session_test.cpp)
    target_link_libraries(ravenemudsk_windows_session_test PRIVATE ravenemudsk_session_store)
    add_test(NAME windows_session_store COMMAND ravenemudsk_windows_session_test)
    set_tests_properties(windows_session_store PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
  endif()
  if(APPLE OR TARGET PkgConfig::LIBSECRET)
    add_executable(ravenemudsk_native_session_test tests/native_session_test.cpp)
    target_link_libraries(ravenemudsk_native_session_test PRIVATE ravenemudsk_session_store)
    # Opt in only with an isolated/unlocked OS vault (CI creates one explicitly).
    option(RAVENEMUDSK_TEST_NATIVE_VAULT "Test native session storage in an isolated OS vault" OFF)
    if(RAVENEMUDSK_TEST_NATIVE_VAULT)
      add_test(NAME native_session_store COMMAND ravenemudsk_native_session_test)
      set_tests_properties(native_session_store PROPERTIES TIMEOUT 30)
    endif()
  endif()
  add_test(NAME help COMMAND RavenEmuDSK --help)
  add_test(NAME launcher_smoke COMMAND ${CMAKE_COMMAND} -E env
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RAVENEMUDSK_UI_SMOKE=1
    $<TARGET_FILE:RavenEmuDSK>)
  set_tests_properties(launcher_smoke PROPERTIES TIMEOUT 30)
  add_test(NAME retro_profile_smoke COMMAND ${CMAKE_COMMAND} -E env
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RAVENEMUDSK_UI_SMOKE=1
    RAVENEMUDSK_UI_SMOKE_PAGE=profile $<TARGET_FILE:RavenEmuDSK>)
  set_tests_properties(retro_profile_smoke PROPERTIES TIMEOUT 30)
  add_test(NAME launcher_invalid_video COMMAND ${CMAKE_COMMAND} -E env
    SDL_VIDEODRIVER=unavailable-raven-test-driver SDL_AUDIODRIVER=dummy RAVENEMUDSK_UI_SMOKE=1
    $<TARGET_FILE:RavenEmuDSK>)
  add_test(NAME launcher_screenshot_failure COMMAND ${CMAKE_COMMAND} -E env
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RAVENEMUDSK_UI_SMOKE=1
    "RAVENEMUDSK_UI_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}"
    $<TARGET_FILE:RavenEmuDSK>)
  set_tests_properties(launcher_invalid_video launcher_screenshot_failure
    PROPERTIES WILL_FAIL TRUE TIMEOUT 30)
endif()
