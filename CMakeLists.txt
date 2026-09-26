cmake_minimum_required(VERSION 3.16)

project(HoverNetLinux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
include(CTest)
find_package(Threads REQUIRED)

# Shown in the Linux client's window title (see SDL2Graphics.cpp). Matches the
# vX.Y.Z tags CI builds releases from (see .gitlab-ci.yml's PACKAGE_VERSION);
# an untagged dev build instead gets the nearest tag plus commit count/hash
# (or just the hash with no tags yet), and "dev" if git isn't available at all
# (e.g. building from a source tarball with no .git directory).
find_package(Git QUIET)
set(HOVERNET_VERSION "dev")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE HOVERNET_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE HOVERNET_GIT_DESCRIBE_RESULT
    )
    if(HOVERNET_GIT_DESCRIBE_RESULT EQUAL 0 AND NOT HOVERNET_VERSION_OUTPUT STREQUAL "")
        set(HOVERNET_VERSION "${HOVERNET_VERSION_OUTPUT}")
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

add_executable(HoverNetTrackLoadSmoke
    NetTarget/LinuxClient/TrackLoadSmoke.cpp
)

add_dependencies(HoverNetTrackLoadSmoke HoverNetHeadlessObjFac1)
target_link_libraries(HoverNetTrackLoadSmoke PRIVATE HoverNetModelCore HoverNetPortableUtil)

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

add_library(HoverNetRaceServerClient STATIC
    NetTarget/LinuxClient/RaceServerClient.cpp
)
target_include_directories(HoverNetRaceServerClient PUBLIC NetTarget/LinuxClient)

add_executable(HoverNetRaceServerClientSmoke
    NetTarget/LinuxClient/RaceServerClientSmoke.cpp
)
add_dependencies(HoverNetRaceServerClientSmoke RaceServer)
target_link_libraries(HoverNetRaceServerClientSmoke PRIVATE HoverNetRaceServerClient)

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

add_executable(HoverNetTrack3DViewer
    NetTarget/LinuxClient/Track3DViewer.cpp
    NetTarget/GraphicsSDL2/SDL2Graphics.cpp
)

add_dependencies(HoverNetTrack3DViewer HoverNetHeadlessObjFac1)
target_compile_definitions(HoverNetTrack3DViewer PRIVATE _HAVE_SDL2 HOVERNET_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(HoverNetTrack3DViewer PRIVATE HoverNetClientSession HoverNetResourceCore HoverNetVideoCore SDL2::SDL2)

add_executable(HoverNetGame2Player
    NetTarget/LinuxClient/Track3DViewer.cpp
    NetTarget/GraphicsSDL2/SDL2Graphics.cpp
)

add_dependencies(HoverNetGame2Player HoverNetHeadlessObjFac1)
target_compile_definitions(HoverNetGame2Player PRIVATE _HAVE_SDL2 HOVERNET_GAME2_PLAYER HOVERNET_SOURCE_DIR="${CMAKE_SOURCE_DIR}" HOVERNET_VERSION="${HOVERNET_VERSION}")
target_link_libraries(HoverNetGame2Player PRIVATE HoverNetObserver HoverNetClientSession HoverNetResourceCore HoverNetVideoCore HoverNetRaceServerClient HoverNetImGui SDL2::SDL2)

if(BUILD_TESTING)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/linux")

    function(add_hovernet_root_test target)
        add_test(NAME ${target} COMMAND ${target} ${ARGN})
        set_tests_properties(${target} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endfunction()

    add_hovernet_root_test(HoverNetRecordFileSmoke)
    add_hovernet_root_test(HoverNetModelCoreSmoke)
    add_hovernet_root_test(HoverNetLevelSmoke)
    add_hovernet_root_test(HoverNetGameSessionSmoke)
    add_hovernet_root_test(HoverNetTrackLoadSmoke)
    add_hovernet_root_test(HoverNetFactorySmoke)
    add_hovernet_root_test(HoverNetResourceSmoke)
    add_hovernet_root_test(HoverNetViewportSmoke)
    add_hovernet_root_test(HoverNetActorRenderSmoke)
    add_hovernet_root_test(HoverNetRaceServerClientSmoke "$<TARGET_FILE:RaceServer>")
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
    add_test(NAME HoverNetGame2PlayerDebug COMMAND HoverNetGame2Player --debug --frames 1)
    set_tests_properties(HoverNetGame2PlayerDebug PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME HoverNetTrack3DViewerPowerup COMMAND HoverNetTrack3DViewer --powerup --frames 1)
    set_tests_properties(HoverNetTrack3DViewerPowerup PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(HoverNetLinuxBootstrap HoverNetTrackViewer HoverNetTrack3DViewer HoverNetGame2Player HoverNetGame2PlayerAutoplay HoverNetGame2PlayerAudio HoverNetGame2PlayerCombat HoverNetGame2PlayerWeaponCycle HoverNetGame2PlayerJump HoverNetGame2PlayerFire HoverNetGame2PlayerControls HoverNetGame2PlayerDebug HoverNetTrack3DViewerPowerup
        PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()