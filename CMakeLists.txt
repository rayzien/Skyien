cmake_minimum_required(VERSION 3.20)
project(Skyien VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pipe -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    $ENV{HOME}/.local/include
    $ENV{HOME}/.local/include/TDLib1.8.38
    $ENV{HOME}/.local/include/jsoncpp
    /usr/include
    /data/data/com.termux/files/usr/include
)

link_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    $ENV{HOME}/.local/lib
    $ENV{HOME}/.local/lib/TDLib1.8.38
    /usr/lib/x86_64-linux-gnu
    /data/data/com.termux/files/usr/lib
)

set(SOURCES
    src/Config.cpp
    src/TerminalUI.cpp
    src/TelegramClient.cpp
    src/AutomationEngine.cpp
    src/BotBridge.cpp
    src/Targetor.cpp
    src/GroupFinder.cpp
    src/UserMonitorDB.cpp
    src/UserCommandHandler.cpp
    src/main.cpp
)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
add_executable(skyien ${SOURCES})

set_target_properties(skyien PROPERTIES
    BUILD_RPATH "$ORIGIN/../lib;$ENV{HOME}/.local/lib;$ENV{HOME}/.local/lib/TDLib1.8.38"
    INSTALL_RPATH "$ORIGIN/../lib;$ENV{HOME}/.local/lib;$ENV{HOME}/.local/lib/TDLib1.8.38"
)

target_link_libraries(skyien
    PRIVATE
    tdjson
    jsoncpp
    curl
    sqlcipher
    pthread
)

install(TARGETS skyien DESTINATION $ENV{HOME}/bin)
