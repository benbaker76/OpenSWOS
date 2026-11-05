# The phone builds of OpenSWOS (docs/MOBILE.md): the same game as the Makefile's SDL build, for
# Android (libmain.so, loaded by SDL's Java activity: packaging/android) and iOS (an app bundle,
# built on a Mac: scripts/build_ios.sh). The desktops and the web stay on the Makefile; this
# mirrors its SDL build's sources and flags -- keep the two in step.
#
# SDL2 is built from its release (2.30.12, the one the macOS app bundles), and GGPO-X and ENET
# from third_party, so a phone plays online as a desktop does.
cmake_minimum_required(VERSION 3.22)
project(OpenSWOS C CXX)
if(IOS)
    enable_language(OBJC)               # (SDL's UIKit backend)
endif()

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
get_filename_component(REPO ${REPO} ABSOLUTE)

option(SWOS_TEST_RULES "a tester build (the Makefile's SWOS_TEST_RULES)" ON)

# The build stamp (include/swos_build.h: the commit, -dirty), as both makefiles write it.
execute_process(COMMAND sh scripts/gen_build_info.sh WORKING_DIRECTORY ${REPO})

# The identity (include/project_identity.h): the app id, the name.
file(READ ${REPO}/include/project_identity.h IDENTITY)
string(REGEX MATCH "#define PROJECT_APP_ID +\"([^\"]+)\"" _ "${IDENTITY}")
set(APP_ID ${CMAKE_MATCH_1})
string(REGEX MATCH "#define PROJECT_NAME +\"([^\"]+)\"" _ "${IDENTITY}")
set(APP_NAME ${CMAKE_MATCH_1})
string(REGEX MATCH "#define PROJECT_URL_SCHEME +\"([^\"]+)\"" _ "${IDENTITY}")
set(SWOS_URL_SCHEME ${CMAKE_MATCH_1})          # (packaging/ios/Info.plist.in: the setup's link)

# ---- SDL2 ----
include(FetchContent)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
if(IOS)                                 # an app is one binary there: SDL goes inside it
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
else()                                  # Android: libSDL2.so, which SDL's Java loads by name
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(SDL2
    URL      https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz
    URL_HASH SHA256=ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea)
FetchContent_MakeAvailable(SDL2)

# ---- the game: the Makefile's SDL build ----
# CONFIGURE_DEPENDS: the glob is checked again at every build. Without it a source added to
# src/ after the first configure was never compiled into a build tree that already existed
# (Gradle keeps its .cxx): the APK of 4708b6ab failed to link, CalLabel_Fit undefined.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${REPO}/src/*.c ${REPO}/src/generated/*.c)

