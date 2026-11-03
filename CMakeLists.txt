cmake_minimum_required(VERSION 3.20)
project(ps2-native-recomp CXX)

# Only clang builds the project (Clang, AppleClang, llvm-mingw; emcc is clang too): the vector units use clang builtins such as
# __builtin_elementwise_min and __builtin_reduce_or (ps2/vu_ops.hpp) that GCC and MSVC lack. Stop here with a clear message
# instead of hundreds of compile errors later. scripts/cxx.sh finds a clang++.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ps2-native-recomp needs clang++ (Clang, AppleClang, llvm-mingw or Emscripten's emcc), but the C++ compiler "
        "is ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER}). The code uses clang builtins that other compilers lack. "
        "Install clang and configure again with -DCMAKE_CXX_COMPILER=clang++ in a new build folder (scripts/cxx.sh picks one).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PS2_GENERATED "" CACHE PATH "Folder with the C++ generated for a game")
set(PS2_RUNTIME_LIB "" CACHE FILEPATH "libps2runtime.a built earlier from this source tree (scripts/runtime.sh); a game links it instead of compiling the runtime again")

if(WIN32)
    add_link_options(-static-libgcc -static-libstdc++)
endif()

# iOS (docs/ios.md): configured by scripts/port_ios.sh with -G Xcode -DCMAKE_SYSTEM_NAME=iOS on a Mac. Only the runtime and the game are
# built for the phone: the tools (eerecomp, vurecomp) run on the Mac, and an iOS app may not compile or load code while it runs, so the
# live compiling of the desktop runner is left out (PS2_NO_LIVE) and the game is built with the code its desktop runs found.
set(PS2_IOS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PS2_IOS ON)
    option(PS2_IOS_SIMULATOR "Build for the iOS Simulator instead of a device" OFF)
    # the PS2's FPU rounds every step: no fused multiply-add (arm64 clang fuses by default), as the desktop builds with -ffp-contract=off
    add_compile_options(-ffp-contract=off)
endif()

if(EMSCRIPTEN)
    add_compile_options(-msimd128 -mtail-call -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0 -pthread)
    add_link_options(-msimd128 -mtail-call -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0 -pthread)
endif()

if(NOT EMSCRIPTEN AND NOT PS2_IOS)
    add_executable(eerecomp
        recomp/main.cpp
        recomp/r5900.cpp
        recomp/elf.cpp
        recomp/analysis.cpp
        recomp/codegen.cpp
        recomp/eerecomp_gen.cpp
        recomp/snr2.cpp)
    target_include_directories(eerecomp PRIVATE runtime/include)
    target_compile_options(eerecomp PRIVATE -Wall -Wextra)

    add_executable(vujit_dump recomp/vujit_dump.cpp runtime/src/vu_jit.cpp)
    target_include_directories(vujit_dump PRIVATE runtime/include runtime/src)
    add_executable(vurecomp recomp/vurecomp.cpp recomp/vurecomp_gen.cpp)
    target_include_directories(vurecomp PRIVATE runtime/include)
    target_compile_options(vurecomp PRIVATE -Wall -Wextra)
endif()

if(PS2_RUNTIME_LIB AND NOT EMSCRIPTEN)
    set(PS2_PREBUILT ON)
    set(PS2_API INTERFACE)
    add_library(ps2runtime STATIC IMPORTED)
    set_target_properties(ps2runtime PROPERTIES IMPORTED_LOCATION "${PS2_RUNTIME_LIB}")
