#include "breach_team/game/terminal_game.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "breach_team/core/math.hpp"

namespace breach_team::game {

namespace {

using core::Fixed16;
using core::Mat2Fixed;
using core::Vec2Fixed;

constexpr int DEFAULT_SCREEN_W = 140;
constexpr int DEFAULT_SCREEN_H = 40;
constexpr int MAP_W = 28;
constexpr int MAP_H = 20;

constexpr Fixed16 MOVE_SPEED = Fixed16::from_double(0.17);
constexpr Fixed16 ROT_COS = Fixed16::from_double(0.9969);
constexpr Fixed16 ROT_SIN = Fixed16::from_double(0.0784);  // ~4.5deg

struct TerminalGuard {
    TerminalGuard() {
        active_ = (tcgetattr(STDIN_FILENO, &old_) == 0);
        if (!active_) {
            return;
        }

        termios current = old_;
        current.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
        current.c_cc[VMIN] = 0;
        current.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &current) != 0) {
            active_ = false;
            return;
        }

        std::cout << "\x1b[2J\x1b[?25l";
        std::cout.flush();
    }

    ~TerminalGuard() {
        if (active_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &old_);
        }
        std::cout << "\x1b[?25h\x1b[0m\n";
        std::cout.flush();
    }

    bool ok() const {
        return active_;
    }

private:
    termios old_{};
    bool active_ = false;
};

struct Enemy {
    Vec2Fixed pos;
    int hp = 2;
    int attack_cooldown = 0;
};

struct Viewport {
    int width = DEFAULT_SCREEN_W;
    int height = DEFAULT_SCREEN_H;
};

Viewport query_viewport() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        const int cols = static_cast<int>(ws.ws_col);
        const int rows = static_cast<int>(ws.ws_row);
        if (cols >= 80 && rows >= 28) {
            return {cols, rows};
        }
    }
    return {DEFAULT_SCREEN_W, DEFAULT_SCREEN_H};
}

bool can_read_input() {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    const int result = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout);
    return result > 0 && FD_ISSET(STDIN_FILENO, &set);
}

bool in_bounds(int x, int y) {
    return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H;
}

bool is_wall(const std::vector<std::string>& map, int x, int y) {
    if (!in_bounds(x, y)) {
        return true;
    }
    return map[y][x] == '#';
}

bool can_walk(const std::vector<std::string>& map, Fixed16 x, Fixed16 y, const std::vector<Enemy>& enemies) {
    const int gx = static_cast<int>(x.to_double());
    const int gy = static_cast<int>(y.to_double());
    if (is_wall(map, gx, gy)) {
        return false;
    }

    for (const auto& enemy : enemies) {
        if (enemy.hp <= 0) {
            continue;
        }
        const double dx = enemy.pos.x.to_double() - x.to_double();
        const double dy = enemy.pos.y.to_double() - y.to_double();
        if ((dx * dx + dy * dy) < 0.2) {
            return false;
        }
    }
    return true;
}

bool can_walk_enemy(
    const std::vector<std::string>& map,
    Fixed16 x,
    Fixed16 y,
    const std::vector<Enemy>& enemies,
    std::size_t self_index
) {
    const int gx = static_cast<int>(x.to_double());
    const int gy = static_cast<int>(y.to_double());
    if (is_wall(map, gx, gy)) {
        return false;
    }

    for (std::size_t i = 0; i < enemies.size(); ++i) {
        if (i == self_index || enemies[i].hp <= 0) {
            continue;
        }
        const double dx = enemies[i].pos.x.to_double() - x.to_double();
        const double dy = enemies[i].pos.y.to_double() - y.to_double();
        if ((dx * dx + dy * dy) < 0.2) {
            return false;
        }
    }
    return true;
}

Mat2Fixed rotation_matrix(bool left) {
    if (left) {
        return {ROT_COS, Fixed16::from_raw(-ROT_SIN.raw()), ROT_SIN, ROT_COS};
    }
    return {ROT_COS, ROT_SIN, Fixed16::from_raw(-ROT_SIN.raw()), ROT_COS};
}

void rotate_player(Vec2Fixed& dir, Vec2Fixed& plane, bool left) {
    const auto rot = rotation_matrix(left);
    dir = rot * dir;
    plane = rot * plane;
}

