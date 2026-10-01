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

if(NOT MSVC)
    if(HOVERNET_WARNINGS)
        add_compile_options(-Wall -Wextra)
    endif()
    if(HOVERNET_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
        # The 1990s fixed-point software rasterizer (4096-scaled coordinates)
        # relies on 32-bit wraparound when the clipper hands it far off-screen
        # geometry. Auditing every site risks changing rendered output, so only
        # this file skips the signed-overflow check; bounds, shift, alignment
        # and memory checks stay on. Tracked in docs/roadmap-2.0.md.
        set_source_files_properties(NetTarget/VideoServices/3DViewportRendering.cpp
            PROPERTIES COMPILE_OPTIONS -fno-sanitize=signed-integer-overflow)
    endif()
endif()

find_package(SDL2 REQUIRED)
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
)

target_link_libraries(HoverNetModelCore PUBLIC HoverNetPortableUtil)
if(WIN32)
    target_link_libraries(HoverNetModelCore PUBLIC winmm)
endif()
set_target_properties(HoverNetModelCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(HoverNetMainCharacter STATIC
    NetTarget/MainCharacter/MainCharacter.cpp
    NetTarget/MainCharacter/MainCharacterRenderer.cpp
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
    NetTarget/GraphicsSDL2/SDL2Graphics.cpp
)

add_dependencies(HoverNetGame2Player HoverNetHeadlessObjFac1)
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