else()
    set(PS2_PREBUILT OFF)
    set(PS2_API PUBLIC)
    add_library(ps2runtime STATIC
        runtime/src/memory.cpp
        runtime/src/dispatch.cpp
        runtime/src/system.cpp
        runtime/src/kernel.cpp
        runtime/src/hw.cpp
        runtime/src/cop.cpp
        runtime/src/disc.cpp
        runtime/src/dmac.cpp
        runtime/src/vif.cpp
        runtime/src/vu_micro.cpp
        runtime/src/vu_jit.cpp
        runtime/src/vu_jit_web.cpp
        runtime/src/live_build.cpp
        runtime/src/vu_live.cpp
        runtime/src/ee_live.cpp
        runtime/src/ipu/ipu.cpp
        runtime/src/gs/gs.cpp
        runtime/src/gs/gif.cpp
        runtime/src/gs/queue.cpp
        runtime/src/gs/raster.cpp
        runtime/src/iop/cpu.cpp
        runtime/src/iop/kernel.cpp
        runtime/src/iop/libs.cpp
        runtime/src/iop/hw.cpp
        runtime/src/iop/spu2.cpp
        runtime/src/iop/sio2.cpp
        runtime/src/iop/io.cpp
        runtime/src/iop/cdvd.cpp
        runtime/src/iop/sif.cpp
        runtime/src/iop/cdvdfsv.cpp)
    target_compile_options(ps2runtime PRIVATE -Wall -Wextra)
    if(PS2_IOS)
        target_compile_definitions(ps2runtime PUBLIC PS2_IOS PS2_NO_LIVE)
    endif()
    if(NOT EMSCRIPTEN AND NOT PS2_IOS)
        # Vector programs the build does not know are compiled while the game runs (runtime/src/vu_live.cpp, docs/vu-live.md): the
        # generator is the one of the vurecomp tool, and the headers its output includes are embedded in the runner.
        target_sources(ps2runtime PRIVATE recomp/vurecomp_gen.cpp)
        # Main processor code that the build does not know is translated, compiled and loaded while the game runs too
        # (runtime/src/ee_live.cpp, docs/ee-live.md): the translator is the one of the eerecomp tool, and the headers its output includes
        # are embedded in the runner as well.
        target_sources(ps2runtime PRIVATE recomp/eerecomp_gen.cpp recomp/codegen.cpp recomp/analysis.cpp recomp/r5900.cpp recomp/elf.cpp recomp/snr2.cpp)
        target_include_directories(ps2runtime PRIVATE recomp)
        foreach(entry "cpu:Cpu" "fpu:Fpu" "vu:Vu" "vu_ops:VuOps" "memory:Memory" "mmi:Mmi" "generated:Generated" "ee_live_abi:EeLiveAbi")
            string(REPLACE ":" ";" pair "${entry}")
            list(GET pair 0 hdr)
            list(GET pair 1 sym)
            set(live_hdr "${CMAKE_BINARY_DIR}/live_hdr_${hdr}.cpp")
            add_custom_command(OUTPUT "${live_hdr}"
                COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/runtime/include/ps2/${hdr}.hpp -DOUT=${live_hdr} -DNAME=kLiveHdr${sym} -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
                DEPENDS runtime/include/ps2/${hdr}.hpp cmake/embed.cmake)
            target_sources(ps2runtime PRIVATE "${live_hdr}")
        endforeach()
        # the compiler this runner was built with is the last place vu_live.cpp looks for one
        set_property(SOURCE runtime/src/live_build.cpp APPEND PROPERTY COMPILE_DEFINITIONS "PS2_LIVE_DEFAULT_CXX=\"${CMAKE_CXX_COMPILER}\"")
    endif()
endif()
if(PS2_IOS AND PS2_PREBUILT)
    target_compile_definitions(ps2runtime INTERFACE PS2_IOS PS2_NO_LIVE)
endif()
if(NOT WIN32 AND NOT EMSCRIPTEN AND NOT PS2_IOS)
    target_link_libraries(ps2runtime ${PS2_API} ${CMAKE_DL_LIBS})  # vu_live.cpp loads libraries with dlopen
endif()
target_include_directories(ps2runtime ${PS2_API} "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")

