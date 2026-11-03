# swosdata -- OpenSWOS's data setup: the conversion library, its command-line
# tool, and "OpenSWOS Setup", the window the Windows installer launches after
# installing the game.
#
#   swosdata_lib     lib/*.c + third_party/swosgfx/lib/*.c. Plain C99, no UI.
#   swosdata         cli/swosdata_cli.c (the Makefile builds the same tool).
#   openswos-setup   app/main.cpp + app/ui/*.slint, on Slint.
#
# build.sh / build.bat drive this; `static` links Slint statically, so the
# Windows installer ships ONE .exe (docs: StorageAgent's docs/SLINT_STATIC.md).
cmake_minimum_required(VERSION 3.21)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(OpenSWOSSetup
        VERSION 1.0.0
        DESCRIPTION "OpenSWOS Setup - the game's data from your own copy of SWOS"
        LANGUAGES C CXX)

set(SD_DISPLAY_NAME "OpenSWOS Setup")

if(APPLE)
  enable_language(OBJC)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Windows has no rpath: a shared Slint has to sit beside the executable.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(OPENSWOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "The OpenSWOS checkout")

# MSVC: link the C/C++ runtime statically (/MT), so the Windows .exe runs on a
# machine without the VC++ redistributable. Only meaningful with a static Slint;
# a shared slint_cpp.dll brings the DLL runtime with it anyway.
option(SD_STATIC_CRT "MSVC: static C/C++ runtime" OFF)
if(MSVC AND SD_STATIC_CRT)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ------------------------------------------------------------------ library ---

# lib/ is OpenSWOS's own half (the disks, the player's copy, the file lists, the
# teams, the tiers); the converters are swosgfx's, the submodule
# third_party/swosgfx (`git submodule update --init`).
set(SWOSGFX_DIR "${OPENSWOS_ROOT}/third_party/swosgfx")
if(NOT EXISTS "${SWOSGFX_DIR}/lib/sg_util.h")
  message(FATAL_ERROR "third_party/swosgfx is empty: run `git submodule update --init` in the OpenSWOS checkout")
