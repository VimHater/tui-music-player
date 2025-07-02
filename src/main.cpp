#include <atomic>
#include <chrono>
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
#include <thread>
#include "miniaudio.h"

// Global
std::atomic<bool> g_paused(false);
std::atomic<bool> g_quit_app(false);
std::atomic<double> g_current_progress(0.0);
std::atomic<bool> g_audio_finished(false);

void audio_playback_thread_logic(const char* filepath) {
    ma_engine engine;
    ma_sound song;
    ma_uint64 total_length_in_pcm_frames;

    // Check audio error
    if (ma_engine_init(nullptr, &engine) != MA_SUCCESS) {
        std::cerr << "Audio: miniaudio error\n";
        g_audio_finished = true;
        return;
    }
    if (ma_sound_init_from_file(&engine, filepath, MA_SOUND_FLAG_STREAM,
                                nullptr, nullptr, &song) != MA_SUCCESS) {
        std::cerr << "Audio: no such file" << filepath << "\n";
        ma_engine_uninit(&engine);
        g_audio_finished = true;
        return;
    }
    if (ma_sound_start(&song) != MA_SUCCESS) {
        std::cerr << "Audio thread: cant start sound\n";
        ma_sound_uninit(&song);
        ma_engine_uninit(&engine);
        g_audio_finished = true;
        return;
    }
    if (ma_sound_get_length_in_pcm_frames(&song, &total_length_in_pcm_frames) !=
        MA_SUCCESS) {
        std::cerr << "Audio thread failed\n";
        ma_sound_stop(&song);
        ma_sound_uninit(&song);
        ma_engine_uninit(&engine);
        g_audio_finished = true;
        return;
    }

    ma_uint32 sample_rate = ma_engine_get_sample_rate(&engine);
    double total_duration_seconds =
        static_cast<double>(total_length_in_pcm_frames) / sample_rate;

    while (!g_quit_app && !g_audio_finished) {
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
                        g_audio_finished = true;
                        g_current_progress = 100.0;
                        break;
                    }
                }

                if (!g_audio_finished) {
                    if (ma_sound_start(&song) != MA_SUCCESS) {
                        std::cerr << "Audio thread: Failed to resume sound\n";
                        g_audio_finished = true;
                        break;
                    }
                }
            }

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

    if (ma_sound_is_playing(&song)) {
        ma_sound_stop(&song);
    }
    ma_sound_uninit(&song);
    ma_engine_uninit(&engine);

    if (!g_quit_app && !g_audio_finished) {
        g_audio_finished = true;
        g_current_progress = 100.0;
    }

    std::cout << "Audio exit\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_file_path>\n";
        return 1;
    }
    const char* audio_file_path = argv[1];

    auto screen = ftxui::ScreenInteractive::TerminalOutput();

    std::string progress_display_text = "0.00%";
    std::string buttonLable = "Pause";

    auto pauseButton = ftxui::Button(&buttonLable, [&] {
        if (!g_audio_finished) {
            g_paused = !g_paused;
            screen.PostEvent(ftxui::Event::Custom);
        }
    });

    // Container must be declare for interactive component
    auto main_container = ftxui::Container::Vertical({
        pauseButton,
    });

    // Who decided to make the entire UI lib pure functional T_T
    auto renderer = ftxui::Renderer(main_container, [&]() -> ftxui::Element {
        double current_p = g_current_progress.load();
        std::stringstream ss;
        ss << std::fixed << std::setprecision(3) << current_p << "%";
        progress_display_text = ss.str();

        ftxui::ButtonOption button_option;

        if (g_audio_finished) {
            buttonLable = "Finished";
        } else if (g_paused) {
            buttonLable = "Play ";
        } else if (!g_paused) {
            buttonLable = "Pause";
        }

        return ftxui::vbox({
                   ftxui::hbox({
                       ftxui::text(audio_file_path) | ftxui::flex,
                   }),
                   ftxui::separator(),
                   ftxui::hbox({
                       pauseButton->Render(),
                       ftxui::separator(),
                       ftxui::text("Progress: "),
                       ftxui::text(progress_display_text) | ftxui::flex |
                           ftxui::align_right |
                           ftxui::color(ftxui::Color::Green),
                   }),
                   ftxui::separator(),
                   ftxui::gauge(current_p / 100.0) |
                       ftxui::color(ftxui::Color::Green),
                   ftxui::separator(),
               }) |
               ftxui::border;
    });

    // TODO: Threading
    std::thread audio_thread(audio_playback_thread_logic, audio_file_path);
    std::thread ui_update_thread([&] {
        while (!g_quit_app) {
            screen.PostEvent(ftxui::Event::Custom);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "UI exit.\n";
    });

    // TODO: Render
    screen.Loop(renderer);

    g_quit_app = true;

    // TODO: Join thread
    if (audio_thread.joinable()) {
        audio_thread.join();
    }
    if (ui_update_thread.joinable()) {
        ui_update_thread.join();
    }

    std::cout << "It actually work???.\n";
}