# the IOP thread manager tested through a synthetic module, no game needed: ninja ps2thbasetest
if(NOT EMSCRIPTEN AND NOT PS2_IOS)
    add_executable(ps2thbasetest EXCLUDE_FROM_ALL tests/iop_thbase.cpp)
    target_include_directories(ps2thbasetest PRIVATE runtime/src)
    target_link_libraries(ps2thbasetest PRIVATE ps2runtime)
    if(WIN32)
        target_link_libraries(ps2thbasetest PRIVATE synchronization)  # the GS queue's WaitOnAddress, which the wgpu line above adds only with wgpu
    endif()
    # the memory card file and save state writes, no game needed: ninja ps2savetest
    add_executable(ps2savetest EXCLUDE_FROM_ALL tests/save_files.cpp)
    target_include_directories(ps2savetest PRIVATE runtime/src)
    target_link_libraries(ps2savetest PRIVATE ps2runtime)
    if(WIN32)
        target_link_libraries(ps2savetest PRIVATE synchronization)
    endif()
endif()

if(PS2_IOS AND PS2_IOS_SIMULATOR)
    set(PS2_WGPU_DEFAULT "${CMAKE_SOURCE_DIR}/third_party/wgpu-native-ios-sim")  # scripts/fetch_wgpu.sh ios-sim
elseif(PS2_IOS)
    set(PS2_WGPU_DEFAULT "${CMAKE_SOURCE_DIR}/third_party/wgpu-native-ios")      # scripts/fetch_wgpu.sh ios
else()
    set(PS2_WGPU_DEFAULT "${CMAKE_SOURCE_DIR}/third_party/wgpu-native")
endif()
set(PS2_WGPU_DIR "${PS2_WGPU_DEFAULT}" CACHE PATH "wgpu-native release folder")
if(EMSCRIPTEN OR EXISTS "${PS2_WGPU_DIR}/lib/libwgpu_native.a")
    set(PS2_GS_WGSL "${CMAKE_BINARY_DIR}/gs_wgsl.cpp")
    add_custom_command(OUTPUT "${PS2_GS_WGSL}"
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/runtime/src/gs/gpu/gs.wgsl -DOUT=${PS2_GS_WGSL} -DNAME=kGsShader -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS runtime/src/gs/gpu/gs.wgsl cmake/embed.cmake)
    set(PS2_BLIT_WGSL "${CMAKE_BINARY_DIR}/blit_wgsl.cpp")
    add_custom_command(OUTPUT "${PS2_BLIT_WGSL}"
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/runtime/src/gs/gpu/blit.wgsl -DOUT=${PS2_BLIT_WGSL} -DNAME=kBlitShader -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS runtime/src/gs/gpu/blit.wgsl cmake/embed.cmake)
    if(NOT PS2_PREBUILT)
        target_sources(ps2runtime PRIVATE runtime/src/gs/gpu/gpu.cpp "${PS2_GS_WGSL}" "${PS2_BLIT_WGSL}")
    endif()
    target_compile_definitions(ps2runtime ${PS2_API} PS2_GPU)
    if(EMSCRIPTEN)
        target_compile_options(ps2runtime PUBLIC --use-port=emdawnwebgpu)
        target_link_options(ps2runtime PUBLIC --use-port=emdawnwebgpu)
    else()
        if(NOT PS2_PREBUILT)
            target_include_directories(ps2runtime PRIVATE "${PS2_WGPU_DIR}/include")
        endif()
        if(WIN32)
            target_link_libraries(ps2runtime ${PS2_API} "${PS2_WGPU_DIR}/lib/libwgpu_native.a" ws2_32 userenv ntdll synchronization pthread m)
        else()
            target_link_libraries(ps2runtime ${PS2_API} "${PS2_WGPU_DIR}/lib/libwgpu_native.a" dl pthread m)
        endif()
        if(PS2_IOS)
            target_link_libraries(ps2runtime ${PS2_API} "-framework Metal" "-framework QuartzCore" "-framework Foundation" "-framework CoreFoundation" "-framework CoreGraphics" "-framework IOSurface" "-framework UIKit" objc)
        elseif(APPLE)
            target_link_libraries(ps2runtime ${PS2_API} "-framework Metal" "-framework QuartzCore" "-framework Foundation" "-framework CoreFoundation" "-framework IOKit" "-framework IOSurface" objc)
        endif()
    endif()
