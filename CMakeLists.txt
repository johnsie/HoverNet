cmake_minimum_required(VERSION 3.16)

project(HoverNetLinux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    # The portable graph still consumes a small MFC compatibility layer through
    # StdAfx.h. CMake and vcpkg use the dynamic MSVC runtime (/MD), so MFC must
    # use its matching shared-DLL ABI as well. Without _AFXDLL, afx.h rejects
    # every translation unit before compilation with C1189.
    add_compile_definitions(_AFXDLL HOVERNET_STATIC NOMINMAX)
endif()

# Linux quality gates (off by default so ordinary and Windows builds are
# unchanged). CI enables HOVERNET_WARNINGS on the Release build and
# HOVERNET_SANITIZE on a dedicated RelWithDebInfo build; see docs/gitlab-ci.md.
option(HOVERNET_WARNINGS "Compile with -Wall -Wextra (GCC/Clang)" OFF)
option(HOVERNET_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
set(HOVERNET_WEB_SOCKET_URL "wss://hovernet.outiva.com/race/" CACHE STRING
    "WebSocket URL used by the browser client to reach its RaceServer gateway")

# The simulation must give the same bits on every CPU. A compiler may fuse a multiply and an add into one instruction (FMA) that rounds once
# instead of twice; GCC and Clang do that by default on ARM (Apple Silicon, Raspberry Pi, ARM servers) and on x86 built for newer CPUs, and
# the result differs in the last bit. Measured on this repository: built with FMA fusing on, the determinism, snapshot and physics-golden
# tests all fail; with it off they pass, with identical hashes (docs/determinism.md, "Floating point"). So fusing is off everywhere.
if(MSVC)
    add_compile_options(/fp:precise)
else()
    add_compile_options(-ffp-contract=off)
endif()

if(NOT MSVC)
    if(HOVERNET_WARNINGS)
        add_compile_options(-Wall -Wextra)
    endif()
    if(HOVERNET_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

if(EMSCRIPTEN)
    # Track downloads use the existing background worker. Pthreads in browsers
    # require cross-origin isolation, which the Apache vhost supplies.
    add_compile_options(-pthread)
    add_link_options(-pthread -sPTHREAD_POOL_SIZE=2)
endif()

if(EMSCRIPTEN)
    # Emscripten ships SDL2 as a system port instead of an SDL2Config.cmake
    # package. Keep the same imported target name used by native targets.
    add_library(SDL2::SDL2 INTERFACE IMPORTED GLOBAL)
    target_compile_options(SDL2::SDL2 INTERFACE -sUSE_SDL=2)
    target_link_options(SDL2::SDL2 INTERFACE -sUSE_SDL=2)
else()
    find_package(SDL2 REQUIRED)
endif()
include(CTest)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Shown in the Linux client's window title (see SDL2Graphics.cpp). Matches the
# vX.Y.Z tags CI builds releases from (see .gitlab-ci.yml's PACKAGE_VERSION);
# an untagged dev build instead gets the nearest tag plus commit count/hash
# (or just the hash with no tags yet), "+local" is added when the files differ from
# that commit, and "dev" is used if git isn't available at all (e.g. building from a
# source tarball with no .git directory).
find_package(Git QUIET)
set(HOVERNET_VERSION "dev")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE HOVERNET_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE HOVERNET_GIT_DESCRIBE_RESULT
    )
    # A build made from files that differ from the commit says so with "+local"
    # (e.g. v2.0.0+local), so nobody mistakes it for the published release.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE HOVERNET_GIT_CHANGES
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(HOVERNET_GIT_DESCRIBE_RESULT EQUAL 0 AND NOT HOVERNET_VERSION_OUTPUT STREQUAL "")
        set(HOVERNET_VERSION "${HOVERNET_VERSION_OUTPUT}")
        if(NOT HOVERNET_GIT_CHANGES STREQUAL "")
            set(HOVERNET_VERSION "${HOVERNET_VERSION}+local")
        endif()
    endif()
endif()
message(STATUS "HoverNet version: ${HOVERNET_VERSION}")

file(GLOB HOVERNET_RACESERVER_SOURCES CONFIGURE_DEPENDS
    NetTarget/RaceServer/*.cpp
    NetTarget/RaceServer/NetworkInterface/*.cpp
    NetTarget/RaceServer/GameSimulation/*.cpp
    NetTarget/RaceServer/RaceManager/*.cpp
    NetTarget/RaceServer/Config/*.cpp
    NetTarget/RaceServer/Logging/*.cpp
)

add_executable(RaceServer ${HOVERNET_RACESERVER_SOURCES})
target_include_directories(RaceServer PRIVATE
    NetTarget/RaceServer
    NetTarget/RaceServer/NetworkInterface
    NetTarget/RaceServer/GameSimulation
    NetTarget/RaceServer/RaceManager
    NetTarget/RaceServer/Config
    NetTarget/RaceServer/Logging
)
target_link_libraries(RaceServer PRIVATE Threads::Threads)

add_library(HoverNetImGui STATIC
    NetTarget/ThirdParty/imgui/imgui.cpp
    NetTarget/ThirdParty/imgui/imgui_draw.cpp
    NetTarget/ThirdParty/imgui/imgui_tables.cpp
    NetTarget/ThirdParty/imgui/imgui_widgets.cpp
    NetTarget/ThirdParty/imgui/backends/imgui_impl_sdl2.cpp
    NetTarget/ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(HoverNetImGui PUBLIC
    NetTarget/ThirdParty/imgui
    NetTarget/ThirdParty/imgui/backends
)
target_link_libraries(HoverNetImGui PUBLIC SDL2::SDL2)
set_target_properties(HoverNetImGui PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(HoverNetPortableUtil STATIC
    NetTarget/Util/DllObjectFactory.cpp
    NetTarget/Util/FuzzyLogic.cpp
    NetTarget/Util/StrRes.cpp
    NetTarget/Util/RecordFile.cpp
    NetTarget/Util/WorldCoordinates.cpp
)

target_include_directories(HoverNetPortableUtil PUBLIC NetTarget/Util)
target_link_libraries(HoverNetPortableUtil PUBLIC ${CMAKE_DL_LIBS})
set_target_properties(HoverNetPortableUtil PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(HoverNetModelCore STATIC
    NetTarget/Model/ConcreteShape.cpp
    NetTarget/Model/ContactEffect.cpp
    NetTarget/Model/FreeElementMovingHelper.cpp
    NetTarget/Model/GameSession.cpp
    NetTarget/Model/Level.cpp
    NetTarget/Model/MazeElement.cpp
    NetTarget/Model/PhysicalCollision.cpp
    NetTarget/Model/Shapes.cpp
    NetTarget/Model/ShapeCollisions.cpp
    NetTarget/Model/StateHash.cpp
    NetTarget/Model/StateSnapshot.cpp
)

target_link_libraries(HoverNetModelCore PUBLIC HoverNetPortableUtil)
if(WIN32)
    target_link_libraries(HoverNetModelCore PUBLIC winmm)
endif()
set_target_properties(HoverNetModelCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The simulation for programs with no screen or speakers (server, replay and determinism tools, tests): level
# model, collisions and the hovercraft's physics, with the hovercraft's presentation seam stubbed out. It links with
# no video, audio or resource code (docs/adr/0001-simulation-core.md); the headless tools link only this, which
# enforces it.
add_library(HoverNetSim STATIC
    NetTarget/MainCharacter/MainCharacter.cpp
    NetTarget/MainCharacter/MainCharacterRenderer.cpp
    NetTarget/MainCharacter/MainCharacterHeadless.cpp
    NetTarget/ObjFac1/SimElements.cpp
    NetTarget/ObjFac1/FuelSource.cpp
    NetTarget/ObjFac1/DoubleSpeedSource.cpp
    NetTarget/ObjFac1/FinishLine.cpp
    NetTarget/Authority/ReplayFormat.cpp
    NetTarget/Authority/ReplayStoreDir.cpp
    NetTarget/Authority/TrackPackage.cpp
    NetTarget/Authority/Hmac.cpp
    NetTarget/Authority/Moderation.cpp
    NetTarget/Authority/Crypto.cpp
    NetTarget/Authority/SecureChannel.cpp
    NetTarget/Authority/WebSocket.cpp
    NetTarget/Authority/AuthoritativeRace.cpp
    NetTarget/Authority/Protocol3.cpp
    NetTarget/Authority/RaceWorld.cpp
    NetTarget/Authority/PredictingClient.cpp
    NetTarget/Authority/RaceHost.cpp
    NetTarget/Authority/LobbyServer.cpp
    NetTarget/LinuxClient/Sha256.cpp
)

target_link_libraries(HoverNetSim PUBLIC HoverNetModelCore HoverNetPortableUtil)
set_target_properties(HoverNetSim PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The same simulation with the real presentation (renderer, sounds, drawing) for the game client.
add_library(HoverNetMainCharacter STATIC
    NetTarget/MainCharacter/MainCharacter.cpp
    NetTarget/MainCharacter/MainCharacterRenderer.cpp
    NetTarget/MainCharacter/MainCharacterView.cpp
)

target_link_libraries(HoverNetMainCharacter PUBLIC HoverNetModelCore HoverNetResourceCore HoverNetVideoCore HoverNetPortableUtil)
set_target_properties(HoverNetMainCharacter PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(HoverNetClientSession STATIC
    NetTarget/Game2/ClientSession.cpp
)

target_link_libraries(HoverNetClientSession PUBLIC HoverNetMainCharacter HoverNetModelCore HoverNetResourceCore HoverNetVideoCore HoverNetPortableUtil)

add_library(HoverNetObserver STATIC
    NetTarget/Game2/Observer.cpp
)

target_link_libraries(HoverNetObserver PUBLIC HoverNetClientSession HoverNetMainCharacter HoverNetResourceCore HoverNetVideoCore HoverNetPortableUtil)

add_library(HoverNetHeadlessObjFac1 SHARED
    NetTarget/ObjFac1/LinuxPlugin.cpp
    NetTarget/ObjFac1/SimElements.cpp
    NetTarget/ObjFac1/FuelSource.cpp
    NetTarget/ObjFac1/DoubleSpeedSource.cpp
    NetTarget/ObjFac1/FinishLine.cpp
)

target_link_libraries(HoverNetHeadlessObjFac1 PRIVATE HoverNetMainCharacter HoverNetModelCore HoverNetResourceCore HoverNetPortableUtil)
target_compile_definitions(HoverNetHeadlessObjFac1 PRIVATE HOVERNET_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(HoverNetHeadlessObjFac1 PROPERTIES
    OUTPUT_NAME ObjFac1
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_library(HoverNetResourceCore STATIC
    NetTarget/ObjFacTools/ResActor.cpp
    NetTarget/ObjFacTools/ResBitmap.cpp
    NetTarget/ObjFacTools/ResSound.cpp
    NetTarget/ObjFacTools/ResSprite.cpp
    NetTarget/ObjFacTools/ResourceLib.cpp
    NetTarget/ObjFacTools/SpriteHandle.cpp
    NetTarget/LinuxClient/HeadlessSoundServer.cpp
    NetTarget/LinuxClient/HeadlessSprite.cpp
)

target_link_libraries(HoverNetResourceCore PUBLIC HoverNetPortableUtil SDL2::SDL2)
set_target_properties(HoverNetResourceCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(HoverNetVideoCore STATIC
    NetTarget/LinuxClient/HeadlessVideoBuffer.cpp
    NetTarget/VideoServices/2DViewPort.cpp
    NetTarget/VideoServices/3DViewport.cpp
    NetTarget/VideoServices/3DViewportRendering.cpp
    NetTarget/VideoServices/Bitmap.cpp
    NetTarget/VideoServices/ColorPalette.cpp
    NetTarget/VideoServices/ColorTab.cpp
)

target_link_libraries(HoverNetVideoCore PUBLIC HoverNetPortableUtil)
set_target_properties(HoverNetVideoCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(HoverNetResourceCore PUBLIC HoverNetVideoCore)

add_executable(HoverNetRecordFileSmoke
    NetTarget/LinuxClient/RecordFileSmoke.cpp
)

target_link_libraries(HoverNetRecordFileSmoke PRIVATE HoverNetPortableUtil)

add_executable(HoverNetModelCoreSmoke
    NetTarget/LinuxClient/ModelCoreSmoke.cpp
)

target_link_libraries(HoverNetModelCoreSmoke PRIVATE HoverNetModelCore)

add_executable(HoverNetLevelSmoke
    NetTarget/LinuxClient/LevelSmoke.cpp
)

target_link_libraries(HoverNetLevelSmoke PRIVATE HoverNetModelCore)

add_executable(HoverNetGameSessionSmoke
    NetTarget/LinuxClient/GameSessionSmoke.cpp
)

target_link_libraries(HoverNetGameSessionSmoke PRIVATE HoverNetModelCore)

add_executable(HoverNetMainCharacterSmoke
    NetTarget/LinuxClient/MainCharacterSmoke.cpp
)

add_dependencies(HoverNetMainCharacterSmoke HoverNetHeadlessObjFac1)
target_link_libraries(HoverNetMainCharacterSmoke PRIVATE HoverNetMainCharacter)

if(EMSCRIPTEN)
    # The same run as a WebAssembly program for Node, with the object factory linked in (the browser
    # build does the same), so its hashes can be compared with the native build's: the first
    # cross-platform determinism check.
    add_executable(HoverNetDeterminismRun
        NetTarget/LinuxClient/DeterminismRun.cpp
        NetTarget/LinuxClient/WebFactory.cpp
        NetTarget/ObjFac1/LinuxPlugin.cpp
        NetTarget/ObjFac1/SimElements.cpp
        NetTarget/ObjFac1/FuelSource.cpp
        NetTarget/ObjFac1/DoubleSpeedSource.cpp
        NetTarget/ObjFac1/FinishLine.cpp
    )
    target_compile_definitions(HoverNetDeterminismRun PRIVATE HOVERNET_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(HoverNetDeterminismRun PRIVATE HoverNetMainCharacter HoverNetModelCore HoverNetResourceCore HoverNetPortableUtil)
    target_link_options(HoverNetDeterminismRun PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node)
    set_target_properties(HoverNetDeterminismRun PROPERTIES SUFFIX ".js")
else()
    add_executable(HoverNetDeterminismRun
        NetTarget/LinuxClient/DeterminismRun.cpp
    )

    target_link_libraries(HoverNetDeterminismRun PRIVATE HoverNetSim)

    add_executable(HoverNetNetcodeProto
        NetTarget/LinuxClient/NetcodeProto.cpp
    )
    target_link_libraries(HoverNetNetcodeProto PRIVATE HoverNetSim)

    add_executable(HoverNetFrameRateTest
        NetTarget/LinuxClient/FrameRateTest.cpp
    )
    target_link_libraries(HoverNetFrameRateTest PRIVATE HoverNetSim)

    add_executable(HoverNetDriver
        NetTarget/LinuxClient/DriverTool.cpp
    )
    target_link_libraries(HoverNetDriver PRIVATE HoverNetSim)

    add_executable(HoverNetAuthorityTest
        NetTarget/LinuxClient/AuthorityTest.cpp
    )
    target_link_libraries(HoverNetAuthorityTest PRIVATE HoverNetSim)

    add_executable(HoverNetReplayLibraryTest NetTarget/LinuxClient/ReplayLibraryTest.cpp NetTarget/Authority/ReplayFormat.cpp)
    add_test(NAME HoverNetReplayLibrary COMMAND HoverNetReplayLibraryTest)
    add_executable(HoverNetReplayShareTest NetTarget/LinuxClient/ReplayShareTest.cpp)
    target_link_libraries(HoverNetReplayShareTest PRIVATE HoverNetSim)
    add_test(NAME HoverNetReplayShare COMMAND HoverNetReplayShareTest)
    add_executable(HoverNetReplayRecorderTest
        NetTarget/LinuxClient/ReplayRecorderTest.cpp
    )
    target_link_libraries(HoverNetReplayRecorderTest PRIVATE HoverNetSim)
    add_test(NAME HoverNetReplayRecorder COMMAND HoverNetReplayRecorderTest ClassicH 3 700)
    add_test(NAME HoverNetReplayRecorderEight COMMAND HoverNetReplayRecorderTest Steeplechase 8 600)
    set_tests_properties(HoverNetReplayRecorder HoverNetReplayRecorderEight PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(HoverNetWebSocketTest
        NetTarget/LinuxClient/WebSocketTest.cpp
        NetTarget/Authority/WebSocket.cpp
    )

    add_executable(HoverNetEd25519Test
        NetTarget/LinuxClient/Ed25519Test.cpp
        NetTarget/Authority/Ed25519.cpp
    )
    add_executable(HoverNetSign
        NetTarget/LinuxClient/SignTool.cpp
        NetTarget/Authority/Ed25519.cpp
    )

    add_executable(HoverNetProtocolTest
        NetTarget/LinuxClient/ProtocolTest.cpp
    )
    target_link_libraries(HoverNetProtocolTest PRIVATE HoverNetSim)

    # A dedicated server for authoritative races over TCP and a scripted player that checks every tick it is sent.
    add_executable(HoverNetAuthServer
        NetTarget/LinuxClient/AuthServer.cpp
        NetTarget/LinuxClient/CrashReporter.cpp
    )
    target_compile_definitions(HoverNetAuthServer PRIVATE HOVERNET_SERVER_VERSION="${HOVERNET_VERSION}")
    target_link_libraries(HoverNetAuthServer PRIVATE HoverNetSim)
    add_executable(HoverNetNetProxy
        NetTarget/LinuxClient/NetProxy.cpp
    )
    if(WIN32)
        target_link_libraries(HoverNetNetProxy PRIVATE ws2_32)
    endif()
    add_executable(HoverNetModTool
        NetTarget/LinuxClient/ModTool.cpp
    )
    target_link_libraries(HoverNetModTool PRIVATE HoverNetSim)
    if(WIN32)
        target_link_libraries(HoverNetModTool PRIVATE ws2_32)
    endif()
    add_executable(HoverNetAuthBot
        NetTarget/LinuxClient/AuthBot.cpp
        NetTarget/LinuxClient/AuthConnection.cpp
    )
    target_link_libraries(HoverNetAuthBot PRIVATE HoverNetSim)
    if(WIN32)
        target_link_libraries(HoverNetAuthServer PRIVATE ws2_32)
        target_link_libraries(HoverNetAuthBot PRIVATE ws2_32)
    endif()

    add_executable(HoverNetOnlineLobbyTest
        NetTarget/LinuxClient/OnlineLobbyTest.cpp
        NetTarget/LinuxClient/OnlineLobby.cpp
        NetTarget/LinuxClient/Sha256.cpp
        NetTarget/LinuxClient/AuthConnection.cpp
        NetTarget/Authority/Protocol3.cpp
        NetTarget/Authority/Crypto.cpp
        NetTarget/Authority/SecureChannel.cpp
        NetTarget/Authority/Hmac.cpp
    )
    if(WIN32)
        target_link_libraries(HoverNetOnlineLobbyTest PRIVATE ws2_32)
    endif()

    add_executable(HoverNetPersonalBestsTest NetTarget/LinuxClient/PersonalBestsTest.cpp)
    add_test(NAME HoverNetPersonalBests COMMAND HoverNetPersonalBestsTest)
    add_executable(HoverNetGhostRunTest NetTarget/LinuxClient/GhostRunTest.cpp)
    add_test(NAME HoverNetGhostRun COMMAND HoverNetGhostRunTest)

    add_executable(HoverNetPredictionTest
        NetTarget/LinuxClient/PredictionTest.cpp
    )
    target_link_libraries(HoverNetPredictionTest PRIVATE HoverNetSim)

    add_executable(HoverNetLobbyTest
        NetTarget/LinuxClient/LobbyTest.cpp
    )
    target_link_libraries(HoverNetLobbyTest PRIVATE HoverNetSim)

    add_executable(HoverNetTrackPack
        NetTarget/LinuxClient/TrackPack.cpp
    )
    target_link_libraries(HoverNetTrackPack PRIVATE HoverNetSim)

    add_executable(HoverNetModerationTest
        NetTarget/LinuxClient/ModerationTest.cpp
    )
    target_link_libraries(HoverNetModerationTest PRIVATE HoverNetSim)

    add_executable(HoverNetLoadTest
        NetTarget/LinuxClient/LoadTest.cpp
    )
    target_link_libraries(HoverNetLoadTest PRIVATE HoverNetSim)

    add_executable(HoverNetCryptoTest
        NetTarget/LinuxClient/CryptoTest.cpp
    )
    target_link_libraries(HoverNetCryptoTest PRIVATE HoverNetSim)

    add_executable(HoverNetSnapshotTest
        NetTarget/LinuxClient/SnapshotTest.cpp
    )
    target_link_libraries(HoverNetSnapshotTest PRIVATE HoverNetSim)

    add_executable(HoverNetReplay
        NetTarget/LinuxClient/ReplayTool.cpp
    )
    target_link_libraries(HoverNetReplay PRIVATE HoverNetSim)
endif()
if(EMSCRIPTEN)
    # The simulation-only tools also run as WebAssembly under Node, so the browser build is held to the same replay, snapshot
    # and authority checks as the native builds (no plug-in or resource file is involved: they use HoverNetSim alone).
    foreach(tool HoverNetReplay HoverNetSnapshotTest HoverNetFrameRateTest HoverNetAuthorityTest HoverNetProtocolTest HoverNetPredictionTest HoverNetLobbyTest HoverNetModerationTest HoverNetCryptoTest)
        if(tool STREQUAL "HoverNetReplay")
            set(source ReplayTool.cpp)
        elseif(tool STREQUAL "HoverNetSnapshotTest")
            set(source SnapshotTest.cpp)
        elseif(tool STREQUAL "HoverNetFrameRateTest")
            set(source FrameRateTest.cpp)
        elseif(tool STREQUAL "HoverNetAuthorityTest")
            set(source AuthorityTest.cpp)
        elseif(tool STREQUAL "HoverNetProtocolTest")
            set(source ProtocolTest.cpp)
        elseif(tool STREQUAL "HoverNetLobbyTest")
            set(source LobbyTest.cpp)
        elseif(tool STREQUAL "HoverNetModerationTest")
            set(source ModerationTest.cpp)
        elseif(tool STREQUAL "HoverNetCryptoTest")
            set(source CryptoTest.cpp)
        else()
            set(source PredictionTest.cpp)
        endif()
        add_executable(${tool} NetTarget/LinuxClient/${source})
        target_link_libraries(${tool} PRIVATE HoverNetSim)
        target_link_options(${tool} PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node)
        set_target_properties(${tool} PROPERTIES SUFFIX ".js")
    endforeach()
endif()


add_executable(HoverNetMainCharacterRenderSmoke
    NetTarget/LinuxClient/MainCharacterRenderSmoke.cpp
)

add_dependencies(HoverNetMainCharacterRenderSmoke HoverNetHeadlessObjFac1)
target_link_libraries(HoverNetMainCharacterRenderSmoke PRIVATE HoverNetClientSession HoverNetVideoCore)

add_executable(HoverNetClientSessionSmoke
    NetTarget/LinuxClient/ClientSessionSmoke.cpp
)

add_dependencies(HoverNetClientSessionSmoke HoverNetHeadlessObjFac1)
target_link_libraries(HoverNetClientSessionSmoke PRIVATE HoverNetClientSession)

add_executable(HoverNetObserverSmoke
    NetTarget/LinuxClient/ObserverSmoke.cpp
)

add_dependencies(HoverNetObserverSmoke HoverNetHeadlessObjFac1)
target_link_libraries(HoverNetObserverSmoke PRIVATE HoverNetObserver)

add_executable(HoverNetTrackValidator
    NetTarget/LinuxClient/TrackLoadSmoke.cpp
)

add_dependencies(HoverNetTrackValidator HoverNetHeadlessObjFac1)
target_link_libraries(HoverNetTrackValidator PRIVATE HoverNetModelCore HoverNetPortableUtil)

add_executable(HoverNetFactorySmoke
    NetTarget/LinuxClient/FactorySmoke.cpp
)

target_link_libraries(HoverNetFactorySmoke PRIVATE HoverNetPortableUtil)

add_executable(HoverNetDynamicFactorySmoke
    NetTarget/LinuxClient/DynamicFactorySmoke.cpp
)

add_dependencies(HoverNetDynamicFactorySmoke HoverNetHeadlessObjFac1)
target_compile_definitions(HoverNetDynamicFactorySmoke PRIVATE HOVERNET_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(HoverNetDynamicFactorySmoke PRIVATE HoverNetPortableUtil HoverNetModelCore HoverNetResourceCore)

add_executable(HoverNetMantaResourceBuilder
    NetTarget/LinuxClient/MantaResourceBuilder.cpp
    NetTarget/ResourceCompiler/ResActorBuilder.cpp
    NetTarget/ResourceCompiler/TextParsing.cpp
)
target_link_libraries(HoverNetMantaResourceBuilder PRIVATE HoverNetResourceCore HoverNetPortableUtil)

add_executable(HoverNetResourceSmoke
    NetTarget/LinuxClient/ResourceSmoke.cpp
)

target_link_libraries(HoverNetResourceSmoke PRIVATE HoverNetResourceCore)

add_executable(HoverNetViewportSmoke
    NetTarget/LinuxClient/ViewportSmoke.cpp
)

target_link_libraries(HoverNetViewportSmoke PRIVATE HoverNetVideoCore HoverNetResourceCore)

add_executable(HoverNetActorRenderSmoke
    NetTarget/LinuxClient/ActorRenderSmoke.cpp
)

target_link_libraries(HoverNetActorRenderSmoke PRIVATE HoverNetVideoCore HoverNetResourceCore)

add_executable(HoverNetImGuiLogicalMouseSmoke
    NetTarget/LinuxClient/ImGuiLogicalMouseSmoke.cpp
)
target_link_libraries(HoverNetImGuiLogicalMouseSmoke PRIVATE HoverNetImGui SDL2::SDL2)

add_library(HoverNetRaceServerClient STATIC
    NetTarget/LinuxClient/RaceServerClient.cpp
)
target_include_directories(HoverNetRaceServerClient PUBLIC NetTarget/LinuxClient)
if(WIN32)
    target_link_libraries(HoverNetRaceServerClient PUBLIC ws2_32)
endif()

add_executable(HoverNetRaceServerClientSmoke
    NetTarget/LinuxClient/RaceServerClientSmoke.cpp
)
add_dependencies(HoverNetRaceServerClientSmoke RaceServer)
target_link_libraries(HoverNetRaceServerClientSmoke PRIVATE HoverNetRaceServerClient)

add_executable(HoverNetServerConfigSmoke
    NetTarget/LinuxClient/ServerConfigSmoke.cpp
    NetTarget/RaceServer/Config/ServerConfig.cpp
)
target_include_directories(HoverNetServerConfigSmoke PRIVATE NetTarget/RaceServer)

add_executable(HoverNetRaceServerLifecycleSmoke
    NetTarget/LinuxClient/RaceServerLifecycleSmoke.cpp
)
add_dependencies(HoverNetRaceServerLifecycleSmoke RaceServer)
target_link_libraries(HoverNetRaceServerLifecycleSmoke PRIVATE HoverNetRaceServerClient)

add_executable(HoverNetInRaceChatSmoke
    NetTarget/LinuxClient/InRaceChatSmoke.cpp
)
add_dependencies(HoverNetInRaceChatSmoke RaceServer HoverNetHeadlessObjFac1)
target_link_libraries(HoverNetInRaceChatSmoke PRIVATE HoverNetClientSession HoverNetRaceServerClient)

add_executable(HoverNetObserverChatRenderSmoke
    NetTarget/LinuxClient/ObserverChatRenderSmoke.cpp
)
add_dependencies(HoverNetObserverChatRenderSmoke HoverNetHeadlessObjFac1)
target_link_libraries(HoverNetObserverChatRenderSmoke PRIVATE HoverNetObserver HoverNetClientSession)

if(EMSCRIPTEN)
    # The same renderer check as WebAssembly under Node (the object factory is compiled in, as in the browser client).
    add_executable(HoverNetRenderGolden
        NetTarget/LinuxClient/RenderGoldenRun.cpp
        NetTarget/LinuxClient/WebFactory.cpp
        NetTarget/ObjFac1/LinuxPlugin.cpp
        NetTarget/ObjFac1/SimElements.cpp
        NetTarget/ObjFac1/FuelSource.cpp
        NetTarget/ObjFac1/DoubleSpeedSource.cpp
        NetTarget/ObjFac1/FinishLine.cpp
    )
    target_compile_definitions(HoverNetRenderGolden PRIVATE HOVERNET_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(HoverNetRenderGolden PRIVATE HoverNetObserver HoverNetClientSession)
    target_link_options(HoverNetRenderGolden PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node)
    set_target_properties(HoverNetRenderGolden PROPERTIES SUFFIX ".js")
endif()
if(NOT EMSCRIPTEN)
    # The classic renderer's output for fixed scenes, as hashes (docs/render-golden.md).
    add_executable(HoverNetRenderGolden
        NetTarget/LinuxClient/RenderGoldenRun.cpp
    )
    add_dependencies(HoverNetRenderGolden HoverNetHeadlessObjFac1)
    target_link_libraries(HoverNetRenderGolden PRIVATE HoverNetObserver HoverNetClientSession)
endif()

add_executable(HoverNetLobby
    NetTarget/LinuxClient/LobbyCli.cpp
)
target_link_libraries(HoverNetLobby PRIVATE HoverNetRaceServerClient)

add_executable(HoverNetLinuxBootstrap
    NetTarget/LinuxClient/main.cpp
    NetTarget/GraphicsSDL2/SDL2Graphics.cpp
)

target_compile_definitions(HoverNetLinuxBootstrap PRIVATE _HAVE_SDL2)
target_link_libraries(HoverNetLinuxBootstrap PRIVATE SDL2::SDL2)

add_executable(HoverNetTrackViewer
    NetTarget/LinuxClient/TrackViewer.cpp
    NetTarget/GraphicsSDL2/SDL2Graphics.cpp
)

add_dependencies(HoverNetTrackViewer HoverNetHeadlessObjFac1)
target_compile_definitions(HoverNetTrackViewer PRIVATE _HAVE_SDL2 HOVERNET_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(HoverNetTrackViewer PRIVATE HoverNetModelCore HoverNetPortableUtil SDL2::SDL2)

add_executable(HoverNetMazeCompiler
    NetTarget/MazeCompiler/main.cpp
    NetTarget/MazeCompiler/LevelBuilder.cpp
    NetTarget/MazeCompiler/LevelBuilderVisiblesZones.cpp
    NetTarget/MazeCompiler/Parser.cpp
    NetTarget/MazeCompiler/TrackCommonStuff.cpp
    NetTarget/MazeCompiler/TrackMap.cpp
)

add_dependencies(HoverNetMazeCompiler HoverNetHeadlessObjFac1)
target_include_directories(HoverNetMazeCompiler PRIVATE NetTarget/MazeCompiler)
target_link_libraries(HoverNetMazeCompiler PRIVATE HoverNetModelCore HoverNetResourceCore HoverNetVideoCore HoverNetPortableUtil)

add_executable(HoverNetTrack3DViewer
    NetTarget/LinuxClient/Track3DViewer.cpp
    NetTarget/GraphicsSDL2/SDL2Graphics.cpp
)

add_dependencies(HoverNetTrack3DViewer HoverNetHeadlessObjFac1)
target_compile_definitions(HoverNetTrack3DViewer PRIVATE _HAVE_SDL2 HOVERNET_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(HoverNetTrack3DViewer PRIVATE HoverNetClientSession HoverNetResourceCore HoverNetVideoCore SDL2::SDL2)

add_executable(HoverNetGame2Player
    NetTarget/LinuxClient/Track3DViewer.cpp
    NetTarget/LinuxClient/TrackCatalog.cpp
    NetTarget/LinuxClient/TrackDownloader.cpp
    NetTarget/LinuxClient/Gunzip.cpp
    NetTarget/LinuxClient/Sha256.cpp
    NetTarget/Authority/ReplayFormat.cpp
    NetTarget/GraphicsSDL2/SDL2Graphics.cpp
)

if(NOT EMSCRIPTEN)
    # The connection to an authoritative server (protocol 3.0) and the client side of its prediction.
    target_sources(HoverNetGame2Player PRIVATE
        NetTarget/LinuxClient/AuthConnection.cpp
        NetTarget/LinuxClient/OnlineLobby.cpp
        NetTarget/Authority/Protocol3.cpp
        NetTarget/Authority/RaceWorld.cpp
        NetTarget/Authority/PredictingClient.cpp
        NetTarget/Authority/Crypto.cpp
        NetTarget/Authority/SecureChannel.cpp
        NetTarget/Authority/Hmac.cpp
    )
    if(WIN32)
        target_link_libraries(HoverNetGame2Player PRIVATE ws2_32)
    endif()
endif()

if(EMSCRIPTEN)
    # The browser cannot dlopen ObjFac1.so. Compile the same factory into the
    # executable and register it through the local-factory API instead.
    target_sources(HoverNetGame2Player PRIVATE
        NetTarget/LinuxClient/WebFactory.cpp
        NetTarget/ObjFac1/LinuxPlugin.cpp
        NetTarget/ObjFac1/SimElements.cpp
        NetTarget/ObjFac1/FuelSource.cpp
        NetTarget/ObjFac1/DoubleSpeedSource.cpp
        NetTarget/ObjFac1/FinishLine.cpp
    )
    # Playing on a HoverNet 3.0 server from the browser: protocol 3.0 over a WebSocket (AuthConnection.cpp has the browser variant).
    target_sources(HoverNetGame2Player PRIVATE
        NetTarget/LinuxClient/AuthConnection.cpp
        NetTarget/LinuxClient/OnlineLobby.cpp
        NetTarget/Authority/Protocol3.cpp
        NetTarget/Authority/RaceWorld.cpp
        NetTarget/Authority/PredictingClient.cpp
        NetTarget/Authority/Crypto.cpp
    )
    target_compile_definitions(HoverNetGame2Player PRIVATE HOVERNET_WEB_AUTH)
    target_compile_definitions(HoverNetGame2Player PRIVATE HOVERNET_WEB)
    target_compile_definitions(HoverNetGame2Player PRIVATE
        "HOVERNET_WEB_SOCKET_URL=\"${HOVERNET_WEB_SOCKET_URL}\"")
    set_target_properties(HoverNetGame2Player PROPERTIES OUTPUT_NAME HoverNet)
    target_link_options(HoverNetGame2Player PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sMAX_WEBGL_VERSION=2
        -sASYNCIFY=1
        -sFETCH=1
        -lwebsocket.js
        -lidbfs.js
        "-sEXPORTED_FUNCTIONS=_main,_hovernet_set_touch_mask,_hovernet_in_race,_hovernet_frames,_hovernet_wants_text,_hovernet_text_input,_hovernet_text_key"
        "-sEXPORTED_RUNTIME_METHODS=ccall"
        --shell-file "${CMAKE_SOURCE_DIR}/packaging/web/shell.html"
        --pre-js "${CMAKE_SOURCE_DIR}/packaging/web/web-pre.js"
        "--preload-file=${CMAKE_SOURCE_DIR}/NetTarget/ObjFac1.dat@/NetTarget/ObjFac1.dat"
        "--preload-file=${CMAKE_SOURCE_DIR}/NetTarget/Tracks@/NetTarget/Tracks"
        "--preload-file=${CMAKE_SOURCE_DIR}/NetTarget/CommunityTracks.tsv@/NetTarget/CommunityTracks.tsv"
        "--preload-file=${CMAKE_SOURCE_DIR}/NetTarget/LinuxClient/assets@/NetTarget/LinuxClient/assets"
    )
    set_target_properties(HoverNetGame2Player PROPERTIES SUFFIX ".html"
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/packaging/web/shell.html;${CMAKE_SOURCE_DIR}/packaging/web/web-pre.js")
else()
    add_dependencies(HoverNetGame2Player HoverNetHeadlessObjFac1)
endif()
target_compile_definitions(HoverNetGame2Player PRIVATE _HAVE_SDL2 HOVERNET_GAME2_PLAYER HOVERNET_SOURCE_DIR="${CMAKE_SOURCE_DIR}" HOVERNET_VERSION="${HOVERNET_VERSION}")
target_link_libraries(HoverNetGame2Player PRIVATE HoverNetObserver HoverNetClientSession HoverNetResourceCore HoverNetVideoCore HoverNetRaceServerClient HoverNetImGui SDL2::SDL2)
# SDL rewrites main() to SDL_main() on Windows. Its small platform entry-point
# library supplies WinMain and forwards into that shared client entry point;
# Linux deliberately has no equivalent dependency.
if(WIN32)
    # Keep the descriptive internal CMake target name, but ship the shared
    # Windows client under the player-facing successor to legacy Game2.exe.
    set_target_properties(HoverNetGame2Player PROPERTIES OUTPUT_NAME HoverNet WIN32_EXECUTABLE TRUE)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(HoverNetGame2Player PRIVATE SDL2::SDL2main)
    endif()
endif()

if(BUILD_TESTING AND NOT WIN32)
    # These legacy/portable-core unit executables still depend on POSIX test
    # harnesses and Linux compatibility pre-includes. Windows parity is covered
    # in CI by the shared-client functional and clean-installer gates rather than
    # generating targets that cannot compile with MSVC.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/linux")

    function(add_hovernet_root_test target)
        add_test(NAME ${target} COMMAND ${target} ${ARGN})
        set_tests_properties(${target} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endfunction()

    add_executable(HoverNetTrackCatalogSmoke
        NetTarget/LinuxClient/TrackCatalogSmoke.cpp
        NetTarget/LinuxClient/TrackCatalog.cpp
    )
    add_hovernet_root_test(HoverNetTrackCatalogSmoke)
    add_executable(HoverNetTrackDownloadSmoke
        NetTarget/LinuxClient/TrackDownloadSmoke.cpp
        NetTarget/LinuxClient/TrackDownloader.cpp
        NetTarget/LinuxClient/TrackCatalog.cpp
        NetTarget/LinuxClient/Gunzip.cpp
        NetTarget/LinuxClient/Sha256.cpp
    )
    target_link_libraries(HoverNetTrackDownloadSmoke PRIVATE Threads::Threads)
    add_hovernet_root_test(HoverNetTrackDownloadSmoke)
    add_test(NAME HoverNetRuntimeDependencies
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-runtime-dependencies.sh"
                "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(HoverNetRuntimeDependencies PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME HoverNetReproduciblePackages
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/test-reproducible-packages.sh"
                "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/reproducible-packages-test")
    set_tests_properties(HoverNetReproduciblePackages PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
    add_test(NAME HoverNetCommunityTracks
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/test-community-tracks.sh"
                "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/community-tracks-test")
    set_tests_properties(HoverNetCommunityTracks PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 600)
    add_hovernet_root_test(HoverNetRecordFileSmoke)
    add_hovernet_root_test(HoverNetModelCoreSmoke)
    add_hovernet_root_test(HoverNetLevelSmoke)
    add_hovernet_root_test(HoverNetGameSessionSmoke)
    function(add_hovernet_track_validation track_id track_name)
        add_test(NAME "HoverNetTrackValidation_${track_id}"
            COMMAND HoverNetTrackValidator "NetTarget/Tracks/${track_name}.trk" "${track_name}")
        set_tests_properties("HoverNetTrackValidation_${track_id}"
            PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endfunction()
    add_hovernet_track_validation(ClassicH ClassicH)
    add_hovernet_track_validation(Steeplechase Steeplechase)
    add_hovernet_track_validation(Switchback Switchback)
    add_hovernet_track_validation(The_Alley2 "The Alley2")
    add_hovernet_track_validation(The_River "The River")
    add_hovernet_track_validation(Tidal_Causeway "Tidal Causeway")
    add_hovernet_track_validation(Metro_Spiral "Metro Spiral")
    add_test(NAME HoverNetTrackToolchain
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/test-track-toolchain.sh"
            "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/track-toolchain-test")
    set_tests_properties(HoverNetTrackToolchain PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetAllTracksMultiplayer
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/test-all-tracks-multiplayer.sh"
            "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/all-tracks-multiplayer")
    set_tests_properties(HoverNetAllTracksMultiplayer PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 240)
    add_hovernet_root_test(HoverNetFactorySmoke)
    add_hovernet_root_test(HoverNetResourceSmoke)
    add_hovernet_root_test(HoverNetViewportSmoke)
    add_hovernet_root_test(HoverNetActorRenderSmoke)
    add_hovernet_root_test(HoverNetImGuiLogicalMouseSmoke)
    add_hovernet_root_test(HoverNetRaceServerClientSmoke "$<TARGET_FILE:RaceServer>")
    add_hovernet_root_test(HoverNetServerConfigSmoke
        "${CMAKE_SOURCE_DIR}/NetTarget/RaceServer/config.xml"
        "${CMAKE_BINARY_DIR}/server-config-round-trip.xml")
    add_hovernet_root_test(HoverNetRaceServerLifecycleSmoke "$<TARGET_FILE:RaceServer>")
    add_test(NAME HoverNetRaceServerProtocolFuzz
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/raceserver_protocol_fuzz.py"
        "$<TARGET_FILE:RaceServer>")
    set_tests_properties(HoverNetRaceServerProtocolFuzz PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetRaceServerProtocolStrictSmoke
        COMMAND HoverNetRaceServerClientSmoke "$<TARGET_FILE:RaceServer>" --strict)
    set_tests_properties(HoverNetRaceServerProtocolStrictSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_hovernet_root_test(HoverNetInRaceChatSmoke "$<TARGET_FILE:RaceServer>")
    add_hovernet_root_test(HoverNetObserverChatRenderSmoke)
    add_hovernet_root_test(HoverNetMainCharacterSmoke)
    add_test(NAME HoverNetDeterminism
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-determinism.sh $<TARGET_FILE:HoverNetDeterminismRun>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    # The classic renderer draws fixed scenes to the committed hashes (docs/render-golden.md).
    add_test(NAME HoverNetRenderGolden
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-render-golden.sh $<TARGET_FILE:HoverNetRenderGolden>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    # Netcode prototype at the roadmap's two latency targets (120 ms / 2% loss, 250 ms / 5% loss): both clients must
    # converge to the ideal run bit for bit.
    add_test(NAME HoverNetNetcodeTarget120 COMMAND HoverNetNetcodeProto ClassicH 1200 120 2 --jitter=10)
    add_test(NAME HoverNetNetcodeTarget250 COMMAND HoverNetNetcodeProto "The River" 1200 250 5 --jitter=25)
    set_tests_properties(HoverNetNetcodeTarget120 HoverNetNetcodeTarget250 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetReplayFormat
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-replay.sh $<TARGET_FILE:HoverNetReplay>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetLeaderboard
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-leaderboard.sh $<TARGET_FILE:HoverNetDriver> $<TARGET_FILE:HoverNetReplay>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetSimBoundary
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-sim-boundary.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetSnapshot
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-snapshot.sh $<TARGET_FILE:HoverNetSnapshotTest>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetFrameRateIndependence COMMAND HoverNetFrameRateTest ClassicH 1500)
    set_tests_properties(HoverNetFrameRateIndependence PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetSnapshotFuzz COMMAND HoverNetSnapshotTest Steeplechase 400 900 --fuzz 1500)
    set_tests_properties(HoverNetSnapshotFuzz PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetAuthority COMMAND HoverNetAuthorityTest ClassicH 8)
    add_test(NAME HoverNetAuthorityRiver COMMAND HoverNetAuthorityTest "The River" 8)
    set_tests_properties(HoverNetAuthority HoverNetAuthorityRiver PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetCrypto COMMAND HoverNetCryptoTest)
    set_tests_properties(HoverNetCrypto PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    add_test(NAME HoverNetModeration COMMAND HoverNetModerationTest)
    set_tests_properties(HoverNetModeration PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetLoad COMMAND HoverNetLoadTest 4 8 0.5 2)
    set_tests_properties(HoverNetLoad PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetWebSocket COMMAND HoverNetWebSocketTest)
    add_test(NAME HoverNetEd25519 COMMAND HoverNetEd25519Test)
    add_test(NAME HoverNetLobby COMMAND HoverNetLobbyTest)
    set_tests_properties(HoverNetLobby PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetProtocol COMMAND HoverNetProtocolTest ClassicH 8)
    set_tests_properties(HoverNetProtocol PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetAuthServerTcp
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-authserver.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> 8 500
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(HoverNetAuthServerTcp PROPERTIES ENVIRONMENT "HOVERNET_REPLAY_TOOL=$<TARGET_FILE:HoverNetReplay>")
    add_test(NAME HoverNetPrediction120 COMMAND HoverNetPredictionTest ClassicH 8 60)
    add_test(NAME HoverNetPrediction250 COMMAND HoverNetPredictionTest "The Alley2" 8 125)
    set_tests_properties(HoverNetPrediction120 HoverNetPrediction250 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetReplayViewer
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-replay-viewer.sh $<TARGET_FILE:HoverNetReplay> $<TARGET_FILE:HoverNetGame2Player> $<TARGET_FILE:HoverNetDriver>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetClientReplay
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-client-replay.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetGame2Player> $<TARGET_FILE:HoverNetReplay>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetAuthClient
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-authclient.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetGame2Player> 8 400
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetAuthClientReconnect
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-authclient.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetGame2Player> 4 600 "The River"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(HoverNetAuthClientReconnect PROPERTIES ENVIRONMENT "AUTH_DROP=200")
    add_test(NAME HoverNetLobbyTcp
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-lobby-tcp.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> 400
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    if(UNIX)
        add_test(NAME HoverNetOnlineLobby
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-online-lobby.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetOnlineLobbyTest>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetOnlineScreenClicks
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-online-screen-clicks.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetRaceView
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-race-view.sh $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetPersonalBestsFlow
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-personal-bests.sh $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetReplayLibraryScreen
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-replay-library.sh $<TARGET_FILE:HoverNetReplay> $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetGhost
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-ghost.sh $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetFinishFlow
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-finish-flow.sh $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetChatAfterFinish
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-chat-after-finish.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetSpectatorCamera
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-spectator-camera.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetGame2Player> $<TARGET_FILE:HoverNetReplay>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetLobbyAutoRefresh
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-lobby-autorefresh.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetSmoothMotion
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-smooth-motion.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetNetProxy> $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetFreeDrive
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-free-drive.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetStartRace
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-start-race.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetLobbyChat
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-lobby-chat.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetOnlineFallback
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-online-fallback.sh $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetOnlineMenu
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-online-menu.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetGame2Player>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetSigning
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-signing.sh $<TARGET_FILE:HoverNetSign>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetAbuse
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-abuse.sh $<TARGET_FILE:HoverNetAuthServer>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        add_test(NAME HoverNetWebSocketTcp
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-websocket.sh $<TARGET_FILE:HoverNetAuthServer>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()
    if(UNIX)   # needs POSIX signals and curl; the Windows service is stopped differently (docs/operators.md)
        add_test(NAME HoverNetDrain
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-drain.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()
    add_test(NAME HoverNetPrivateTcp
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-private-tcp.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetSpectatorTcp
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-spectator-tcp.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetAuthClientSpectate
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-authclient.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetGame2Player> 3 700
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(HoverNetAuthClientSpectate PROPERTIES ENVIRONMENT "AUTH_SPECTATE=1")
    add_test(NAME HoverNetTrackPack
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-trackpack.sh $<TARGET_FILE:HoverNetTrackPack>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetModerationTcp
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-moderation-tcp.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetModTool>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetObservability
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-observability.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetNetworkConditions
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-network-conditions.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetNetProxy> $<TARGET_FILE:HoverNetGame2Player>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetSecureTcp
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-secure-tcp.sh $<TARGET_FILE:HoverNetAuthServer> $<TARGET_FILE:HoverNetAuthBot> $<TARGET_FILE:HoverNetGame2Player>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME HoverNetPhysicsGolden
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/test-physics.sh $<TARGET_FILE:HoverNetDeterminismRun>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_hovernet_root_test(HoverNetMainCharacterRenderSmoke)
    add_hovernet_root_test(HoverNetClientSessionSmoke)
    add_hovernet_root_test(HoverNetObserverSmoke)

    add_test(NAME HoverNetDynamicFactorySmoke COMMAND HoverNetDynamicFactorySmoke)
    set_tests_properties(HoverNetDynamicFactorySmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    add_hovernet_root_test(HoverNetLinuxBootstrap --frames 1)
    add_hovernet_root_test(HoverNetTrackViewer --frames 1)
    add_hovernet_root_test(HoverNetTrack3DViewer --frames 1)
    add_hovernet_root_test(HoverNetGame2Player --frames 1)
    add_test(NAME HoverNetGame2PlayerAutoplay COMMAND HoverNetGame2Player --autoplay --frames 10)
    set_tests_properties(HoverNetGame2PlayerAutoplay PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetGame2PlayerSwitchback COMMAND HoverNetGame2Player --track Switchback --frames 10)
    set_tests_properties(HoverNetGame2PlayerSwitchback PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetGame2PlayerAudio COMMAND HoverNetGame2Player --autoplay --frames 60)
    set_tests_properties(HoverNetGame2PlayerAudio PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetGame2PlayerCombat COMMAND HoverNetGame2Player --autoplay --fire --frames 60)
    set_tests_properties(HoverNetGame2PlayerCombat PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetGame2PlayerWeaponCycle COMMAND HoverNetGame2Player --select-weapon --frames 10)
    set_tests_properties(HoverNetGame2PlayerWeaponCycle PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetGame2PlayerJump COMMAND HoverNetGame2Player --autoplay --jump --frames 30)
    set_tests_properties(HoverNetGame2PlayerJump PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetGame2PlayerFire COMMAND HoverNetGame2Player --fire --frames 2)
    set_tests_properties(HoverNetGame2PlayerFire PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetGame2PlayerControls COMMAND HoverNetGame2Player --jump --select-weapon --frames 2)
    set_tests_properties(HoverNetGame2PlayerControls PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetSettingsScreenSmoke COMMAND HoverNetGame2Player --settings-screen --frames 2)
    set_tests_properties(HoverNetSettingsScreenSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetControlsReferenceSmoke COMMAND HoverNetGame2Player --controls-reference --frames 2)
    set_tests_properties(HoverNetControlsReferenceSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetPauseMenuSmoke COMMAND HoverNetGame2Player --pause-menu --frames 2)
    set_tests_properties(HoverNetPauseMenuSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetQuitConfirmationSmoke COMMAND HoverNetGame2Player --quit-confirmation --frames 2)
    set_tests_properties(HoverNetQuitConfirmationSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetPostRaceSmoke COMMAND HoverNetGame2Player --results-screen --frames 2)
    set_tests_properties(HoverNetPostRaceSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetOnboardingSmoke COMMAND HoverNetGame2Player --onboarding --frames 2)
    set_tests_properties(HoverNetOnboardingSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetOnboardingConfigSmoke COMMAND HoverNetGame2Player --onboarding-config --frames 2)
    set_tests_properties(HoverNetOnboardingConfigSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetGame2PlayerDebug COMMAND HoverNetGame2Player --debug --frames 1)
    set_tests_properties(HoverNetGame2PlayerDebug PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetTrack3DViewerPowerup COMMAND HoverNetTrack3DViewer --powerup --frames 1)
    set_tests_properties(HoverNetTrack3DViewerPowerup PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # Phase 1 of docs/roadmap-2.0.md calls for "a repeatable frame-time and
    # memory baseline" -- unlike the smoke tests above (a handful of frames,
    # just checking nothing crashes), this runs long enough for a real leak to
    # show up as a front-half-vs-back-half RSS trend (see the check at the end
    # of Track3DViewer.cpp's main()), rather than needing someone to notice a
    # multi-hour session gradually using more memory by hand.
    add_test(NAME HoverNetMemoryBaselineSmoke
        COMMAND HoverNetGame2Player --autoplay --frames 600
                --memory-report "${CMAKE_BINARY_DIR}/memory-baseline-report.csv")
    set_tests_properties(HoverNetMemoryBaselineSmoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    if(HOVERNET_SANITIZE)
        # This test judges a leak by RSS growth over a 600-frame run (limit 20%). Under
        # AddressSanitizer the same run grows 13-22% from the sanitizer's own overhead
        # (shadow/redzone memory), so it passes or fails with machine load; the normal
        # build grows about 0.5%. Real leaks are caught by LeakSanitizer in the rest of
        # the suite, and this check still runs on the non-sanitizer build.
        set_tests_properties(HoverNetMemoryBaselineSmoke PROPERTIES DISABLED TRUE)
    endif()
    set_tests_properties(HoverNetLinuxBootstrap HoverNetTrackViewer HoverNetTrack3DViewer HoverNetGame2Player HoverNetGame2PlayerAutoplay HoverNetGame2PlayerSwitchback HoverNetGame2PlayerAudio HoverNetGame2PlayerCombat HoverNetGame2PlayerWeaponCycle HoverNetGame2PlayerJump HoverNetGame2PlayerFire HoverNetGame2PlayerControls HoverNetSettingsScreenSmoke HoverNetControlsReferenceSmoke HoverNetPauseMenuSmoke HoverNetQuitConfirmationSmoke HoverNetPostRaceSmoke HoverNetOnboardingSmoke HoverNetOnboardingConfigSmoke HoverNetGame2PlayerDebug HoverNetTrack3DViewerPowerup HoverNetMemoryBaselineSmoke
        PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()
