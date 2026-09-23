cmake_minimum_required(VERSION 3.16)
project(RaceServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Add source files
add_executable(RaceServer
    stdafx.cpp
    RaceServer.cpp
    NetworkInterface/ServerSocket.cpp
    NetworkInterface/ClientConnection.cpp
    NetworkInterface/MessageDispatcher.cpp
    GameSimulation/GameSimulation.cpp
    RaceManager/RaceManager.cpp
    RaceManager/RaceSession.cpp
    Config/ServerConfig.cpp
    Logging/ServerLogger.cpp
)

# Link Windows socket library
if(WIN32)
    target_link_libraries(RaceServer ws2_32 winmm)
endif()
target_link_libraries(RaceServer PRIVATE Threads::Threads)

# Include directories
target_include_directories(RaceServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/NetworkInterface
    ${CMAKE_CURRENT_SOURCE_DIR}/GameSimulation
    ${CMAKE_CURRENT_SOURCE_DIR}/RaceManager
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Logging
)

# Set output directory
set_target_properties(RaceServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)

install(TARGETS RaceServer RUNTIME DESTINATION lib/hovernet)
install(FILES config.xml DESTINATION etc/hovernet)
