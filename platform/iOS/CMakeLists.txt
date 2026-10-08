# NotBlood for iOS / iPadOS
#
# Generates an Xcode project (software renderer + SDL2 Metal presentation + touch controls).
# See platform/iOS/README.md and .github/workflows/ios.yml.
#
#   cmake -S platform/iOS -B build-ios -G Xcode \
#         -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
#         -DSDL2_SOURCE_DIR=/path/to/SDL2-2.32.x
#   cmake --build build-ios --config Release -- -sdk iphoneos CODE_SIGNING_ALLOWED=NO

cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "Configure with -DCMAKE_SYSTEM_NAME=iOS (and -G Xcode)")
endif()

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "")

project(NotBlood LANGUAGES C CXX OBJC OBJCXX)

set(NOTBLOOD_BUNDLE_ID "com.notblood.ios" CACHE STRING "Bundle identifier (sideloading tools usually rewrite it)")
set(NOTBLOOD_VERSION "1.0" CACHE STRING "CFBundleShortVersionString")
set(NOTBLOOD_BUILD "1" CACHE STRING "CFBundleVersion")
set(SDL2_SOURCE_DIR "" CACHE PATH "Path to an SDL 2.x source tree")

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SRC "${ROOT}/source")

#
# SDL2 (static, built from source for iphoneos)
#
if(NOT SDL2_SOURCE_DIR OR NOT EXISTS "${SDL2_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set SDL2_SOURCE_DIR to an SDL 2.x source tree")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
add_subdirectory("${SDL2_SOURCE_DIR}" SDL2 EXCLUDE_FROM_ALL)

#
# shared compile settings (mirrors Common.mak for: SDL2, classic renderer only, no asm, netcode on)
#
set(NB_DEFINES
    RENDERTYPESDL=1
    SDL_TARGET=2
    SDL_MAIN_HANDLED
    NOASM
    HAVE_VORBIS
    HAVE_XMP
    NOONE_EXTENSIONS
    NDEBUG
    _FORTIFY_SOURCE=0
)

set(NB_INCLUDES
    "${SRC}/build/include"
    "${SRC}/mact/include"
    "${SRC}/audiolib/include"
    "${SRC}/glad/include"
    "${SRC}/imgui/include"
    "${SRC}/libsmackerdec/include"
    "${SRC}/hmpplay/include"
)

# The C++-only flags are harmless on the few C/ObjC files (just unused-argument noise),
# and keeping one flag set avoids per-language generator expressions in the Xcode generator.
set(NB_C_FLAGS -funsigned-char -fno-strict-aliasing -frounding-math -O2
    -fno-exceptions -fno-rtti -fno-threadsafe-statics
    -Wno-everything -Wno-unused-command-line-argument)

function(nb_library name)
    add_library(${name} STATIC ${ARGN})
    target_compile_definitions(${name} PRIVATE ${NB_DEFINES})
    target_include_directories(${name} PRIVATE ${NB_INCLUDES})
    target_compile_options(${name} PRIVATE ${NB_C_FLAGS})
    set_target_properties(${name} PROPERTIES
        C_STANDARD 11 CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC NO)
    target_link_libraries(${name} PRIVATE SDL2::SDL2-static)
endfunction()

#
# engine (source/build)
#
file(GLOB ENGINE_SOURCES "${SRC}/build/src/*.c" "${SRC}/build/src/*.cpp")
set(ENGINE_EXCLUDE
    # editor / other frontends
    build.cpp config.cpp sdlayer12.cpp sdlkeytrans.cpp
    startgtk.editor.cpp startwin.editor.cpp
    gtkbits.cpp dynamicgtk.cpp winbits.cpp winlayer.cpp rawinput.cpp wiibits.cpp
    # OpenGL renderers (iOS has no desktop GL; classic renderer is used)
    glbuild.cpp glsurface.cpp mdsprite.cpp polymer.cpp polymost.cpp tilepacker.cpp voxmodel.cpp
    animvpx.cpp
)
foreach(f ${ENGINE_EXCLUDE})
    list(REMOVE_ITEM ENGINE_SOURCES "${SRC}/build/src/${f}")
endforeach()
list(APPEND ENGINE_SOURCES "${SRC}/build/src/iosbits.mm")

# upstream builds its .c files as C++ (CPLUSPLUS=1 in Common.mak)
file(GLOB ENGINE_C_SOURCES "${SRC}/build/src/*.c")
set_source_files_properties(${ENGINE_C_SOURCES} PROPERTIES LANGUAGE CXX)

nb_library(nb_engine ${ENGINE_SOURCES})
target_link_libraries(nb_engine PRIVATE nb_imgui)

#
# imgui (sdlayer uses the SDL2 backend)
#
nb_library(nb_imgui
    "${SRC}/imgui/src/imgui.cpp"
    "${SRC}/imgui/src/imgui_demo.cpp"
    "${SRC}/imgui/src/imgui_draw.cpp"
    "${SRC}/imgui/src/imgui_tables.cpp"
    "${SRC}/imgui/src/imgui_widgets.cpp"
    "${SRC}/imgui/src/imgui_impl_sdl2.cpp"
)