char wall_shade(double distance, bool side_hit) {
    if (distance < 1.2) {
        return side_hit ? '@' : '#';
    }
    if (distance < 2.8) {
        return side_hit ? '%' : 'X';
    }
    if (distance < 4.8) {
        return side_hit ? 'x' : '=';
    }
    if (distance < 7.0) {
        return side_hit ? '+' : '-';
    }
    return '.';
}

void draw_minimap(
    std::vector<std::string>& frame,
    const std::vector<std::string>& map,
    const Vec2Fixed& player,
    const std::vector<Enemy>& enemies
) {
    const int origin_x = 0;
    const int origin_y = 0;
    const int frame_h = static_cast<int>(frame.size());
    const int frame_w = frame.empty() ? 0 : static_cast<int>(frame.front().size());

    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            if (origin_y + y >= frame_h || origin_x + x >= frame_w) {
                continue;
            }
            frame[origin_y + y][origin_x + x] = map[y][x];
        }
    }

    const int px = static_cast<int>(player.x.to_double());
    const int py = static_cast<int>(player.y.to_double());
    if (in_bounds(px, py)) {
        frame[origin_y + py][origin_x + px] = '@';
    }

    for (const auto& enemy : enemies) {
        if (enemy.hp <= 0) {
            continue;
        }
        const int ex = static_cast<int>(enemy.pos.x.to_double());
        const int ey = static_cast<int>(enemy.pos.y.to_double());
        if (in_bounds(ex, ey)) {
            frame[origin_y + ey][origin_x + ex] = 'e';
        }
    }
}

void draw_weapon_overlay(std::vector<std::string>& frame) {
    if (frame.empty() || frame.front().empty()) {
        return;
    }
    const int screen_h = static_cast<int>(frame.size());
    const int screen_w = static_cast<int>(frame.front().size());
    const int center_x = screen_w / 2;
    const int base_y = screen_h - 2;
    if (base_y < 4) {
        return;
    }

    const std::vector<std::string> gun{
        "        ___        ",
        "   ____/___\\____   ",
        "==|____  _  ____|==",
        "       |___|       ",
    };

    const int gun_w = static_cast<int>(gun.front().size());
    const int left = std::max(0, center_x - gun_w / 2);
    const int top = base_y - static_cast<int>(gun.size()) + 1;

    for (int gy = 0; gy < static_cast<int>(gun.size()); ++gy) {
        const int fy = top + gy;
        if (fy < 0 || fy >= screen_h) {
            continue;
        }
        for (int gx = 0; gx < gun_w; ++gx) {
            const int fx = left + gx;
            if (fx < 0 || fx >= screen_w) {
                continue;
            }
            const char pixel = gun[gy][gx];
            if (pixel != ' ') {
                frame[fy][fx] = pixel;
            }
        }
    }

    if (screen_h / 2 >= 0 && screen_h / 2 < screen_h && center_x >= 0 && center_x < screen_w) {
        frame[screen_h / 2][center_x] = '+';
    }
}

bool hitscan_enemy(
    const std::vector<std::string>& map,
    const Vec2Fixed& origin,
    const Vec2Fixed& dir,
    std::vector<Enemy>& enemies
) {
    double ray_x = origin.x.to_double();
    double ray_y = origin.y.to_double();
    const double dx = dir.x.to_double();
    const double dy = dir.y.to_double();

    for (int step = 0; step < 90; ++step) {
        ray_x += dx * 0.12;
        ray_y += dy * 0.12;

        if (is_wall(map, static_cast<int>(ray_x), static_cast<int>(ray_y))) {
            return false;
        }

        for (auto& enemy : enemies) {
            if (enemy.hp <= 0) {
                continue;
            }
            const double ex = enemy.pos.x.to_double();
            const double ey = enemy.pos.y.to_double();
            const double ddx = ex - ray_x;
            const double ddy = ey - ray_y;
            if ((ddx * ddx + ddy * ddy) <= 0.18) {
                --enemy.hp;
                return true;
            }
        }
    }
    return false;
}