endif()
file(GLOB SD_LIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c")
file(GLOB SG_LIB_SOURCES CONFIGURE_DEPENDS "${SWOSGFX_DIR}/lib/*.c")
# miniz (third_party/miniz, the game's too): the setup reads .zip downloads of the
# Amiga disks with it, so here its archive functions are on (the game's build has
# its own configuration, src/miniz_impl.c).
set(MINIZ_C "${OPENSWOS_ROOT}/third_party/miniz/miniz.c")
add_library(swosdata_lib STATIC ${SD_LIB_SOURCES} ${SG_LIB_SOURCES} ${MINIZ_C})
target_include_directories(swosdata_lib PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/lib"
  "${SWOSGFX_DIR}/lib"
  "${OPENSWOS_ROOT}/third_party/miniz")
if(NOT MSVC)
  set_source_files_properties(${MINIZ_C} PROPERTIES COMPILE_OPTIONS "-w")
endif()
set_target_properties(swosdata_lib PROPERTIES OUTPUT_NAME swosdata)
if(MSVC)
  target_compile_definitions(swosdata_lib PUBLIC _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
  target_compile_options(swosdata_lib PRIVATE /W3 /utf-8)
else()
  target_compile_options(swosdata_lib PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(swosdata_lib PUBLIC m)
endif()

# ---------------------------------------------------------------------- cli ---

add_executable(swosdata cli/swosdata_cli.c)
target_link_libraries(swosdata PRIVATE swosdata_lib)

# ---------------------------------------------------------------------- app ---

option(SD_WITH_APP "Build the OpenSWOS Setup window (needs Slint)" ON)

if(SD_WITH_APP)
  if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
  endif()
  if(SLINT_ROOT)
    # An SDK built or installed elsewhere -- the static one build.sh static and
    # build.bat static point at, or the prebuilt shared SDK.
    find_package(Slint REQUIRED PATHS "${SLINT_ROOT}/lib/cmake/Slint" NO_DEFAULT_PATH)
  else()
    if(NOT DEFINED FETCHCONTENT_BASE_DIR)
      string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" _sd_deps_key)
      set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/${_sd_deps_key}"
          CACHE PATH "Dependency cache")
    endif()
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    include(FetchSlint)
  endif()

  if(TARGET Slint::Slint)
    # nativefiledialog-extended (zlib licence), vendored in third_party/nfd.
    # Linux uses the xdg-desktop-portal backend, so the only dependency is dbus.
    set(NFD_SOURCE)
    set(SD_HAVE_DIALOGS FALSE)
    if(WIN32)
      set(NFD_SOURCE third_party/nfd/nfd_win.cpp)
      set(SD_HAVE_DIALOGS TRUE)
    elseif(APPLE)
      set(NFD_SOURCE third_party/nfd/nfd_cocoa.m)
      set(SD_HAVE_DIALOGS TRUE)
    else()
      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
        pkg_check_modules(DBUS QUIET dbus-1)
      endif()
      if(DBUS_FOUND)
        set(NFD_SOURCE third_party/nfd/nfd_portal.cpp)
        set(SD_HAVE_DIALOGS TRUE)
      else()
        message(STATUS "dbus-1 not found -- building without file pickers "
                       "(install libdbus-1-dev). --scan still works.")
      endif()
    endif()

    set(SD_PLATFORM_SOURCES "")
    if(WIN32)
      # The game's own icon. Configured so the .rc holds an absolute path.
      set(SD_ICON "${OPENSWOS_ROOT}/SWOS.ico")
      configure_file(app/openswos-setup.rc.in
                     "${CMAKE_CURRENT_BINARY_DIR}/openswos-setup.rc" @ONLY)
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/openswos-setup.rc"
          PROPERTIES OBJECT_DEPENDS "${SD_ICON}")
      # The manifest makes the process's code page UTF-8, which is what lets the
      # C library's fopen / FindFirstFileA take the UTF-8 paths the pickers
      # return (a user folder named "José" would fail otherwise).
      list(APPEND SD_PLATFORM_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/openswos-setup.rc"
                                      app/openswos-setup.manifest)
    elseif(APPLE)
      set(SD_ICNS "${OPENSWOS_ROOT}/SWOS.icns")
      if(EXISTS "${SD_ICNS}")
        list(APPEND SD_PLATFORM_SOURCES "${SD_ICNS}")
        set_source_files_properties("${SD_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
      endif()
      # The game's own files inside the bundle too (Contents/Resources, laid out as
      # bin/ is; FindResDir looks there first): Gatekeeper runs an unsigned app
      # opened where it was unzipped from a random read-only copy (App
      # Translocation), where nothing beside the .app can be seen. Named, not
      # globbed: a drop carries tracked files only.
      set(SD_RES_FILES
          setup/glyphs.txt setup/LOADER1_AGA.bmp setup/MENUS2_AGA.bmp
          setup/CJCTEAM1_CELEB.bmp setup/CJCTEAM2_CELEB.bmp setup/CJCTEAM3_CELEB.bmp
          setup/whdload/OpenSWOS.info setup/whdload/OpenSWOSAGA-NE.info
          data/credits_icons.bmp)
      foreach(f ${SD_RES_FILES})
        get_filename_component(d "${f}" DIRECTORY)
        list(APPEND SD_PLATFORM_SOURCES "${OPENSWOS_ROOT}/bin/${f}")
        set_source_files_properties("${OPENSWOS_ROOT}/bin/${f}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/${d}")
      endforeach()
    endif()

    set_property(GLOBAL PROPERTY SLINT_STYLE fluent-dark)

    # The Wi-Fi copy to a phone (app/phone_server.cpp): its QR code, Project Nayuki's (MIT).
    add_executable(openswos-setup app/main.cpp app/platform.cpp app/phone_server.cpp app/tr.cpp
                   ${OPENSWOS_ROOT}/third_party/qrcodegen/qrcodegen.c ${NFD_SOURCE} ${SD_PLATFORM_SOURCES})
    target_link_libraries(openswos-setup PRIVATE swosdata_lib Slint::Slint)
    # The window icon is compiled in, not referenced by absolute path.
    set_property(TARGET openswos-setup PROPERTY SLINT_EMBED_RESOURCES embed-files)
    # The languages (app/tr.hpp): the .po files bundled into the program for the .slint texts,
    # one msgid per English text (no per-component context), the same files' table for C++.
    set_property(TARGET openswos-setup PROPERTY SLINT_BUNDLE_TRANSLATIONS "${CMAKE_CURRENT_SOURCE_DIR}/app/lang")
    set_property(TARGET openswos-setup PROPERTY SLINT_NO_DEFAULT_TRANSLATION_CONTEXT ON)
    slint_target_sources(openswos-setup app/ui/app.slint)
    target_compile_definitions(openswos-setup PRIVATE SD_VERSION="${PROJECT_VERSION}")

    find_package(Threads REQUIRED)
    target_link_libraries(openswos-setup PRIVATE Threads::Threads)
    if(WIN32)
      # The Downloads folder (SHGetKnownFolderPath) and GOG's registry entries.
      target_link_libraries(openswos-setup PRIVATE shell32 ole32 advapi32 ws2_32)
    endif()

    if(APPLE)
      target_link_libraries(openswos-setup PRIVATE "-framework CoreFoundation")   # (tr.cpp: the system's language)
    endif()

    if(SD_HAVE_DIALOGS)
      target_compile_definitions(openswos-setup PRIVATE SD_WITH_DIALOGS=1)
      target_include_directories(openswos-setup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nfd)
      if(APPLE)
        target_link_libraries(openswos-setup PRIVATE
          "-framework AppKit" "-framework UniformTypeIdentifiers")
      elseif(NOT WIN32)
        target_include_directories(openswos-setup PRIVATE ${DBUS_INCLUDE_DIRS})
        target_link_libraries(openswos-setup PRIVATE ${DBUS_LIBRARIES})
      endif()
    endif()

    if(MSVC)
      set_source_files_properties(app/main.cpp app/platform.cpp app/phone_server.cpp app/tr.cpp
                                  PROPERTIES COMPILE_OPTIONS "/W3;/utf-8")
    else()
      set_source_files_properties(app/main.cpp app/platform.cpp app/phone_server.cpp app/tr.cpp
                                  PROPERTIES COMPILE_OPTIONS "-Wall")
    endif()

    if(APPLE)
      set_target_properties(openswos-setup PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "${SD_DISPLAY_NAME}"
        MACOSX_BUNDLE_BUNDLE_NAME "${SD_DISPLAY_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.baker76.openswos.setup"
        MACOSX_BUNDLE_ICON_FILE "SWOS")
      if(TARGET slint_cpp-shared)
        set_target_properties(openswos-setup PROPERTIES
          BUILD_RPATH "@executable_path/../Frameworks"
          INSTALL_RPATH "@executable_path/../Frameworks")
        add_custom_command(TARGET openswos-setup POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory
                  $<TARGET_BUNDLE_CONTENT_DIR:openswos-setup>/Frameworks
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:slint_cpp-shared>
                  $<TARGET_BUNDLE_CONTENT_DIR:openswos-setup>/Frameworks)
      endif()
    endif()

    if(WIN32)
      target_link_libraries(openswos-setup PRIVATE shell32 ole32 user32)
      # No console behind the window; plain main() stays the entry point.
      if(MSVC)
        target_link_options(openswos-setup PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
      endif()
      # A shared Slint: its DLL beside the exe. Empty for a static Slint.
      if(NOT TARGET slint_cpp-static)
        add_custom_command(TARGET openswos-setup POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "$<TARGET_RUNTIME_DLLS:openswos-setup>" "$<TARGET_FILE_DIR:openswos-setup>"
          COMMAND_EXPAND_LISTS VERBATIM)
      endif()
    endif()

    # A static Slint needs the system libraries its Rust crates declare, hung on
    # Slint's own imported target so they land AFTER the archive on the link
    # line (SLINT_STATIC.md, traps 1 and 2).
    if(TARGET slint_cpp-static)
      if(WIN32)
        set_property(TARGET slint_cpp-static APPEND PROPERTY
                     INTERFACE_LINK_LIBRARIES
                     opengl32 imm32 ws2_32 ntdll shlwapi userenv dbghelp)
      elseif(APPLE)
        # rustc --print native-static-libs, Slint 1.17.1 (winit, FemtoVG,
        # accessibility), libc / libSystem left to the toolchain.
        set_property(TARGET slint_cpp-static APPEND PROPERTY
                     INTERFACE_LINK_LIBRARIES
                     "-framework Accessibility" "-framework AppKit"
                     "-framework ApplicationServices" "-framework Carbon"
                     "-framework CloudKit" "-framework CoreData"
                     "-framework CoreFoundation" "-framework CoreGraphics"
                     "-framework CoreImage" "-framework CoreServices"
                     "-framework CoreText" "-framework CoreVideo"
                     "-framework Foundation" "-framework OpenGL"
                     "-framework QuartzCore" objc iconv)
        target_link_options(openswos-setup PRIVATE -Wl,-dead_strip)
      else()
        set_property(TARGET slint_cpp-static APPEND PROPERTY
                     INTERFACE_LINK_LIBRARIES fontconfig freetype)
        target_link_options(openswos-setup PRIVATE -Wl,--gc-sections)
      endif()
      if(MSVC)
        target_link_options(openswos-setup PRIVATE /IGNORE:4217)
        if(SD_STATIC_CRT)
          # Slint's Rust archive asks for the DLL CRT (/defaultlib:msvcrt);
          # refuse it so the static one (libcmt, libvcruntime, libucrt) serves
          # both, and the .exe needs no VC++ redistributable.
          target_link_options(openswos-setup PRIVATE /NODEFAULTLIB:msvcrt.lib
                              /NODEFAULTLIB:msvcprt.lib /NODEFAULTLIB:vcruntime.lib
                              /NODEFAULTLIB:ucrt.lib)
        endif()
      endif()
      message(STATUS "Slint is static -- the binary carries it, no runtime to ship")
    elseif(UNIX AND NOT APPLE AND TARGET slint_cpp-shared)
      # A shared Slint on Linux: find libslint_cpp.so beside the binary once
      # installed, and in the SDK while it is a build.
      set_target_properties(openswos-setup PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()

    install(TARGETS openswos-setup swosdata
      RUNTIME DESTINATION "." COMPONENT app
      BUNDLE DESTINATION "." COMPONENT app)
  else()
    message(STATUS "Slint is not available -- the CLI still builds, the window does not.")
  endif()
endif()
