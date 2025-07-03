cmake_minimum_required(VERSION 3.16)
project(tui-music-player)
include(FetchContent)

FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9  # Replace with a version, tag, or commit hash
)
FetchContent_MakeAvailable(ftxui)


set(DCMAKE_EXPORT_COMPILE_COMMANDS=1)
find_package(FFTW3 REQUIRED)
add_executable(
    ${PROJECT_NAME}
    src/main.cpp
    src/miniaudio/miniaudio.c
    src/cava/cavacore.c
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE ftxui::screen
    PRIVATE ftxui::dom
    PRIVATE ftxui::component
    PRIVATE
        ${FFTW3_LIBRARIES}
)