#
# mact, audiolib, codecs
#
file(GLOB MACT_SOURCES "${SRC}/mact/src/*.cpp")
nb_library(nb_mact ${MACT_SOURCES})

file(GLOB AUDIOLIB_SOURCES "${SRC}/audiolib/src/*.cpp")
foreach(f music_external.cpp driver_directsound.cpp driver_winmm.cpp driver_alsa.cpp)
    list(REMOVE_ITEM AUDIOLIB_SOURCES "${SRC}/audiolib/src/${f}")
endforeach()
nb_library(nb_audiolib ${AUDIOLIB_SOURCES})
target_include_directories(nb_audiolib PRIVATE "${SRC}/libxmp-lite/include")

file(GLOB XMP_SOURCES "${SRC}/libxmp-lite/src/*.c")
set_source_files_properties(${XMP_SOURCES} PROPERTIES LANGUAGE CXX)
nb_library(nb_xmp ${XMP_SOURCES})
target_compile_definitions(nb_xmp PRIVATE HAVE_ROUND LIBXMP_CORE_PLAYER LIBXMP_NO_PROWIZARD LIBXMP_NO_DEPACKERS BUILDING_STATIC)
target_include_directories(nb_xmp PRIVATE "${SRC}/libxmp-lite/include/libxmp-lite")

nb_library(nb_smacker
    "${SRC}/libsmackerdec/src/BitReader.cpp"
    "${SRC}/libsmackerdec/src/FileStream.cpp"
    "${SRC}/libsmackerdec/src/HuffmanVLC.cpp"
    "${SRC}/libsmackerdec/src/LogError.cpp"
    "${SRC}/libsmackerdec/src/SmackerDecoder.cpp"
)

nb_library(nb_hmpplay
    "${SRC}/hmpplay/src/hmpplay.cpp"
    "${SRC}/hmpplay/src/fmmidi3.cpp"
)

#
# game
#
set(BLOOD_SOURCES
    blood.cpp actor.cpp ai.cpp aibat.cpp aibeast.cpp aiboneel.cpp aiburn.cpp aicaleb.cpp aicerber.cpp
    aicult.cpp aigarg.cpp aighost.cpp aigilbst.cpp aihand.cpp aihound.cpp aiinnoc.cpp aipod.cpp airat.cpp
    aispid.cpp aitchern.cpp aizomba.cpp aizombf.cpp asound.cpp barf.cpp callback.cpp chatpipe.cpp choke.cpp
    common.cpp config.cpp controls.cpp credits.cpp db.cpp demo.cpp dude.cpp endgame.cpp eventq.cpp fire.cpp
    fx.cpp gamemenu.cpp gameutil.cpp getopt.cpp gfx.cpp gib.cpp globals.cpp gui.cpp inifile.cpp iob.cpp
    levels.cpp loadsave.cpp map2d.cpp menu.cpp messages.cpp mirrors.cpp misc.cpp network.cpp osdcmd.cpp
    player.cpp qav.cpp qheap.cpp replace.cpp resource.cpp screen.cpp sectorfx.cpp seq.cpp sfx.cpp sound.cpp
    tile.cpp trig.cpp triggers.cpp view.cpp warp.cpp weapon.cpp weather.cpp
    # NOONE_EXTENSIONS
    nnextsif.cpp nnexts.cpp nnextstr.cpp nnextcitem.cpp nnextslaser.cpp nnextcdud.cpp aicdud.cpp aipatrol.cpp
    # iOS
    touchcontrols.cpp
)
list(TRANSFORM BLOOD_SOURCES PREPEND "${SRC}/blood/src/")

set(APP_RESOURCES
    "${ROOT}/notblood.pk3"
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets"
)
set_source_files_properties(${APP_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

add_executable(NotBlood MACOSX_BUNDLE ${BLOOD_SOURCES} ${APP_RESOURCES})
target_compile_definitions(NotBlood PRIVATE ${NB_DEFINES})
target_include_directories(NotBlood PRIVATE ${NB_INCLUDES} "${SRC}/blood/src")
target_compile_options(NotBlood PRIVATE ${NB_C_FLAGS})
set_target_properties(NotBlood PROPERTIES
    CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "NotBlood"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${NOTBLOOD_BUNDLE_ID}"
    XCODE_ATTRIBUTE_MARKETING_VERSION "${NOTBLOOD_VERSION}"
    XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${NOTBLOOD_BUILD}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING "YES"
    XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "NO"
)

target_link_libraries(NotBlood PRIVATE
    nb_engine nb_mact nb_audiolib nb_xmp nb_smacker nb_hmpplay nb_imgui
    SDL2::SDL2-static
    "-framework AVFoundation"
    "-framework AudioToolbox"
    "-framework CoreAudio"
    "-framework CoreBluetooth"
    "-framework CoreFoundation"
    "-framework CoreGraphics"
    "-framework CoreHaptics"
    "-framework CoreMotion"
    "-framework Foundation"
    "-framework GameController"
    "-framework Metal"
    "-framework OpenGLES"
    "-framework QuartzCore"
    "-framework UIKit"
)
