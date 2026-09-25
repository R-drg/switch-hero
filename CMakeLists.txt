cmake_minimum_required(VERSION 3.20)
project(SwitchHero VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(SWITCH_HERO_BUILD_GAME "Build SDL game" ON)
if(EMSCRIPTEN)
  # Threads are used everywhere (mixer, loaders, background scans), so every
  # object is built for shared memory.
  add_compile_options(-pthread -fexceptions)
  add_link_options(-pthread -fexceptions)
endif()
add_library(switch_hero_core src/song.cpp src/enchor.cpp src/scores.cpp src/lang.cpp src/library.cpp)
target_include_directories(switch_hero_core PUBLIC include)
target_include_directories(switch_hero_core PRIVATE vendor)
target_compile_options(switch_hero_core PRIVATE -Wall -Wextra -Wpedantic)
if(SWITCH_HERO_BUILD_GAME)
  if(NINTENDO_SWITCH)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2>=2.0.18)
    set(FRET_SDL_TARGET PkgConfig::SDL2)
  elseif(EMSCRIPTEN)
    # SDL2, Vorbis and mpg123 come from Emscripten's ports.
    add_library(fret_sdl INTERFACE)
    target_compile_options(fret_sdl INTERFACE -sUSE_SDL=2)
    target_link_options(fret_sdl INTERFACE -sUSE_SDL=2)
    set(FRET_SDL_TARGET fret_sdl)
  else()
    find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 REQUIRED)
    find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0 libSDL2-2.0.so.0 REQUIRED)
    add_library(fret_sdl INTERFACE)
    target_include_directories(fret_sdl INTERFACE ${SDL2_INCLUDE_DIR})
    target_link_libraries(fret_sdl INTERFACE ${SDL2_LIBRARY})
    set(FRET_SDL_TARGET fret_sdl)
  endif()
  if(EMSCRIPTEN)
    add_executable(switch-hero src/main.cpp src/audio.cpp src/look.cpp src/downloader_web.cpp src/web.cpp)
  else()
    add_executable(switch-hero src/main.cpp src/audio.cpp src/look.cpp src/downloader.cpp)
  endif()
  # One version, from project() above: the title menu and the downloader's
  # User-Agent read it, so they cannot drift from the NRO's metadata.
  target_compile_definitions(switch-hero PRIVATE SWITCH_HERO_VERSION="${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
  target_include_directories(switch-hero PRIVATE vendor)
  target_link_libraries(switch-hero PRIVATE switch_hero_core ${FRET_SDL_TARGET})
  if(NINTENDO_SWITCH)
    pkg_check_modules(VORBIS REQUIRED IMPORTED_TARGET vorbisfile)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opusfile)
    pkg_check_modules(MPG123 REQUIRED IMPORTED_TARGET libmpg123)
    # libnx-backed curl: TLS goes through the console's own SSL service.
    pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
    # devkitPro portlibs are static; include all dependency link flags.
    target_link_libraries(switch-hero PRIVATE PkgConfig::VORBIS PkgConfig::OPUS PkgConfig::MPG123 PkgConfig::CURL
      ${SDL2_STATIC_LIBRARIES} ${VORBIS_STATIC_LIBRARIES} ${OPUS_STATIC_LIBRARIES} ${MPG123_STATIC_LIBRARIES}
      ${CURL_STATIC_LIBRARIES})
    nx_generate_nacp(OUTPUT switch-hero.nacp NAME "Switch Hero" AUTHOR "Rodrigo Ferreira" VERSION "${PROJECT_VERSION}")
    nx_create_nro(switch-hero NACP switch-hero.nacp ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg"
      ROMFS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
  elseif(EMSCRIPTEN)
    # Opus has no Emscripten port, so it and opusfile are built from source.
    enable_language(C)
    include(FetchContent)
    FetchContent_Declare(opus
      URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
      URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_Declare(opusfile
      URL https://github.com/xiph/opusfile/releases/download/v0.12/opusfile-0.12.tar.gz
      URL_HASH SHA256=118d8601c12dd6a44f52423e68ca9083cc9f2bfe72da7a8c1acb22a80ae3550b
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    # opusfile has no CMake build of its own, so this only unpacks it.
    FetchContent_MakeAvailable(opus opusfile)
    add_library(opusfile STATIC ${opusfile_SOURCE_DIR}/src/info.c ${opusfile_SOURCE_DIR}/src/internal.c
      ${opusfile_SOURCE_DIR}/src/opusfile.c ${opusfile_SOURCE_DIR}/src/stream.c)
    target_compile_options(opusfile PRIVATE -sUSE_OGG=1)
    target_include_directories(opusfile PUBLIC ${opusfile_SOURCE_DIR}/include)
    target_link_libraries(opusfile PUBLIC opus)
    # The game includes <opus/opusfile.h>, as the installed layout has it.
    file(COPY ${opusfile_SOURCE_DIR}/include/opusfile.h DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/web-include/opus)
    target_include_directories(switch-hero PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/web-include)
    set(FRET_WEB_PORTS -sUSE_OGG=1 -sUSE_VORBIS=1 -sUSE_MPG123=1)
    target_compile_options(switch-hero PRIVATE ${FRET_WEB_PORTS})
    target_link_libraries(switch-hero PRIVATE opusfile)
    set_target_properties(switch-hero PROPERTIES SUFFIX ".html")
    set_property(TARGET switch-hero APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    target_link_options(switch-hero PRIVATE ${FRET_WEB_PORTS}
      # The game runs its own main loop; ASYNCIFY lets it hand each frame back
      # to the browser (web::endFrame) without restructuring it.
      -sASYNCIFY -sASYNCIFY_STACK_SIZE=131072
      -sSTACK_SIZE=4MB -sDEFAULT_PTHREAD_STACK_SIZE=2MB
      # Workers are made ahead of time: the main thread cannot start one while
      # it waits on a thread, and loaders, scans and texture painting all
      # overlap at start-up.
      -sPTHREAD_POOL_SIZE=16
      # Growth with threads only slows the JS glue's heap access, which is
      # light here: songs live in the JS-side file system, not the heap.
      -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -Wno-pthreads-mem-growth
      -sENVIRONMENT=web,worker
      -sFORCE_FILESYSTEM=1 -lidbfs.js
      -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,addRunDependency,removeRunDependency,callMain
      -sINVOKE_RUN=0
      # SHELL: keeps CMake from merging the repeated flags into one.
      "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/songs/First-Light@/demo/First-Light")
    add_custom_command(TARGET switch-hero POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/web/coi-sw.js $<TARGET_FILE_DIR:switch-hero>/coi-sw.js
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:switch-hero>/fonts
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/BlackOpsOne-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/RussoOne-Regular.ttf $<TARGET_FILE_DIR:switch-hero>/fonts/)
  else()
    find_package(Threads REQUIRED)
    find_package(CURL REQUIRED)
    target_link_libraries(switch-hero PRIVATE CURL::libcurl)
    find_path(SNDFILE_INCLUDE_DIR sndfile.h REQUIRED)
    find_library(SNDFILE_LIBRARY NAMES sndfile libsndfile.so.1 REQUIRED)
    add_library(fret_sndfile INTERFACE)
    target_include_directories(fret_sndfile INTERFACE ${SNDFILE_INCLUDE_DIR})
    target_link_libraries(fret_sndfile INTERFACE ${SNDFILE_LIBRARY})
    target_link_libraries(switch-hero PRIVATE fret_sndfile Threads::Threads)
  endif()
endif()
if(NOT NINTENDO_SWITCH AND NOT EMSCRIPTEN)
  enable_testing()
  add_executable(guitar_tests tests/guitar_tests.cpp)
  target_include_directories(guitar_tests PRIVATE include integrations/missioncontrol)
  add_test(NAME guitar COMMAND guitar_tests)
  add_executable(timing_tests tests/timing_tests.cpp)
  target_link_libraries(timing_tests PRIVATE switch_hero_core)
  add_test(NAME timing COMMAND timing_tests)
  add_executable(enchor_tests tests/enchor_tests.cpp)
  target_link_libraries(enchor_tests PRIVATE switch_hero_core)
  add_test(NAME enchor COMMAND enchor_tests)
  add_executable(core_tests tests/core_tests.cpp)
  target_link_libraries(core_tests PRIVATE switch_hero_core)
  add_executable(multiplayer_tests tests/multiplayer_tests.cpp)
  target_link_libraries(multiplayer_tests PRIVATE switch_hero_core)
  add_test(NAME multiplayer COMMAND multiplayer_tests)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME fixtures COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/make_fixtures.py ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
  set_tests_properties(fixtures PROPERTIES FIXTURES_SETUP song_fixtures)
  add_test(NAME core COMMAND core_tests ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
  set_tests_properties(core PROPERTIES FIXTURES_REQUIRED song_fixtures)
  if(SWITCH_HERO_BUILD_GAME)
    add_executable(audio_tests tests/audio_tests.cpp src/audio.cpp)
    target_link_libraries(audio_tests PRIVATE switch_hero_core ${FRET_SDL_TARGET} fret_sndfile Threads::Threads)
    add_test(NAME audio COMMAND audio_tests ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
    # The shipped gem sprites must match the renderer that made them.
    add_test(NAME gems COMMAND switch-hero --check-gems WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(audio PROPERTIES FIXTURES_REQUIRED song_fixtures ENVIRONMENT "SDL_AUDIODRIVER=dummy")
  endif()
endif()
