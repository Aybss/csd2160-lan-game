cmake_minimum_required(VERSION 3.20)
project(TankNet VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SFML 3 COMPONENTS Graphics Window System Audio REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)

set(SOURCES
    src/main.cpp
    src/Network.cpp
    src/Persistence.cpp
    src/Tank.cpp
    src/Bullet.cpp
    src/GameServer.cpp
    src/GameClient.cpp
    src/VoiceChat.cpp
)

add_executable(TankNet ${SOURCES})
# Only hide the console if we are NOT in a Debug build
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(TankNet PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
target_include_directories(TankNet PRIVATE include)
target_link_libraries(TankNet PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Audio
    Opus::opus
    unofficial-sodium::sodium
    ws2_32
)

# Copy server.cfg, run.bat and assets next to exe
add_custom_command(TARGET TankNet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/server.cfg
        $<TARGET_FILE_DIR:TankNet>/server.cfg
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/run.bat
        $<TARGET_FILE_DIR:TankNet>/run.bat
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:TankNet>/assets
)

# Copy SFML DLLs
if(WIN32)
    add_custom_command(TARGET TankNet POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:TankNet>
            $<TARGET_FILE_DIR:TankNet>
        COMMAND_EXPAND_LISTS
    )
endif()