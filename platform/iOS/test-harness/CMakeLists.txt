# Desktop (Linux) test harness for the iOS OpenGL path.
#
# Builds NotBlood the way the iOS app renders (Polymost through gl4es on an OpenGL ES 2.0
# context that reports the iPad's capabilities, see source/blood/src/nbtest.cpp), using
# SDL's offscreen video driver, so rendering problems can be reproduced and screenshotted
# without a device:
#
#   (apply platform/iOS/gl4es/*.patch to the gl4es tree first, as the iOS workflow does)
#   cmake -S platform/iOS/test-harness -B build-harness -G Ninja \
#         -DSDL2_DIR=<SDL2 install>/lib/cmake/SDL2 -DGL4ES_SOURCE_DIR=<gl4es>
#   cmake --build build-harness
#   cd <folder with Blood data + notblood.pk3>
#   SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
#   NBTEST_SCRIPT="90:shot:menu;120:quit" <build-harness>/notblood-harness -usecwd

cmake_minimum_required(VERSION 3.22)
project(NotBloodHarness LANGUAGES C CXX)

set(GL4ES_SOURCE_DIR "" CACHE PATH "Path to a gl4es source tree")
find_package(SDL2 REQUIRED CONFIG)

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(SRC "${ROOT}/source")

set(NB_DEFINES RENDERTYPESDL=1 SDL_TARGET=2 SDL_MAIN_HANDLED USE_OPENGL NOASM HAVE_VORBIS HAVE_XMP
    NOONE_EXTENSIONS NDEBUG _FORTIFY_SOURCE=0 NOTBLOOD_GL4ES_TEST)
set(NB_INCLUDES "${SRC}/build/include" "${SRC}/mact/include" "${SRC}/audiolib/include" "${SRC}/glad/include"
    "${SRC}/imgui/include" "${SRC}/libsmackerdec/include" "${SRC}/hmpplay/include")
set(NB_FLAGS -funsigned-char -fno-strict-aliasing -frounding-math -O1 -g -fno-exceptions -fno-rtti
    -fno-threadsafe-statics -w)

# gl4es (desktop GL 2.1 on GLES 2.0), configured like the iOS build
file(GLOB GL4ES_SOURCES "${GL4ES_SOURCE_DIR}/src/gl/*.c" "${GL4ES_SOURCE_DIR}/src/gl/wrap/*.c" "${GL4ES_SOURCE_DIR}/src/gl/math/*.c")
list(APPEND GL4ES_SOURCES "${GL4ES_SOURCE_DIR}/src/glx/hardext.c")
add_library(nb_gl4es STATIC ${GL4ES_SOURCES})
target_compile_definitions(nb_gl4es PRIVATE NOX11 EGL_NO_X11 NOEGL STATICLIB NO_GBM NO_INIT_CONSTRUCTOR DEFAULT_ES=2)
target_include_directories(nb_gl4es PRIVATE "${GL4ES_SOURCE_DIR}/include" "${GL4ES_SOURCE_DIR}/src" "${GL4ES_SOURCE_DIR}/src/glx")
target_compile_options(nb_gl4es PRIVATE -std=gnu11 -O1 -g -w)

file(GLOB ENGINE_SOURCES "${SRC}/build/src/*.c" "${SRC}/build/src/*.cpp")
foreach(f build.cpp config.cpp sdlayer12.cpp sdlkeytrans.cpp startgtk.editor.cpp startwin.editor.cpp gtkbits.cpp
          dynamicgtk.cpp winbits.cpp winlayer.cpp rawinput.cpp wiibits.cpp polymer.cpp animvpx.cpp)
    list(REMOVE_ITEM ENGINE_SOURCES "${SRC}/build/src/${f}")