void render_frame(
    const std::vector<std::string>& map,
    const Vec2Fixed& player_pos,
    const Vec2Fixed& player_dir,
    const Vec2Fixed& camera_plane,
    const std::vector<Enemy>& enemies,
    const std::string& mode_label,
    Viewport viewport,
    int hp,
    int ammo,
    int score,
    int wave
) {
    const int screen_w = std::max(80, viewport.width);
    const int screen_h = std::max(28, viewport.height - 2);  // 2 rows reserved for HUD text
    std::vector<std::string> frame(screen_h, std::string(screen_w, ' '));
    std::vector<double> zbuffer(static_cast<std::size_t>(screen_w), std::numeric_limits<double>::max());

    for (int y = screen_h / 2; y < screen_h; ++y) {
        for (int x = 0; x < screen_w; ++x) {
            frame[y][x] = '.';
        }
    }

    const double pos_x = player_pos.x.to_double();
    const double pos_y = player_pos.y.to_double();
    const double dir_x = player_dir.x.to_double();
    const double dir_y = player_dir.y.to_double();
    const double plane_x = camera_plane.x.to_double();
    const double plane_y = camera_plane.y.to_double();

    for (int x = 0; x < screen_w; ++x) {
        const double camera_x = 2.0 * static_cast<double>(x) / static_cast<double>(screen_w) - 1.0;
        const double ray_dir_x = dir_x + plane_x * camera_x;
        const double ray_dir_y = dir_y + plane_y * camera_x;

        int map_x = static_cast<int>(pos_x);
        int map_y = static_cast<int>(pos_y);

        const double delta_dist_x =
            (std::abs(ray_dir_x) < 1e-9) ? 1e30 : std::abs(1.0 / ray_dir_x);
        const double delta_dist_y =
            (std::abs(ray_dir_y) < 1e-9) ? 1e30 : std::abs(1.0 / ray_dir_y);

        double side_dist_x = 0.0;
        double side_dist_y = 0.0;
        int step_x = 0;
        int step_y = 0;

        if (ray_dir_x < 0) {
            step_x = -1;
            side_dist_x = (pos_x - static_cast<double>(map_x)) * delta_dist_x;
        } else {
            step_x = 1;
            side_dist_x = (static_cast<double>(map_x + 1) - pos_x) * delta_dist_x;
        }
        if (ray_dir_y < 0) {
            step_y = -1;
            side_dist_y = (pos_y - static_cast<double>(map_y)) * delta_dist_y;
        } else {
            step_y = 1;
            side_dist_y = (static_cast<double>(map_y + 1) - pos_y) * delta_dist_y;
        }

        bool hit = false;
        bool side = false;
        while (!hit) {
            if (side_dist_x < side_dist_y) {
                side_dist_x += delta_dist_x;
                map_x += step_x;
                side = false;
            } else {
                side_dist_y += delta_dist_y;
                map_y += step_y;
                side = true;
            }
            if (is_wall(map, map_x, map_y)) {
                hit = true;
            }
        }

        const double perp_wall_dist =
            side ? (side_dist_y - delta_dist_y) : (side_dist_x - delta_dist_x);
        zbuffer[static_cast<std::size_t>(x)] = perp_wall_dist;

        const int line_height = static_cast<int>(static_cast<double>(screen_h) / std::max(perp_wall_dist, 0.0001));
        int draw_start = -line_height / 2 + screen_h / 2;
        int draw_end = line_height / 2 + screen_h / 2;
        draw_start = std::max(draw_start, 0);
        draw_end = std::min(draw_end, screen_h - 1);

        const char shade = wall_shade(perp_wall_dist, side);
        for (int y = draw_start; y <= draw_end; ++y) {
            frame[y][x] = shade;
        }
    }

    struct SpriteRender {
        double depth = 0.0;
        int screen_x = 0;
        int draw_start_y = 0;
        int draw_end_y = 0;
        int draw_start_x = 0;
        int draw_end_x = 0;
        char shade = 'M';
    };
    std::vector<SpriteRender> sprites;

    for (const auto& enemy : enemies) {
        if (enemy.hp <= 0) {
            continue;
        }

        const double sx = enemy.pos.x.to_double() - pos_x;
        const double sy = enemy.pos.y.to_double() - pos_y;

        const double inv_det = 1.0 / (plane_x * dir_y - dir_x * plane_y);
        const double transform_x = inv_det * (dir_y * sx - dir_x * sy);
        const double transform_y = inv_det * (-plane_y * sx + plane_x * sy);
        if (transform_y <= 0.1) {
            continue;
        }

        const int sprite_screen_x = static_cast<int>((screen_w / 2.0) * (1.0 + transform_x / transform_y));
        const int sprite_height = std::abs(static_cast<int>(screen_h / transform_y));
        const int sprite_width = std::abs(static_cast<int>(screen_h / transform_y));

        SpriteRender render{};
        render.depth = transform_y;
        render.screen_x = sprite_screen_x;
        render.draw_start_y = std::max(-sprite_height / 2 + screen_h / 2, 0);
        render.draw_end_y = std::min(sprite_height / 2 + screen_h / 2, screen_h - 1);
        render.draw_start_x = std::max(-sprite_width / 2 + sprite_screen_x, 0);
        render.draw_end_x = std::min(sprite_width / 2 + sprite_screen_x, screen_w - 1);
        render.shade = (enemy.hp == 1) ? 'w' : 'M';
        sprites.push_back(render);
    }

    std::sort(sprites.begin(), sprites.end(), [](const SpriteRender& lhs, const SpriteRender& rhs) {
        return lhs.depth > rhs.depth;
    });

    for (const auto& sprite : sprites) {
        for (int x = sprite.draw_start_x; x <= sprite.draw_end_x; ++x) {
            if (sprite.depth >= zbuffer[static_cast<std::size_t>(x)]) {
                continue;
            }
            for (int y = sprite.draw_start_y; y <= sprite.draw_end_y; ++y) {
                frame[y][x] = sprite.shade;
            }
        }
    }

    draw_minimap(frame, map, player_pos, enemies);
    draw_weapon_overlay(frame);

    std::cout << "\x1b[H";
    std::cout << "\x1b[2J\x1b[H";
    std::cout << "BREACH TEAM // " << mode_label << " | HP:" << hp << " Ammo:" << ammo << " Score:" << score
              << " Wave:" << wave << '\n';
    std::cout << "W/S move  A/D rotate  SPACE fire  Q quit\n";
    for (const auto& line : frame) {
        std::cout << line << '\n';
    }
    std::cout.flush();
}

}  // namespace

