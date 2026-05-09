#include "breach_team/game/terminal_game.hpp"

#ifdef BREACH_TEAM_HAS_SDL2
#include "breach_team/game/sdl_game.hpp"
#endif

#include <string_view>

int main(int argc, char** argv) {
#ifdef BREACH_TEAM_HAS_SDL2
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--sdl") {
            return breach_team::game::run_sdl_game();
        }
    }
#endif

    return breach_team::game::run_terminal_game();
}
