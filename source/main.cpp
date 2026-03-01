// Progressions — Arch Linux System Update Manager
// Compiled as the single translation unit. All logic lives in src/*.hpp.

#include "inc/app.hpp"

int main(int argc, char* argv[]) {
    return Progressions::App::instance()->run(argc, argv);
}
