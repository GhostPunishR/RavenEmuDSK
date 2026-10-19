cmake_minimum_required(VERSION 3.22)
project(RavenEmuDSK VERSION 0.1.0 LANGUAGES C CXX)
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
  GIT_TAG 241f56c3564ca1389813753d5c13dd931771fe40
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
  src/ui/library.cpp src/ui/settings.cpp src/ui/storage.cpp src/ui/updates.cpp)
target_include_directories(ravenemudsk_ui_library PUBLIC include)
target_compile_features(ravenemudsk_ui_library PUBLIC cxx_std_20)
target_link_libraries(ravenemudsk_ui_library PRIVATE ravenemu_common)

add_library(ravenemudsk_game_input STATIC src/ui/game_input.cpp)
target_include_directories(ravenemudsk_game_input PUBLIC include)
target_link_libraries(ravenemudsk_game_input PUBLIC ${RAVENEMUDSK_SDL} ravenemu_c_api)

add_library(ravenemudsk_session_store STATIC src/achievements/session_store.cpp src/achievements/profile_store.cpp)
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

add_executable(RavenEmuDSK
  src/desktop.cpp
  src/achievements/client.cpp src/achievements/progress.cpp src/achievements/notifications.cpp
  src/ui/badge_cache.cpp
  src/ui/app_shell.cpp src/ui/game_overlay.cpp
  src/ui/image_loader.cpp src/ui/theme.cpp
  src/ui/widgets.cpp src/ui/update_check.cpp)
target_compile_features(RavenEmuDSK PRIVATE cxx_std_20)
target_compile_definitions(RavenEmuDSK PRIVATE
  RAVENEMUDSK_VERSION="${PROJECT_VERSION}" RAVENEMUDSK_HAVE_CURL)
target_include_directories(RavenEmuDSK PRIVATE include "${stb_SOURCE_DIR}")
target_link_libraries(RavenEmuDSK PRIVATE
  ravenemudsk_ui_library ravenemudsk_imgui ravenemudsk_game_input ravenemu_c_api
  ravenemudsk_rcheevos ravenemudsk_session_store ${RAVENEMUDSK_SDL} CURL::libcurl)
if(WIN32)
  target_compile_definitions(RavenEmuDSK PRIVATE SDL_MAIN_HANDLED)
  set_target_properties(RavenEmuDSK PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(RavenEmuDSK PRIVATE comdlg32 shell32 ole32 winhttp)
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
  add_executable(ravenemudsk_notifications_test tests/notifications_test.cpp src/achievements/notifications.cpp src/achievements/progress.cpp)
  target_include_directories(ravenemudsk_notifications_test PRIVATE include)
  add_test(NAME retro_notifications COMMAND ravenemudsk_notifications_test)
  add_executable(ravenemudsk_profile_store_test tests/profile_store_test.cpp)
  target_link_libraries(ravenemudsk_profile_store_test PRIVATE ravenemudsk_session_store)
  add_test(NAME retro_profile_store COMMAND ravenemudsk_profile_store_test)
  add_executable(ravenemudsk_badge_cache_test tests/badge_cache_test.cpp src/ui/badge_cache.cpp src/ui/image_loader.cpp)
  target_compile_definitions(ravenemudsk_badge_cache_test PRIVATE SDL_MAIN_HANDLED)
  target_include_directories(ravenemudsk_badge_cache_test PRIVATE include "${stb_SOURCE_DIR}")
  target_link_libraries(ravenemudsk_badge_cache_test PRIVATE ${RAVENEMUDSK_SDL} CURL::libcurl)
  add_test(NAME retro_badge_cache COMMAND ravenemudsk_badge_cache_test "${RAVENEMUDSK_RA_LOGO}")
  add_executable(ravenemudsk_progress_test
    tests/progress_test.cpp src/achievements/progress.cpp)
  target_include_directories(ravenemudsk_progress_test PRIVATE include)
  add_test(NAME retro_progress COMMAND ravenemudsk_progress_test)
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
  add_executable(ravenemudsk_home_grid_test tests/home_grid_test.cpp
    src/achievements/client.cpp src/ui/badge_cache.cpp src/ui/image_loader.cpp
    src/ui/theme.cpp src/ui/widgets.cpp src/ui/update_check.cpp)
  target_include_directories(ravenemudsk_home_grid_test PRIVATE include "${stb_SOURCE_DIR}")
  target_compile_definitions(ravenemudsk_home_grid_test PRIVATE SDL_MAIN_HANDLED RAVENEMUDSK_HAVE_CURL)
  target_link_libraries(ravenemudsk_home_grid_test PRIVATE ravenemudsk_ui_library
    ravenemudsk_imgui ravenemu_c_api ravenemudsk_rcheevos ravenemudsk_session_store ${RAVENEMUDSK_SDL} CURL::libcurl)
  if(WIN32)
    target_link_libraries(ravenemudsk_home_grid_test PRIVATE comdlg32 shell32 ole32 winhttp)
  endif()
  add_test(NAME home_grid COMMAND ravenemudsk_home_grid_test)
  set_tests_properties(home_grid PROPERTIES TIMEOUT 30 ENVIRONMENT "RAVENEMUDSK_UI_SMOKE=1")
  add_executable(ravenemudsk_ra_game_test tests/ra_game_test.cpp src/achievements/client.cpp)
  target_include_directories(ravenemudsk_ra_game_test PRIVATE include)
  target_link_libraries(ravenemudsk_ra_game_test PRIVATE ravenemudsk_session_store ravenemu_c_api ravenemudsk_rcheevos CURL::libcurl)
  add_test(NAME retro_game_integration COMMAND ravenemudsk_ra_game_test)
  set_tests_properties(retro_game_integration PROPERTIES TIMEOUT 30)
  add_executable(ravenemudsk_session_test tests/session_test.cpp src/achievements/client.cpp)
  target_include_directories(ravenemudsk_session_test PRIVATE include)
  target_link_libraries(ravenemudsk_session_test PRIVATE ravenemudsk_session_store
    ravenemu_c_api ravenemudsk_rcheevos CURL::libcurl)
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
