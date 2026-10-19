cmake_minimum_required(VERSION 3.22)
project(RavenEmuDSK VERSION 0.1.0 LANGUAGES C CXX)
include(CTest)
include(FetchContent)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# All source dependencies are pinned. RavenCore stays a separate reusable engine.
set(RAVENEMU_BUILD_LIBRETRO OFF CACHE BOOL "" FORCE)
set(RAVENEMU_INSTALL OFF CACHE BOOL "" FORCE)
set(RAVENEMUDSK_LOCAL_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ravencore
  GIT_REPOSITORY https://github.com/GhostPunishR/RavenCore.git
  GIT_TAG 8f682a121b0ce2dc1d5c4777fc602e8f449110b5
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
add_library(ravenemudsk_rcheevos STATIC ${RC_SOURCES})
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

add_executable(RavenEmuDSK
  src/desktop.cpp
  src/achievements/client.cpp
  src/ui/app_shell.cpp src/ui/game_overlay.cpp
  src/ui/image_loader.cpp src/ui/theme.cpp
  src/ui/widgets.cpp src/ui/update_check.cpp)
target_compile_features(RavenEmuDSK PRIVATE cxx_std_20)
target_compile_definitions(RavenEmuDSK PRIVATE
  RAVENEMUDSK_VERSION="${PROJECT_VERSION}" RAVENEMUDSK_HAVE_CURL)
target_include_directories(RavenEmuDSK PRIVATE include "${stb_SOURCE_DIR}")
target_link_libraries(RavenEmuDSK PRIVATE
  ravenemudsk_ui_library ravenemudsk_imgui ravenemu_c_api
  ravenemudsk_rcheevos ${RAVENEMUDSK_SDL} CURL::libcurl)
if(WIN32)
  target_compile_definitions(RavenEmuDSK PRIVATE SDL_MAIN_HANDLED)
  set_target_properties(RavenEmuDSK PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(RavenEmuDSK PRIVATE comdlg32 shell32 ole32 winhttp)
  add_custom_command(TARGET RavenEmuDSK POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:RavenEmuDSK> $<TARGET_FILE_DIR:RavenEmuDSK>
    COMMAND_EXPAND_LISTS VERBATIM)
endif()

# Reuse the RavenEmu brand asset from a fixed revision (not a floating main URL).
set(RAVENEMUDSK_BRAND_URL
  "https://raw.githubusercontent.com/GhostPunishR/RavenEmu/5f002726a68c1978f4cad273bf8b7b3786b9fb3b/docs/assets/ravenemu-logo.png")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/branding")
file(DOWNLOAD "${RAVENEMUDSK_BRAND_URL}"
  "${CMAKE_CURRENT_BINARY_DIR}/branding/ravenemu-logo.png"
  STATUS BRAND_STATUS TLS_VERIFY ON)
list(GET BRAND_STATUS 0 BRAND_ERROR)
if(BRAND_ERROR EQUAL 0)
  add_custom_command(TARGET RavenEmuDSK POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RavenEmuDSK>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_BINARY_DIR}/branding/ravenemu-logo.png"
      "$<TARGET_FILE_DIR:RavenEmuDSK>/assets/ravenemu-logo.png"
    VERBATIM)
else()
  message(WARNING "RavenEmu brand asset download failed. See README.")
endif()

if(BUILD_TESTING)
  add_executable(ravenemudsk_library_test tests/library_test.cpp)
  target_link_libraries(ravenemudsk_library_test PRIVATE ravenemudsk_ui_library)
  add_test(NAME library COMMAND ravenemudsk_library_test)
  add_test(NAME help COMMAND RavenEmuDSK --help)
  add_test(NAME launcher_smoke COMMAND ${CMAKE_COMMAND} -E env
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RAVENEMUDSK_UI_SMOKE=1
    $<TARGET_FILE:RavenEmuDSK>)
  set_tests_properties(launcher_smoke PROPERTIES TIMEOUT 30)
endif()
