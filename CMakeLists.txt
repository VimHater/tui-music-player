cmake_minimum_required(VERSION 3.16)
project(tui-music-player)
include(FetchContent)

FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9  # Replace with a version, tag, or commit hash
)
FetchContent_MakeAvailable(ftxui)

include_directories(
    ${PROJECT_SOURCE_DIR}/cava
)

set(DCMAKE_EXPORT_COMPILE_COMMANDS=1)

add_executable(
    ${PROJECT_NAME}
    src/main.cpp
    src/miniaudio.c
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE ftxui::screen
    PRIVATE ftxui::dom
    PRIVATE ftxui::component
    PRIVATE ${PROJECT_SOURCE_DIR}/cava/cavacore.c
)

