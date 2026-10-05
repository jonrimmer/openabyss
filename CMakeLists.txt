cmake_minimum_required(VERSION 3.16)
project(OpenAbyss LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(EMSCRIPTEN)
  # SDL's EM_ASM macros require GNU extensions with modern Emscripten.
  set(CMAKE_C_EXTENSIONS ON)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SDL 3: an installed one when there is one, else built from source here.
option(OPENABYSS_VENDORED_SDL "Build SDL 3 from source rather than use an installed one" OFF)
if(NOT OPENABYSS_VENDORED_SDL AND NOT EMSCRIPTEN)
  find_package(SDL3 CONFIG QUIET)
endif()
set(OPENABYSS_SDL_BUILT OFF)
if(NOT SDL3_FOUND)
  set(OPENABYSS_SDL_BUILT ON)
  message(STATUS "OpenAbyss: building SDL 3 from source")
  include(FetchContent)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE)
  if(EMSCRIPTEN)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
  else()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  endif()
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
endif()

file(GLOB OPENABYSS_LIBRARY CONFIGURE_DEPENDS src/uw_*.c)
add_executable(openabyss WIN32 src/tools/uwshell.c ${OPENABYSS_LIBRARY})
if(APPLE)
  set_target_properties(openabyss PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME OpenAbyss
    MACOSX_BUNDLE_BUNDLE_NAME OpenAbyss
    MACOSX_BUNDLE_GUI_IDENTIFIER org.openabyss.OpenAbyss)
endif()
target_include_directories(openabyss PRIVATE src)
target_compile_definitions(openabyss PRIVATE
  UW_NO_HARNESS UW_NO_ACCOUNTING
  UW_PROGRAM="openabyss" UW_WINDOW_TITLE="OpenAbyss" UW_PREF_DIR="openabyss")
target_link_libraries(openabyss PRIVATE SDL3::SDL3)
if(NOT MSVC)
  target_link_libraries(openabyss PRIVATE m)
  target_compile_options(openabyss PRIVATE -Wall -Wextra)
endif()
if(MINGW)
  target_link_options(openabyss PRIVATE -static-libgcc)
endif()

if(EMSCRIPTEN)
  set(OPENABYSS_WEB_DIR "${CMAKE_SOURCE_DIR}/dist/web" CACHE PATH
    "Directory containing the static browser build for GitHub Pages")
  set_target_properties(openabyss PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${OPENABYSS_WEB_DIR}")
  # Keep multi-configuration generators from adding a Release/ subfolder.
  foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(openabyss PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${OPENABYSS_WEB_DIR}")
  endforeach()
  target_link_options(openabyss PRIVATE
    "-sASYNCIFY=1"
    "-sASYNCIFY_STACK_SIZE=262144"
    "-sSTACK_SIZE=1048576"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createOpenAbyss"
    "-sENVIRONMENT=web"
    "-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','callMain']"
    "-sEXPORTED_FUNCTIONS=['_main']"
    "-sEXIT_RUNTIME=1"
    "-lidbfs.js")
  foreach(asset index.html launcher.js style.css)
    configure_file(web/${asset} "${OPENABYSS_WEB_DIR}/${asset}" COPYONLY)
  endforeach()
  file(WRITE "${OPENABYSS_WEB_DIR}/.nojekyll" "")
  add_custom_target(wasm DEPENDS openabyss)
endif()

include(CTest)
if(BUILD_TESTING AND NOT EMSCRIPTEN)
  add_executable(texmap_test tests/texmap_test.c src/uw_texmap.c)
  target_include_directories(texmap_test PRIVATE src)
  if(NOT MSVC)
    target_link_libraries(texmap_test PRIVATE m)
    target_compile_options(texmap_test PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME perspective_texmap COMMAND texmap_test)
  add_executable(console_test tests/console_test.c ${OPENABYSS_LIBRARY})
  target_include_directories(console_test PRIVATE src)
  target_compile_definitions(console_test PRIVATE UW_NO_HARNESS UW_NO_ACCOUNTING)
  target_link_libraries(console_test PRIVATE SDL3::SDL3)
  if(NOT MSVC)
    target_link_libraries(console_test PRIVATE m)
    target_compile_options(console_test PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME console COMMAND console_test)
endif()

# SDL3's library beside the program, where Windows looks for it
if(WIN32)
  add_custom_command(TARGET openabyss POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:openabyss>)
endif()

# Installed, the program finds a library installed with it in ../lib; an SDL
# built here is installed there too (SDL does not install itself when it is
# built as part of another project).
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
  set_target_properties(openabyss PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
if(EMSCRIPTEN)
  install(FILES "${OPENABYSS_WEB_DIR}/openabyss.js"
    "${OPENABYSS_WEB_DIR}/openabyss.wasm"
    "${OPENABYSS_WEB_DIR}/index.html"
    "${OPENABYSS_WEB_DIR}/launcher.js"
    "${OPENABYSS_WEB_DIR}/style.css"
    "${OPENABYSS_WEB_DIR}/.nojekyll" DESTINATION .)
else()
  install(TARGETS openabyss RUNTIME DESTINATION bin BUNDLE DESTINATION .)
endif()
if(APPLE AND NOT EMSCRIPTEN)
  # Copy SDL and any other non-system dependencies into the installed app,
  # rewriting their load paths so the bundle can be moved to another Mac.
  install(CODE "
    include(BundleUtilities)
    set(BU_CHMOD_BUNDLE_ITEMS TRUE)
    fixup_bundle(\"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/OpenAbyss.app\"
      \"\" \"$<TARGET_FILE_DIR:SDL3::SDL3>\")
  ")
elseif(OPENABYSS_SDL_BUILT AND NOT EMSCRIPTEN)
  install(TARGETS SDL3-shared LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
endif()