endforeach()
foreach(shader polymost1Frag polymost1Vert)
    file(READ "${SRC}/build/src/${shader}.glsl" shader_text)
    set(shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.glsl.cpp")
    file(WRITE "${shader_out}" "extern char const *${shader};\nchar const *${shader} = R\"shader(${shader_text})shader\";\n")
    list(APPEND ENGINE_SOURCES "${shader_out}")
endforeach()
list(APPEND ENGINE_SOURCES "${SRC}/glad/src/glad.c")

file(GLOB MACT_SOURCES "${SRC}/mact/src/*.cpp")
file(GLOB AUDIOLIB_SOURCES "${SRC}/audiolib/src/*.cpp")
foreach(f music_external.cpp driver_directsound.cpp driver_winmm.cpp driver_alsa.cpp)
    list(REMOVE_ITEM AUDIOLIB_SOURCES "${SRC}/audiolib/src/${f}")
endforeach()
file(GLOB XMP_SOURCES "${SRC}/libxmp-lite/src/*.c")
file(GLOB SMACKER_SOURCES "${SRC}/libsmackerdec/src/*.cpp")
set(IMGUI_SOURCES imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_impl_sdl2.cpp imgui_impl_opengl3.cpp)
list(TRANSFORM IMGUI_SOURCES PREPEND "${SRC}/imgui/src/")

set(BLOOD_SOURCES
    blood.cpp actor.cpp ai.cpp aibat.cpp aibeast.cpp aiboneel.cpp aiburn.cpp aicaleb.cpp aicerber.cpp
    aicult.cpp aigarg.cpp aighost.cpp aigilbst.cpp aihand.cpp aihound.cpp aiinnoc.cpp aipod.cpp airat.cpp
    aispid.cpp aitchern.cpp aizomba.cpp aizombf.cpp asound.cpp barf.cpp callback.cpp chatpipe.cpp choke.cpp
    common.cpp config.cpp controls.cpp credits.cpp db.cpp demo.cpp dude.cpp endgame.cpp eventq.cpp fire.cpp
    fx.cpp gamemenu.cpp gameutil.cpp getopt.cpp gfx.cpp gib.cpp globals.cpp gui.cpp inifile.cpp iob.cpp
    levels.cpp loadsave.cpp map2d.cpp menu.cpp messages.cpp mirrors.cpp misc.cpp network.cpp osdcmd.cpp
    player.cpp qav.cpp qheap.cpp replace.cpp resource.cpp screen.cpp sectorfx.cpp seq.cpp sfx.cpp sound.cpp
    tile.cpp trig.cpp triggers.cpp view.cpp warp.cpp weapon.cpp weather.cpp
    nnextsif.cpp nnexts.cpp nnextstr.cpp nnextcitem.cpp nnextslaser.cpp nnextcdud.cpp aicdud.cpp aipatrol.cpp
    nbtest.cpp)
list(TRANSFORM BLOOD_SOURCES PREPEND "${SRC}/blood/src/")

set(ALL_CXX_AS_C ${XMP_SOURCES} "${SRC}/glad/src/glad.c")
file(GLOB ENGINE_C_SOURCES "${SRC}/build/src/*.c")
list(APPEND ALL_CXX_AS_C ${ENGINE_C_SOURCES})
set_source_files_properties(${ALL_CXX_AS_C} PROPERTIES LANGUAGE CXX)

add_executable(notblood-harness ${ENGINE_SOURCES} ${MACT_SOURCES} ${AUDIOLIB_SOURCES} ${XMP_SOURCES}
    ${SMACKER_SOURCES} "${SRC}/hmpplay/src/hmpplay.cpp" "${SRC}/hmpplay/src/fmmidi3.cpp" ${IMGUI_SOURCES} ${BLOOD_SOURCES}
    "${SRC}/blood/rsrc/game_icon.c" "${CMAKE_CURRENT_SOURCE_DIR}/harness_stubs.cpp")
set_source_files_properties("${SRC}/blood/rsrc/game_icon.c" PROPERTIES LANGUAGE CXX)
target_compile_definitions(notblood-harness PRIVATE ${NB_DEFINES})
set_source_files_properties(${XMP_SOURCES} PROPERTIES COMPILE_DEFINITIONS
    "HAVE_ROUND;LIBXMP_CORE_PLAYER;LIBXMP_NO_PROWIZARD;LIBXMP_NO_DEPACKERS;BUILDING_STATIC")
target_include_directories(notblood-harness PRIVATE ${NB_INCLUDES} "${SRC}/blood/src" "${SRC}/audiolib/src" "${SRC}/libxmp-lite/include"
    "${SRC}/libxmp-lite/include/libxmp-lite")
target_compile_options(notblood-harness PRIVATE ${NB_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++14>)
target_link_libraries(notblood-harness PRIVATE nb_gl4es SDL2::SDL2-static dl pthread m rt)