set(GGPO_DIR ${REPO}/third_party/ggpo-x/src)
file(GLOB GGPO_SOURCES CONFIGURE_DEPENDS ${GGPO_DIR}/lib/ggpo/*.cpp ${GGPO_DIR}/lib/ggpo/network/*.cpp
                       ${GGPO_DIR}/lib/ggpo/backends/*.cpp)
list(FILTER GGPO_SOURCES EXCLUDE REGEX "platform_windows\\.cpp$")
set(ENET_DIR ${REPO}/third_party/enet)
set(ENET_SOURCES ${ENET_DIR}/callbacks.c ${ENET_DIR}/compress.c ${ENET_DIR}/host.c ${ENET_DIR}/list.c
                 ${ENET_DIR}/packet.c ${ENET_DIR}/peer.c ${ENET_DIR}/protocol.c ${ENET_DIR}/unix.c)
set(ALL_SOURCES ${GAME_SOURCES} ${GGPO_SOURCES} ${ENET_SOURCES} ${REPO}/src/netplay_netem_install.cpp)

if(ANDROID)
    add_library(main SHARED ${ALL_SOURCES})
    set(GAME main)
else()
    add_executable(OpenSWOS MACOSX_BUNDLE ${ALL_SOURCES})
    set(GAME OpenSWOS)
endif()

target_include_directories(${GAME} PRIVATE ${REPO}/src ${REPO}/include ${GGPO_DIR}/include ${ENET_DIR}/include)
# (GGPO-X's own folders for its C++ only, as the Makefile's GGPO_CXXFLAGS: in C, its poll.h would
#  stand in for the system's)
target_include_directories(${GAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${GGPO_DIR}/lib/ggpo
    ${GGPO_DIR}/lib/ggpo/network ${GGPO_DIR}/lib/ggpo/backends>)
target_compile_definitions(${GAME} PRIVATE
    COMPETITION_DATA_C=1 TACTICS_DATA_C=1 FIXTURE_DATA_C=1 SIMULATION_DATA_C=1 PLAYER_DATA_C=1
    RENDER_DATA_C=1 PALETTE_DATA_C=1 STRINGS_DATA_C=1 GENERAL_DATA_C=1
    SWOS_WORLDS=1 SWOS_ONLINE_CLIENT=0 SWOS_SAVE_FORMAT_JSON=1 SWOS_NETPLAY=1
    $<$<BOOL:${SWOS_TEST_RULES}>:SWOS_TEST_RULES=1>)
set(C_FLAGS -O2 -g -std=gnu11 -fno-strict-aliasing -fno-builtin
    -include ${REPO}/include/swos_platform.h
    -Wall -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable
    -Wno-unused-but-set-variable -Wno-attributes -Wno-multichar -Wno-pointer-sign
    -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-braces
    -Wno-address-of-packed-member -Wno-strict-aliasing -Wno-array-bounds
    -Wno-incompatible-function-pointer-types -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
    -Wno-implicit-function-declaration)
target_compile_options(${GAME} PRIVATE $<$<COMPILE_LANGUAGE:C>:${C_FLAGS}>)
set_source_files_properties(${ENET_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-w;-DHAS_SOCKLEN_T=1;-DHAS_POLL=1;-DHAS_FCNTL=1;-DHAS_INET_PTON=1;-DHAS_INET_NTOP=1;-DHAS_MSGHDR_FLAGS=1")
target_compile_options(${GAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-O2 -g -std=gnu++17 -w -DGGPO_SEND_FRAME_ADVANTAGE=0>)
# (the one C++ file of ours: the game's platform header too, as the Makefile's rule for it)
set_source_files_properties(${REPO}/src/netplay_netem_install.cpp PROPERTIES COMPILE_OPTIONS
    "-include;${REPO}/include/swos_platform.h")

if(ANDROID)
    target_link_libraries(${GAME} PRIVATE SDL2::SDL2 m log)
else()
    target_link_libraries(${GAME} PRIVATE SDL2::SDL2main SDL2::SDL2-static)
    # The app: its Info.plist (packaging/ios), its icons, and the project's own data -- the
    # tracked files of bin/ every drop ships (scripts/stage_runtime.sh) -- in the bundle, which
    # is the game's working folder (src/mobile.c). Never the original game's data.
    execute_process(COMMAND git rev-list --count HEAD WORKING_DIRECTORY ${REPO}
                    OUTPUT_VARIABLE APP_BUILD OUTPUT_STRIP_TRAILING_WHITESPACE)
    file(GLOB IOS_ICONS ${REPO}/packaging/ios/icons/*.png)
    target_sources(${GAME} PRIVATE ${IOS_ICONS} ${REPO}/packaging/ios/app_delegate.m)
    set_source_files_properties(${IOS_ICONS} PROPERTIES MACOSX_PACKAGE_LOCATION .)
    set_target_properties(${GAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${REPO}/packaging/ios/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME ${APP_NAME}
        MACOSX_BUNDLE_GUI_IDENTIFIER ${APP_ID}
        MACOSX_BUNDLE_SHORT_VERSION_STRING 1.0.${APP_BUILD}
        MACOSX_BUNDLE_BUNDLE_VERSION ${APP_BUILD}
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
    add_custom_command(TARGET ${GAME} POST_BUILD
        COMMAND bash ${REPO}/scripts/stage_runtime.sh $<TARGET_BUNDLE_DIR:${GAME}>
                data grafs grafs_aga grafs_dos grafs_2020 grafs_arcade grafs_jag sound sound_dos data_json mods
        WORKING_DIRECTORY ${REPO} VERBATIM)
    # The packed structs (game_types.h, the Amiga's layouts) hold pointers at unaligned offsets.
    # iOS 15's chained fixups cannot express those ("pointer not aligned in _cupWinnersCup+0x1C"),
    # and the classic linker the Makefile's macOS link uses is gone from current Xcode; the older
    # fixup format takes them, and arm64 loads an unaligned pointer correctly.
    target_link_options(${GAME} PRIVATE -Wl,-no_fixup_chains)
endif()
