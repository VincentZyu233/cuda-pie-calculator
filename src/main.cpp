#include "pi_engine.hpp"
#include "tui.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        pie::PiEngine engine;
        pie::TerminalUi ui;
        return ui.run(engine);
    } catch (const std::exception& error) {
        std::cerr << "cuda-pie-calculator: " << error.what() << '\n';
        return 1;
    }
}
