# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.22)
if(NOT APP OR NOT IS_DIRECTORY "${APP}/Contents/MacOS")
  message(FATAL_ERROR "APP must point to the staged RavenEmu.app")
endif()
include(BundleUtilities)

# Record the original locations before fixup_bundle rewrites the install names.
# The packaging script uses these paths to include the matching license files.
get_bundle_keys("${APP}" "" "" bundle_keys)
file(WRITE "${APP}/Contents/Resources/dependency-paths.txt" "")
foreach(key IN LISTS bundle_keys)
  if("${${key}_COPYFLAG}" STREQUAL "1")
    file(APPEND "${APP}/Contents/Resources/dependency-paths.txt" "${${key}_RESOLVED_ITEM}\n")
  endif()
endforeach()
clear_bundle_keys(bundle_keys)

# Copy non-system dylibs and rewrite references to locations inside the app.
# Apple frameworks and /usr/lib libraries remain provided by macOS.
fixup_bundle("${APP}" "" "")