endif()

if(PS2_IOS AND NOT EXISTS "${PS2_WGPU_DIR}/lib/libwgpu_native.a")
    message(FATAL_ERROR "iOS draws through wgpu-native only, and there is none in ${PS2_WGPU_DIR}: run scripts/fetch_wgpu.sh ios (or ios-sim)")
endif()

if(NOT EMSCRIPTEN AND NOT PS2_IOS)
    add_executable(ps2sifcmdtest EXCLUDE_FROM_ALL tests/sif_cmd_intr.cpp)
    target_include_directories(ps2sifcmdtest PRIVATE runtime/src)
    target_link_libraries(ps2sifcmdtest PRIVATE ps2runtime)
    if(WIN32)
        target_link_libraries(ps2sifcmdtest PRIVATE synchronization)  # the GS queue's WaitOnAddress, which the wgpu line above adds only with wgpu
    endif()
    # VU control-flow, E-bit return, and ILW/branch-register timing regression fixture: ninja ps2vuregtest
    add_executable(ps2vuregtest EXCLUDE_FROM_ALL tests/test_vu_regression.cpp)
    target_include_directories(ps2vuregtest PRIVATE runtime/src)
    target_link_libraries(ps2vuregtest PRIVATE ps2runtime)
    if(WIN32)
        target_link_libraries(ps2vuregtest PRIVATE synchronization)
    endif()
    # damaged disc images, IOP modules and boot ELFs refused cleanly, no game needed: ninja ps2discinputtest
    add_executable(ps2discinputtest EXCLUDE_FROM_ALL tests/disc_input.cpp)
    target_include_directories(ps2discinputtest PRIVATE runtime/src)
    target_link_libraries(ps2discinputtest PRIVATE ps2runtime)
    if(WIN32)
        target_link_libraries(ps2discinputtest PRIVATE synchronization)
    endif()
    # bad guest pointers and sizes at the IOP's libraries and the SIF cut or refused, no game needed: ninja ps2iopboundstest
    add_executable(ps2iopboundstest EXCLUDE_FROM_ALL tests/iop_bounds.cpp)
    target_include_directories(ps2iopboundstest PRIVATE runtime/src)
    target_link_libraries(ps2iopboundstest PRIVATE ps2runtime)
    if(WIN32)
        target_link_libraries(ps2iopboundstest PRIVATE synchronization)
    endif()
endif()

if(PS2_IOS)
    # SDL2 built for iOS as a static library by scripts/fetch_sdl2_ios.sh (SDL has no iOS release to download)
    if(PS2_IOS_SIMULATOR)
        set(PS2_SDL2_IOS_DIR "${CMAKE_SOURCE_DIR}/third_party/sdl2-ios-sim" CACHE PATH "SDL2 installed for the iOS Simulator")
    else()
        set(PS2_SDL2_IOS_DIR "${CMAKE_SOURCE_DIR}/third_party/sdl2-ios" CACHE PATH "SDL2 installed for iOS devices")
    endif()
    find_package(SDL2 CONFIG REQUIRED PATHS "${PS2_SDL2_IOS_DIR}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    set(PS2_IOS_FRAMEWORKS "-framework AVFoundation" "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreBluetooth"
        "-framework CoreGraphics" "-framework CoreHaptics" "-framework CoreMotion" "-framework Foundation" "-framework GameController"
        "-framework Metal" "-framework OpenGLES" "-framework QuartzCore" "-framework UIKit")
    set(PS2_HOST_SOURCES host/window.cpp host/keymap.cpp host/quickmenu.cpp host/stretch.cpp host/touchpad.cpp host/ios_launch.cpp)
    # the iOS host compiled without a game, so CI can check it: cmake --build <dir> --target ps2ioshostcheck
    add_library(ps2ioshostcheck STATIC EXCLUDE_FROM_ALL host/main.cpp ${PS2_HOST_SOURCES})
    target_compile_definitions(ps2ioshostcheck PRIVATE PS2_WINDOW PS2_TOUCH)
    target_include_directories(ps2ioshostcheck PRIVATE runtime/src)
    target_link_libraries(ps2ioshostcheck PRIVATE ps2runtime SDL2::SDL2)
