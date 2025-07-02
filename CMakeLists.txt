cmake_minimum_required(VERSION 3.16)
project(tui-music-player)
include(FetchContent)

FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9  # Replace with a version, tag, or commit hash
)
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.3  # or latest stable
)

FetchContent_MakeAvailable(ftxui)

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
)

