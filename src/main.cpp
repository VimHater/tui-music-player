#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include "miniaudio.h"
void renderUI() {
    using namespace ftxui;
    auto screen = ScreenInteractive::TerminalOutput();

    std::string input_txt;
    std::string output_txt;

    auto input = Input(&input_txt, "Type something...");
    auto button = Button("Submit", [&]{
        output_txt = "You typed: " + input_txt;
    });

    auto container = Container::Vertical({
        input,
        button,
    });

    auto renderer = Renderer(container, [&] {
        return vbox({
            hbox("Input: "),
            input->Render(),
            button->Render(),
            text(output_txt) | color(Color::DarkGreen),

        }) | borderRounded;
    });

    screen.Loop(renderer);
}
int main(int argc, char** argv) {
    renderUI();
}
