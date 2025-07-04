#include <pthread.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "miniaudio/miniaudio.h"

// Global
std::atomic<bool> g_paused(false);
std::atomic<bool> g_quit_app(false);
std::atomic<double> g_current_progress(0.0);
std::atomic<bool> g_audio_finished(false);
std::atomic<bool> g_seek_requested(false);
std::atomic<double> g_seek_position_percent(0.0);
std::atomic<bool> g_skip_next(false);
std::atomic<bool> g_skip_prev(false);
std::atomic<int> g_current_track_index(0);

std::vector<std::string> g_playlist;

void audio_playback_thread_logic() {
    ma_engine engine;
    ma_sound song;
    ma_uint64 total_length_in_pcm_frames;
    bool song_loaded = false;

    // Init audio engine
    if (ma_engine_init(nullptr, &engine) != MA_SUCCESS) {
        std::cerr << "Audio: miniaudio error\n";
        g_audio_finished = true;
        return;
    }

    while (!g_quit_app) {
        int current_index = g_current_track_index.load();

        if (g_skip_next.load()) {
            if (song_loaded) {
                ma_sound_stop(&song);
                ma_sound_uninit(&song);
                song_loaded = false;
            }
            current_index = (current_index + 1) % g_playlist.size();
            g_current_track_index = current_index;
            g_current_progress = 0.0;
            g_skip_next = false;
        }

        if (g_skip_prev.load()) {
            if (song_loaded) {
                ma_sound_stop(&song);
                ma_sound_uninit(&song);
                song_loaded = false;
            }
            current_index =
                (current_index - 1 + g_playlist.size()) % g_playlist.size();
            g_current_track_index = current_index;
            g_current_progress = 0.0;
            g_skip_prev = false;
        }

        // Load new song if needed
        if (!song_loaded && current_index < g_playlist.size()) {
            const char* filepath = g_playlist[current_index].c_str();

            if (ma_sound_init_from_file(&engine, filepath, MA_SOUND_FLAG_STREAM,
                                        nullptr, nullptr,
                                        &song) != MA_SUCCESS) {
                std::cerr << "Audio: no such file " << filepath << "\n";
                // Skip to next track if current one fails
                current_index = (current_index + 1) % g_playlist.size();
                g_current_track_index = current_index;
                continue;
            }

            if (ma_sound_get_length_in_pcm_frames(
                    &song, &total_length_in_pcm_frames) != MA_SUCCESS) {
                std::cerr << "Audio thread failed to get length\n";
                ma_sound_uninit(&song);
                current_index = (current_index + 1) % g_playlist.size();
                g_current_track_index = current_index;
                continue;
            }

            song_loaded = true;
            g_audio_finished = false;

            ma_result seek_result = ma_sound_seek_to_pcm_frame(&song, 0);
            if (seek_result != MA_SUCCESS) {
                std::cerr << "Faile to start: "
                          << ma_result_description(seek_result) << "\n";
            }
            g_current_progress = 0.0;
        }

        if (!song_loaded) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        ma_uint32 sample_rate = ma_engine_get_sample_rate(&engine);
        double total_duration_seconds =
            static_cast<double>(total_length_in_pcm_frames) / sample_rate;

        // Seek
        if (g_seek_requested.load()) {
            double target_percent = g_seek_position_percent.load();
            ma_uint64 seek_frame = static_cast<ma_uint64>(
                (target_percent / 100.0) * total_length_in_pcm_frames);

            ma_result seek_result =
                ma_sound_seek_to_pcm_frame(&song, seek_frame);
            if (seek_result == MA_SUCCESS) {
                g_current_progress = target_percent;
            } else {
                std::cerr << "Audio Thread: Failed to seek: "
                          << ma_result_description(seek_result) << "\n";
            }
            g_seek_requested = false;
        }

        // Pause/play
        if (g_paused) {
            if (ma_sound_is_playing(&song)) {
                ma_sound_stop(&song);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } else {
            if (!ma_sound_is_playing(&song)) {
                ma_uint64 cursor_in_pcm_frames;
                if (ma_sound_get_cursor_in_pcm_frames(
                        &song, &cursor_in_pcm_frames) == MA_SUCCESS) {
                    if (cursor_in_pcm_frames >=
                        total_length_in_pcm_frames - 100) {
                        ma_sound_stop(&song);
                        ma_sound_uninit(&song);
                        song_loaded = false;
                        current_index = (current_index + 1) % g_playlist.size();
                        g_current_track_index = current_index;
                        g_current_progress = 0.0;
                        continue;
                    }
                }

                if (ma_sound_start(&song) != MA_SUCCESS) {
                    std::cerr << "Audio thread: Failed to resume sound\n";
                    break;
                }
            }

            // Update
            if (ma_sound_is_playing(&song)) {
                ma_uint64 cursor_in_pcm_frames;
                if (ma_sound_get_cursor_in_pcm_frames(
                        &song, &cursor_in_pcm_frames) == MA_SUCCESS) {
                    double current_time_seconds =
                        static_cast<double>(cursor_in_pcm_frames) / sample_rate;
                    g_current_progress =
                        (current_time_seconds / total_duration_seconds) * 100.0;
                } else {
                    std::cerr << "Audio no cursor\n";
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup
    if (song_loaded) {
        if (ma_sound_is_playing(&song)) {
            ma_sound_stop(&song);
        }
        ma_sound_uninit(&song);
    }
    ma_engine_uninit(&engine);

    std::cout << "Audio exit\n";
}

std::string get_filename_from_path(const std::string& path) {
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        return path.substr(last_slash + 1);
    }
    return path;
}

void play_playlist(const std::vector<std::string>& playlist) {
    g_playlist = playlist;
    g_current_track_index = 0;

    auto screen = ftxui::ScreenInteractive::TerminalOutput();
    std::string progress_display_text = "0.00%";
    std::string button_label = "Pause";
    float ui_slider_value = 0.0;

    // UI
    auto pause_button = ftxui::Button(&button_label, [&] {
        if (!g_audio_finished) {
            g_paused = !g_paused;
        }
    });
    auto prev_button = ftxui::Button(" << ", [&] {
        g_skip_prev = true;
        g_paused = false;
        ui_slider_value = 0.0f;
        g_current_progress = 0.0;
        g_seek_requested = false;
    });
    auto next_button = ftxui::Button(" >> ", [&] {
        g_skip_next = true;
        g_paused = false;
        ui_slider_value = 0.0f;
        g_current_progress = 0.0;
        g_seek_requested = false;
    });
    auto process_slider = ftxui::Slider("", &ui_slider_value, 0.0, 100.0, 0.1);

    // Container for interactive components
    auto main_container = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            prev_button,
            pause_button,
            next_button,
        }),
        process_slider,
    });

    process_slider->TakeFocus();

    // Renderer
    auto renderer = ftxui::Renderer(main_container, [&]() -> ftxui::Element {
        double current_p = g_current_progress.load();
        int current_track = g_current_track_index.load();

        std::stringstream ss;
        ss << std::fixed << std::setprecision(3) << current_p << "%";
        progress_display_text = ss.str();

        // Handle slider interaction
        if (std::abs(ui_slider_value - current_p) > 0.5 &&
            !g_audio_finished.load()) {
            g_seek_position_percent = ui_slider_value;
            g_seek_requested = true;
        } else {
            if (!g_seek_requested.load()) {
                ui_slider_value = current_p;
            }
        }

        // Update button labels
        if (g_audio_finished) {
            button_label = "Finished";
        } else if (g_paused) {
            button_label = " |> ";
        } else {
            button_label = " || ";
        }

        // Current track info
        std::string current_file = "No track";
        if (current_track < g_playlist.size()) {
            current_file = get_filename_from_path(g_playlist[current_track]);
        }

        std::string track_info = "Track " + std::to_string(current_track + 1) +
                                 "/" + std::to_string(g_playlist.size());

        return ftxui::vbox({
                   ftxui::hbox({
                       ftxui::text("File: "),
                       ftxui::text(current_file) | ftxui::flex |
                           ftxui::color(ftxui::Color::Green),
                   }),
                   ftxui::hbox({
                       ftxui::text(track_info) |
                           ftxui::color(ftxui::Color::Blue),
                       ftxui::hbox({
                           ftxui::text("Progress: "),
                           ftxui::text(progress_display_text) |
                               ftxui::color(ftxui::Color::Green),
                       }) | ftxui::align_right |
                           ftxui::flex,
                   }),
                   ftxui::separator(),
                   ftxui::hbox({
                       process_slider->Render(),
                   }),
                   ftxui::separator(),
                   ftxui::hbox({
                       prev_button->Render() | ftxui::center | ftxui::flex,
                       ftxui::text(" "),
                       pause_button->Render() | ftxui::flex,
                       ftxui::text(" "),
                       next_button->Render() | ftxui::flex,
                   }) | ftxui::center,
                   ftxui::separator(),
                   ftxui::text("Controls: Space:Play/Pause, ←:Previous, "
                               "→:Next, ↑↓ Seek") |
                       ftxui::dim | ftxui::center,
               }) |
               ftxui::border;
    });

    // Keyboard
    auto keyboard_handler =
        ftxui::CatchEvent(renderer, [&](ftxui::Event event) -> bool {
            if (event == ftxui::Event::Character(' ')) {
                g_paused = !g_paused;
                return true;
            }
            if (event == ftxui::Event::ArrowLeft) {
                g_skip_prev = true;
                g_paused = false;
                ui_slider_value = 0.0f;
                g_current_progress = 0.0;
                g_seek_requested = false;
                return true;
            }
            if (event == ftxui::Event::ArrowRight) {
                g_skip_next = true;
                g_paused = false;
                ui_slider_value = 0.0f;
                g_current_progress = 0.0;
                g_seek_requested = false;
                return true;
            }
            if (event == ftxui::Event::ArrowUp) {
                ui_slider_value = std::min(100.0f, ui_slider_value + 5.0f);
                g_seek_position_percent = ui_slider_value;
                g_seek_requested = true;
                return true;
            }
            if (event == ftxui::Event::ArrowDown) {
                ui_slider_value = std::max(0.0f, ui_slider_value - 5.0f);
                g_seek_position_percent = ui_slider_value;
                g_seek_requested = true;
                return true;
            }
            return false;
        });

    // Start threads 😵
    std::thread audio_thread(audio_playback_thread_logic);
    std::thread ui_update_thread([&] {
        while (!g_quit_app) {
            screen.PostEvent(ftxui::Event::Custom);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "UI exit.\n";
    });

    screen.Loop(keyboard_handler);
    g_quit_app = true;

    // WTF is join threads anyway
    if (audio_thread.joinable()) {
        audio_thread.join();
    }
    if (ui_update_thread.joinable()) {
        ui_update_thread.join();
    }
    std::cout << "Player finished.\n";
}

std::vector<std::string> parse_file(int argc, char** argv) {
    std::vector<std::string> playlist;
    for (int i = 2; i < argc; ++i) {
        playlist.push_back(argv[i]);
    }
    return playlist;
}

std::vector<std::string> parse_folder(int argc, char** argv) {
    std::vector<std::string> playlist;

    return playlist;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " {file/folder} [audio_file1] [audio_file2] [...]\n";
        return 1;
    }

    std::string arg1(argv[1]);

    if (arg1 != "link" && arg1 != "file") {
        std::cerr << "Usage: " << argv[0]
                  << " {file/folder} [audio_file1] [audio_file2] [...]\n";
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        std::filesystem::path path(argv[i]);
        if (!std::filesystem::is_regular_file(path)) {
            std::cerr << argv[i] <<" is not a file\n"
                      << "Usage: " << argv[0]
                      << " {file/link} [audio_file1] [audio_file2] [...]\n";
            return 1;
        }
    }

    std::vector<std::string> playlist;

    if (arg1 == "file") {
        playlist = parse_file(argc, argv);
    }

    std::cout << "Playlist: " << playlist.size() << " tracks:\n";
    for (size_t i = 0; i < playlist.size(); ++i) {
        std::cout << "  " << (i + 1) << ". "
                  << get_filename_from_path(playlist[i]) << "\n";
    }
    std::cout << "\n";

    play_playlist(playlist);
}