int run_terminal_game() {
    std::string mode_label = "SOLO PLAY";
    while (true) {
        std::cout << "\n=== BREACH TEAM ===\n";
        std::cout << "1) Solo Play\n";
        std::cout << "2) Host Game\n";
        std::cout << "3) Join Game\n";
        std::cout << "Q) Quit\n";
        std::cout << "Select mode: ";

        std::string choice;
        if (!std::getline(std::cin, choice)) {
            return 0;
        }
        if (choice.empty()) {
            continue;
        }

        const char selected = choice[0];
        if (selected == '1') {
            mode_label = "SOLO PLAY";
            break;
        }
        if (selected == '2') {
            std::cout << "Host name (optional): ";
            std::string host_name;
            std::getline(std::cin, host_name);
            mode_label = host_name.empty() ? "HOST MODE" : ("HOST MODE [" + host_name + "]");
            break;
        }
        if (selected == '3') {
            std::cout << "Join code (optional): ";
            std::string join_code;
            std::getline(std::cin, join_code);
            mode_label = join_code.empty() ? "JOIN MODE" : ("JOIN MODE [" + join_code + "]");
            break;
        }
        if (selected == 'q' || selected == 'Q') {
            return 0;
        }
    }

    TerminalGuard terminal;
    if (!terminal.ok()) {
        std::cerr << "Failed to initialize terminal raw mode.\n";
        return 1;
    }

    const std::vector<std::string> map{
        "############################",
        "#..........#...............#",
        "#...####...#....#####......#",
        "#..........#...............#",
        "#..........######..........#",
        "#......................#...#",
        "#...######.............#...#",
        "#......................#...#",
        "#.........########.........#",
        "#.......................#..#",
        "#..######...............#..#",
        "#.......................#..#",
        "#.........########......#..#",
        "#.......................#..#",
        "#..####.................#..#",
        "#.......................#..#",
        "#......#########...........#",
        "#..........................#",
        "#.................####.....#",
        "############################",
    };

    Vec2Fixed player_pos{Fixed16::from_double(2.5), Fixed16::from_double(2.5)};
    Vec2Fixed player_dir{Fixed16::from_double(1.0), Fixed16::from_double(0.0)};
    Vec2Fixed camera_plane{Fixed16::from_double(0.0), Fixed16::from_double(0.66)};

    std::vector<Enemy> enemies{
        {Vec2Fixed{Fixed16::from_double(12.5), Fixed16::from_double(5.5)}, 2, 0},
        {Vec2Fixed{Fixed16::from_double(18.5), Fixed16::from_double(10.5)}, 2, 0},
        {Vec2Fixed{Fixed16::from_double(22.5), Fixed16::from_double(16.5)}, 2, 0},
    };
    const std::vector<Vec2Fixed> spawn_points{
        {Fixed16::from_double(24.5), Fixed16::from_double(2.5)},
        {Fixed16::from_double(24.5), Fixed16::from_double(17.5)},
        {Fixed16::from_double(15.5), Fixed16::from_double(16.5)},
        {Fixed16::from_double(20.5), Fixed16::from_double(5.5)},
    };

    int hp = 7;
    int ammo = 80;
    int score = 0;
    int wave = 1;
    int tick = 0;
    bool running = true;

    while (running && hp > 0) {
        bool shoot = false;
        while (can_read_input()) {
            char key = 0;
            if (read(STDIN_FILENO, &key, 1) <= 0) {
                break;
            }

            if (key == 'q' || key == 'Q') {
                running = false;
            } else if (key == 'a' || key == 'A') {
                rotate_player(player_dir, camera_plane, true);
            } else if (key == 'd' || key == 'D') {
                rotate_player(player_dir, camera_plane, false);
            } else if (key == 'w' || key == 'W') {
                const Vec2Fixed forward = player_dir * MOVE_SPEED;
                const auto nx = player_pos.x + forward.x;
                const auto ny = player_pos.y + forward.y;
                if (can_walk(map, nx, player_pos.y, enemies)) {
                    player_pos.x = nx;
                }
                if (can_walk(map, player_pos.x, ny, enemies)) {
                    player_pos.y = ny;
                }
            } else if (key == 's' || key == 'S') {
                const Vec2Fixed backward = player_dir * MOVE_SPEED;
                const auto nx = player_pos.x - backward.x;
                const auto ny = player_pos.y - backward.y;
                if (can_walk(map, nx, player_pos.y, enemies)) {
                    player_pos.x = nx;
                }
                if (can_walk(map, player_pos.x, ny, enemies)) {
                    player_pos.y = ny;
                }
            } else if (key == ' ') {
                shoot = true;
            }
        }

        if (shoot && ammo > 0) {
            --ammo;
            if (hitscan_enemy(map, player_pos, player_dir, enemies)) {
                score += 15;
            }
        }

        enemies.erase(
            std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& enemy) { return enemy.hp <= 0; }),
            enemies.end()
        );

        if (tick % 16 == 0) {
            for (std::size_t idx = 0; idx < enemies.size(); ++idx) {
                auto& enemy = enemies[idx];
                const double ex = enemy.pos.x.to_double();
                const double ey = enemy.pos.y.to_double();
                const double px = player_pos.x.to_double();
                const double py = player_pos.y.to_double();
                const double ddx = px - ex;
                const double ddy = py - ey;
                const double dist_sq = ddx * ddx + ddy * ddy;

                if (dist_sq > 0.36) {
                    const double len = std::sqrt(std::max(dist_sq, 0.0001));
                    const double step_x = ddx / len * 0.11;
                    const double step_y = ddy / len * 0.11;
                    const auto nx = Fixed16::from_double(ex + step_x);
                    const auto ny = Fixed16::from_double(ey + step_y);
                    if (can_walk_enemy(map, nx, ny, enemies, idx)) {
                        enemy.pos.x = nx;
                        enemy.pos.y = ny;
                    }
                }

                if (enemy.attack_cooldown > 0) {
                    --enemy.attack_cooldown;
                }

                const double dist = std::sqrt(dist_sq);
                if (dist < 1.2 && enemy.attack_cooldown == 0) {
                    --hp;
                    enemy.attack_cooldown = 20;
                }
            }
        }

        if (tick % 120 == 0 && enemies.size() < 8) {
            const auto& spawn = spawn_points[static_cast<std::size_t>((tick / 120) % static_cast<int>(spawn_points.size()))];
            const double ddx = spawn.x.to_double() - player_pos.x.to_double();
            const double ddy = spawn.y.to_double() - player_pos.y.to_double();
            if ((ddx * ddx + ddy * ddy) > 25.0 && can_walk(map, spawn.x, spawn.y, enemies)) {
                enemies.push_back(Enemy{spawn, 2, 0});
                if ((tick / 120) % 3 == 0) {
                    ++wave;
                }
            }
        }

        if (ammo <= 0 && enemies.empty()) {
            running = false;
        }

        const Viewport viewport = query_viewport();
        render_frame(map, player_pos, player_dir, camera_plane, enemies, mode_label, viewport, hp, ammo, score, wave);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
        ++tick;
    }

    std::cout << "\n";
    if (hp <= 0) {
        std::cout << "You were overrun. Score: " << score << "\n";
        return 2;
    }

    std::cout << "Run complete. Score: " << score << "\n";
    return 0;
}

}  // namespace breach_team::game