endif()

if(PS2_GENERATED)
    file(GLOB PS2_UNITS CONFIGURE_DEPENDS "${PS2_GENERATED}/*.cpp")
    add_library(ps2game STATIC ${PS2_UNITS})
    target_include_directories(ps2game PRIVATE "${PS2_GENERATED}")
    target_link_libraries(ps2game PUBLIC ps2runtime)
    target_compile_options(ps2game PRIVATE -Wno-unused-label -Wno-unused-variable)

    if(EMSCRIPTEN)
        set(PS2_WEB_ELF "" CACHE FILEPATH "Game executable preloaded as /game/core.elf")
        set(PS2_WEB_BOOT_FILE "" CACHE FILEPATH "Text file holding the argv[0] the game sees, preloaded as /game/boot.txt")
        set(PS2_WEB_PATCHES "" CACHE FILEPATH "Patch list the runtime writes into memory, preloaded as /game/patches.txt")
        add_executable(ps2web host/web.cpp)
        if(PS2_WEB_BOOT_FILE)
            target_link_options(ps2web PRIVATE "SHELL:--preload-file \"${PS2_WEB_BOOT_FILE}@/game/boot.txt\"")
        endif()
        if(PS2_WEB_PATCHES)
            target_link_options(ps2web PRIVATE "SHELL:--preload-file \"${PS2_WEB_PATCHES}@/game/patches.txt\"")
        endif()
        target_link_libraries(ps2web PRIVATE ps2game ps2runtime)
        target_link_options(ps2web PRIVATE --profiling-funcs -sJSPI -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sPTHREAD_POOL_SIZE=2 -sALLOW_TABLE_GROWTH=1 -lidbfs.js -sSTACK_SIZE=16777216 -sENVIRONMENT=web
            -sEXPORTED_FUNCTIONS=_main,_ps2web_pad,_ps2web_pad_connected,_ps2web_resize,_ps2web_gpu_ms,_ps2web_frames,_ps2web_primitives,_ps2web_audio_ring,_ps2web_audio_queued_ms,_ps2web_idle_ms,_ps2web_vu_interpreted,_ps2web_quick_state,_ps2web_video_options,_ps2web_stream
            -sEXPORTED_RUNTIME_METHODS=callMain,addRunDependency,removeRunDependency,HEAPU8,ENV
            --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html --preload-file ${PS2_WEB_ELF}@/game/core.elf)
        set_target_properties(ps2web PROPERTIES SUFFIX ".html" LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/shell.html)
        return()
    endif()

    if(PS2_IOS)
        # One app per game (scripts/port_ios.sh): the runner, the game's program, patches and args.txt in it (PS2_IOS_GAME_DIR), the disc
        # image in the app's Documents folder, where the player copies it with the Files app or Finder.
        set(PS2_IOS_GAME_DIR "" CACHE PATH "Folder whose files go into the app's game folder (args.txt, the program, patches.txt)")
        set(PS2_IOS_NAME "PS2 Game" CACHE STRING "The name under the app's icon")
        set(PS2_IOS_BUNDLE_ID "com.example.ps2native.game" CACHE STRING "The app's bundle identifier (one per game)")
        set(PS2_IOS_TEAM "" CACHE STRING "Apple development team ID for signing (empty: Xcode's choice, or an unsigned build)")
        set(PS2_IOS_VERSION "1.0" CACHE STRING "The app's version")
        set(PS2_IOS_MORE_MEMORY OFF CACHE BOOL "Ask iOS for the increased memory limit (the team must have that capability)")
        add_executable(ps2run MACOSX_BUNDLE host/main.cpp ${PS2_HOST_SOURCES})
        target_compile_definitions(ps2run PRIVATE PS2_WINDOW PS2_TOUCH)
        target_include_directories(ps2run PRIVATE runtime/src)
        target_link_libraries(ps2run PRIVATE ps2game ps2runtime SDL2::SDL2main SDL2::SDL2 ${PS2_IOS_FRAMEWORKS})
        configure_file(ios/Info.plist.in "${CMAKE_BINARY_DIR}/ios/Info.plist" @ONLY)
        if(PS2_IOS_MORE_MEMORY)
            configure_file(ios/memory.entitlements "${CMAKE_BINARY_DIR}/ios/app.entitlements" COPYONLY)
        else()
            configure_file(ios/plain.entitlements "${CMAKE_BINARY_DIR}/ios/app.entitlements" COPYONLY)
        endif()
        set_target_properties(ps2run PROPERTIES
            OUTPUT_NAME "ps2run"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/ios/Info.plist"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${PS2_IOS_BUNDLE_ID}"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
            XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_BINARY_DIR}/ios/app.entitlements"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${PS2_IOS_TEAM}"
            XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
            XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING "YES")
        if(PS2_IOS_GAME_DIR)
            # copied after linking, before Xcode signs the app
            add_custom_command(TARGET ps2run POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${PS2_IOS_GAME_DIR}" "$<TARGET_BUNDLE_DIR:ps2run>/game")
        endif()
        return()
    endif()

    add_executable(ps2run host/main.cpp)
    target_link_libraries(ps2run PRIVATE ps2game ps2runtime)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_sources(ps2run PRIVATE host/profile.cpp)
        target_compile_definitions(ps2run PRIVATE PS2_PROFILE)
    elseif(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_sources(ps2run PRIVATE host/profile_win.cpp)
        target_compile_definitions(ps2run PRIVATE PS2_PROFILE)
        target_link_libraries(ps2run PRIVATE winmm)
    endif()
    if(WIN32)
        list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/third_party/sdl2")
        set(SDL2_NO_MWINDOWS ON)
    endif()
    find_package(SDL2 QUIET)
    if(SDL2_FOUND AND NOT EMSCRIPTEN)
        target_sources(ps2run PRIVATE host/window.cpp host/keymap.cpp host/quickmenu.cpp host/stretch.cpp)
        target_compile_definitions(ps2run PRIVATE PS2_WINDOW)
        target_link_libraries(ps2run PRIVATE SDL2::SDL2)
        if(WIN32)
            add_custom_command(TARGET ps2run POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:ps2run>)
            # Microsoft's shader compiler for Direct3D 12 (scripts/fetch_dxc.sh): used when the two DLLs sit next to ps2run.exe,
            # FXC otherwise. Not shipped with the source: each PC downloads it.
            foreach(dxc_dll dxcompiler.dll dxil.dll)
                if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/dxc/${dxc_dll}")
                    add_custom_command(TARGET ps2run POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/dxc/${dxc_dll}" $<TARGET_FILE_DIR:ps2run>)
                endif()
            endforeach()
            # A window (GUI) program, so a double-click or a shortcut opens no console window (-mwindows; SDL2_NO_MWINDOWS above
            # keeps SDL's own package from adding it). The entry point still reaches main() in host/main.cpp, which gives stdout
            # and stderr back: a pipe or file of the parent as it is, else the console of the parent, else NUL.
            set_target_properties(ps2run PROPERTIES WIN32_EXECUTABLE ON)
        endif()
        # the game controller path tested with SDL's virtual controllers, no game needed: ninja -C build/native-<game> ps2padtest
        add_executable(ps2padtest EXCLUDE_FROM_ALL host/pad_test.cpp host/window.cpp host/keymap.cpp host/quickmenu.cpp host/stretch.cpp)
        target_include_directories(ps2padtest PRIVATE runtime/src)
        target_link_libraries(ps2padtest PRIVATE ps2runtime SDL2::SDL2)
    endif()
endif()
