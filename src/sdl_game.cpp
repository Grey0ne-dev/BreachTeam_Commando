#include "breach_team/game/sdl_game.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#ifdef BREACH_TEAM_HAS_SDL2_IMAGE
#include <SDL2/SDL_image.h>
#endif

#include "breach_team/core/math.hpp"

namespace breach_team::game {

namespace {

using core::Fixed16;
using core::Mat2Fixed;
using core::Vec2Fixed;

constexpr int MAP_W = 28;
constexpr int MAP_H = 20;
constexpr Fixed16 MOVE_SPEED = Fixed16::from_double(0.13);
constexpr Fixed16 ROT_COS = Fixed16::from_double(0.9978);
constexpr Fixed16 ROT_SIN = Fixed16::from_double(0.0663);  // ~3.8 deg

struct Enemy {
    Vec2Fixed pos;
    int hp = 2;
    int attack_cooldown = 0;
};

struct TexturePack {
    SDL_Surface* wall = nullptr;
    SDL_Surface* floor = nullptr;
    SDL_Surface* enemy = nullptr;
    SDL_Texture* gun = nullptr;
};

SDL_Texture* g_framebuffer = nullptr;
int g_framebuffer_w = 0;
int g_framebuffer_h = 0;

void destroy_framebuffer() {
    if (g_framebuffer != nullptr) {
        SDL_DestroyTexture(g_framebuffer);
        g_framebuffer = nullptr;
        g_framebuffer_w = 0;
        g_framebuffer_h = 0;
    }
}

bool ensure_framebuffer(SDL_Renderer* renderer, int w, int h) {
    if (g_framebuffer != nullptr && g_framebuffer_w == w && g_framebuffer_h == h) {
        return true;
    }
    destroy_framebuffer();
    g_framebuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (g_framebuffer == nullptr) {
        return false;
    }
    g_framebuffer_w = w;
    g_framebuffer_h = h;
    return true;
}

void destroy_textures(TexturePack& textures) {
    if (textures.wall != nullptr) {
        SDL_FreeSurface(textures.wall);
        textures.wall = nullptr;
    }
    if (textures.floor != nullptr) {
        SDL_FreeSurface(textures.floor);
        textures.floor = nullptr;
    }
    if (textures.enemy != nullptr) {
        SDL_FreeSurface(textures.enemy);
        textures.enemy = nullptr;
    }
    if (textures.gun != nullptr) {
        SDL_DestroyTexture(textures.gun);
        textures.gun = nullptr;
    }
}

bool file_exists(const std::string& path) {
    return std::filesystem::exists(path);
}

SDL_Surface* load_surface_any(const std::vector<std::string>& paths) {
#ifdef BREACH_TEAM_HAS_SDL2_IMAGE
    for (const auto& path : paths) {
        if (!file_exists(path)) {
            continue;
        }
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (surface != nullptr) {
            SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
            SDL_FreeSurface(surface);
            if (converted != nullptr) {
                return converted;
            }
        }
    }
#else
    (void)paths;
#endif
    return nullptr;
}

SDL_Texture* load_texture_any(SDL_Renderer* renderer, const std::vector<std::string>& paths) {
    SDL_Surface* surface = load_surface_any(paths);
    if (surface == nullptr) {
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

std::uint32_t pixel_at(const SDL_Surface* surface, int x, int y) {
    const int cx = std::clamp(x, 0, surface->w - 1);
    const int cy = std::clamp(y, 0, surface->h - 1);
    const auto* row = static_cast<const std::uint8_t*>(surface->pixels) + cy * surface->pitch;
    const auto* pixel = reinterpret_cast<const std::uint32_t*>(row) + cx;
    return *pixel;
}

std::uint32_t sample_surface(const SDL_Surface* surface, double u, double v) {
    const int tx = static_cast<int>(u * static_cast<double>(surface->w - 1));
    const int ty = static_cast<int>(v * static_cast<double>(surface->h - 1));
    return pixel_at(surface, tx, ty);
}

std::uint32_t dim_color(std::uint32_t argb, int num, int den) {
    const std::uint32_t a = (argb >> 24) & 0xFFU;
    const std::uint32_t r = ((argb >> 16) & 0xFFU) * static_cast<std::uint32_t>(num) / static_cast<std::uint32_t>(den);
    const std::uint32_t g = ((argb >> 8) & 0xFFU) * static_cast<std::uint32_t>(num) / static_cast<std::uint32_t>(den);
    const std::uint32_t b = (argb & 0xFFU) * static_cast<std::uint32_t>(num) / static_cast<std::uint32_t>(den);
    return (a << 24) | (r << 16) | (g << 8) | b;
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

    for (int step = 0; step < 120; ++step) {
        ray_x += dx * 0.1;
        ray_y += dy * 0.1;
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
            if ((ddx * ddx + ddy * ddy) <= 0.22) {
                --enemy.hp;
                return true;
            }
        }
    }
    return false;
}

void draw_weapon(SDL_Renderer* renderer, int w, int h, const TexturePack& textures, int fire_anim_ticks) {
    const int kick = (fire_anim_ticks > 0) ? std::min(14, fire_anim_ticks * 2) : 0;

    if (textures.gun != nullptr) {
        const int gun_h = std::max(100, h / 4);
        const int gun_w = gun_h * 2;
        SDL_Rect dst{w / 2 - gun_w / 2, h - gun_h - 8 + kick, gun_w, gun_h};
        SDL_RenderCopy(renderer, textures.gun, nullptr, &dst);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 220);
    SDL_Rect body{w / 2 - 90, h - 90 + kick, 180, 60};
    SDL_RenderFillRect(renderer, &body);

    SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
    SDL_Rect barrel{w / 2 - 14, h - 104 + kick, 28, 28};
    SDL_RenderFillRect(renderer, &barrel);

    if (fire_anim_ticks > 0) {
        SDL_SetRenderDrawColor(renderer, 255, 210, 110, 220);
        SDL_Rect flash1{w / 2 - 6, h - 130 + kick, 12, 22};
        SDL_Rect flash2{w / 2 - 16, h - 118 + kick, 32, 10};
        SDL_RenderFillRect(renderer, &flash1);
        SDL_RenderFillRect(renderer, &flash2);
    }

    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
    SDL_RenderDrawLine(renderer, w / 2 - 8, h / 2, w / 2 + 8, h / 2);
    SDL_RenderDrawLine(renderer, w / 2, h / 2 - 8, w / 2, h / 2 + 8);
}

void draw_minimap(
    SDL_Renderer* renderer,
    const std::vector<std::string>& map,
    const Vec2Fixed& player,
    const std::vector<Enemy>& enemies
) {
    constexpr int cell = 8;
    constexpr int ox = 8;
    constexpr int oy = 8;

    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            if (map[y][x] == '#') {
                SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 20, 20, 20, 220);
            }
            SDL_Rect r{ox + x * cell, oy + y * cell, cell - 1, cell - 1};
            SDL_RenderFillRect(renderer, &r);
        }
    }

    SDL_SetRenderDrawColor(renderer, 0, 220, 0, 255);
    SDL_Rect pr{
        ox + static_cast<int>(player.x.to_double() * cell) - 2,
        oy + static_cast<int>(player.y.to_double() * cell) - 2,
        4,
        4,
    };
    SDL_RenderFillRect(renderer, &pr);

    SDL_SetRenderDrawColor(renderer, 220, 40, 40, 255);
    for (const auto& enemy : enemies) {
        if (enemy.hp <= 0) {
            continue;
        }
        SDL_Rect er{
            ox + static_cast<int>(enemy.pos.x.to_double() * cell) - 2,
            oy + static_cast<int>(enemy.pos.y.to_double() * cell) - 2,
            4,
            4,
        };
        SDL_RenderFillRect(renderer, &er);
    }
}

void render_world(
    SDL_Renderer* renderer,
    int w,
    int h,
    const std::vector<std::string>& map,
    const Vec2Fixed& player_pos,
    const Vec2Fixed& player_dir,
    const Vec2Fixed& camera_plane,
    const std::vector<Enemy>& enemies,
    const TexturePack& textures
) {
    if (!ensure_framebuffer(renderer, w, h)) {
        return;
    }

    void* raw_pixels = nullptr;
    int pitch_bytes = 0;
    if (SDL_LockTexture(g_framebuffer, nullptr, &raw_pixels, &pitch_bytes) != 0) {
        return;
    }

    auto* pixels = static_cast<std::uint32_t*>(raw_pixels);
    const int pitch = pitch_bytes / 4;
    const std::uint32_t sky_color = 0xFF181C2EU;
    const std::uint32_t floor_color = 0xFF1E1612U;

    for (int y = 0; y < h; ++y) {
        std::uint32_t* row = pixels + y * pitch;
        if (y < h / 2) {
            std::fill(row, row + w, sky_color);
            continue;
        }

        if (textures.floor == nullptr) {
            std::fill(row, row + w, floor_color);
            continue;
        }

        const int fy = ((y - h / 2) * textures.floor->h * 6 / std::max(1, h / 2)) % textures.floor->h;
        for (int x = 0; x < w; ++x) {
            const int fx = (x * textures.floor->w * 4 / std::max(1, w)) % textures.floor->w;
            row[x] = pixel_at(textures.floor, fx, fy);
        }
    }

    std::vector<double> zbuffer(static_cast<std::size_t>(w), 1e9);
    const double pos_x = player_pos.x.to_double();
    const double pos_y = player_pos.y.to_double();
    const double dir_x = player_dir.x.to_double();
    const double dir_y = player_dir.y.to_double();
    const double plane_x = camera_plane.x.to_double();
    const double plane_y = camera_plane.y.to_double();

    for (int x = 0; x < w; ++x) {
        const double camera_x = 2.0 * static_cast<double>(x) / static_cast<double>(w) - 1.0;
        const double ray_dir_x = dir_x + plane_x * camera_x;
        const double ray_dir_y = dir_y + plane_y * camera_x;

        int map_x = static_cast<int>(pos_x);
        int map_y = static_cast<int>(pos_y);
        const double delta_dist_x = (std::abs(ray_dir_x) < 1e-9) ? 1e30 : std::abs(1.0 / ray_dir_x);
        const double delta_dist_y = (std::abs(ray_dir_y) < 1e-9) ? 1e30 : std::abs(1.0 / ray_dir_y);

        double side_dist_x;
        double side_dist_y;
        int step_x;
        int step_y;

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

        const double perp_wall_dist = side ? (side_dist_y - delta_dist_y) : (side_dist_x - delta_dist_x);
        zbuffer[static_cast<std::size_t>(x)] = perp_wall_dist;
        const int line_h = static_cast<int>(static_cast<double>(h) / std::max(perp_wall_dist, 0.0001));
        int start = std::max(0, -line_h / 2 + h / 2);
        int end = std::min(h - 1, line_h / 2 + h / 2);

        if (textures.wall != nullptr) {
            const double wall_x = side ? (pos_x + perp_wall_dist * ray_dir_x) : (pos_y + perp_wall_dist * ray_dir_y);
            double wall_u = wall_x - std::floor(wall_x);
            if ((!side && ray_dir_x > 0) || (side && ray_dir_y < 0)) {
                wall_u = 1.0 - wall_u;
            }
            for (int y = start; y <= end; ++y) {
                const double v = static_cast<double>(y - start) / static_cast<double>(std::max(1, end - start));
                std::uint32_t c = sample_surface(textures.wall, wall_u, v);
                if (side) {
                    c = dim_color(c, 3, 4);
                }
                pixels[y * pitch + x] = c;
            }
        } else {
            Uint8 c = 0;
            if (perp_wall_dist < 1.5) {
                c = side ? 220 : 255;
            } else if (perp_wall_dist < 3.5) {
                c = side ? 150 : 190;
            } else {
                c = side ? 90 : 130;
            }
            const std::uint32_t col = 0xFF000000U | (static_cast<std::uint32_t>(c) << 16) |
                                      (static_cast<std::uint32_t>(c) << 8) | static_cast<std::uint32_t>(c);
            for (int y = start; y <= end; ++y) {
                pixels[y * pitch + x] = col;
            }
        }
    }

    struct Sprite {
        double depth;
        SDL_Rect rect;
        Uint8 r;
        Uint8 g;
        Uint8 b;
    };
    std::vector<Sprite> sprites;
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

        const int screen_x = static_cast<int>((w / 2.0) * (1.0 + transform_x / transform_y));
        const int sprite_h = std::abs(static_cast<int>(h / transform_y));
        const int sprite_w = std::max(2, sprite_h / 2);
        SDL_Rect rect{
            screen_x - sprite_w / 2,
            h / 2 - sprite_h / 2,
            sprite_w,
            sprite_h,
        };
        sprites.push_back(Sprite{
            transform_y,
            rect,
            static_cast<Uint8>(enemy.hp == 1 ? 240 : 210),
            static_cast<Uint8>(enemy.hp == 1 ? 120 : 40),
            static_cast<Uint8>(enemy.hp == 1 ? 120 : 40),
        });
    }

    std::sort(sprites.begin(), sprites.end(), [](const Sprite& a, const Sprite& b) { return a.depth > b.depth; });
    for (const auto& s : sprites) {
        const int center_x = s.rect.x + s.rect.w / 2;
        if (center_x >= 0 && center_x < w && s.depth < zbuffer[static_cast<std::size_t>(center_x)]) {
            if (textures.enemy != nullptr) {
                for (int y = s.rect.y; y < s.rect.y + s.rect.h; ++y) {
                    if (y < 0 || y >= h) {
                        continue;
                    }
                    const int sy = (y - s.rect.y) * (textures.enemy->h - 1) / std::max(1, s.rect.h - 1);
                    for (int x = s.rect.x; x < s.rect.x + s.rect.w; ++x) {
                        if (x < 0 || x >= w || s.depth >= zbuffer[static_cast<std::size_t>(x)]) {
                            continue;
                        }
                        const int sx = (x - s.rect.x) * (textures.enemy->w - 1) / std::max(1, s.rect.w - 1);
                        const std::uint32_t c = pixel_at(textures.enemy, sx, sy);
                        if (((c >> 24) & 0xFFU) > 16U) {
                            pixels[y * pitch + x] = c;
                        }
                    }
                }
            } else {
                const int x0 = std::max(0, s.rect.x);
                const int y0 = std::max(0, s.rect.y);
                const int x1 = std::min(w, s.rect.x + s.rect.w);
                const int y1 = std::min(h, s.rect.y + s.rect.h);
                const std::uint32_t col =
                    0xFF000000U | (static_cast<std::uint32_t>(s.r) << 16) | (static_cast<std::uint32_t>(s.g) << 8) |
                    static_cast<std::uint32_t>(s.b);
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        if (s.depth < zbuffer[static_cast<std::size_t>(x)]) {
                            pixels[y * pitch + x] = col;
                        }
                    }
                }
            }
        }
    }

    SDL_UnlockTexture(g_framebuffer);
    SDL_RenderCopy(renderer, g_framebuffer, nullptr, nullptr);
}

}  // namespace

int run_sdl_game() {
    std::string mode_label = "SOLO PLAY";
    while (true) {
        std::cout << "\n=== BREACH TEAM SDL2 ===\n";
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        return 1;
    }

    const std::string base_title = "BREACH TEAM SDL2 // " + mode_label;
    SDL_Window* window = SDL_CreateWindow(
        base_title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

#ifdef BREACH_TEAM_HAS_SDL2_IMAGE
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#endif

    TexturePack textures{};
    textures.wall = load_surface_any({
        "src/assets/textures/firewala.png",
        "assets/textures/firewala.png",
    });
    textures.floor = load_surface_any({
        "src/assets/textures/flat-sflr7_4.png",
        "assets/textures/flat-sflr7_4.png",
        "src/assets/textures/flat-flat23.png",
        "assets/textures/flat-flat23.png",
    });
    textures.enemy = load_surface_any({
        "src/assets/textures/HEADE1.png",
        "assets/textures/HEADE1.png",
        "src/assets/textures/blodrip2.png",
        "assets/textures/blodrip2.png",
    });
    textures.gun = load_texture_any(renderer, {
        "src/assets/textures/RIFJB0.png",
        "assets/textures/RIFJB0.png",
    });

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
    int ammo = 100;
    int score = 0;
    int wave = 1;
    int tick = 0;
    bool running = true;
    bool prev_space = false;
    int fire_anim_ticks = 0;

    auto last_title = std::chrono::steady_clock::now();
    while (running && hp > 0) {
        SDL_Event e{};
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
        }

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_Q]) {
            running = false;
        }
        if (keys[SDL_SCANCODE_A]) {
            rotate_player(player_dir, camera_plane, true);
        }
        if (keys[SDL_SCANCODE_D]) {
            rotate_player(player_dir, camera_plane, false);
        }
        if (keys[SDL_SCANCODE_W]) {
            const Vec2Fixed f = player_dir * MOVE_SPEED;
            const auto nx = player_pos.x + f.x;
            const auto ny = player_pos.y + f.y;
            if (can_walk(map, nx, player_pos.y, enemies)) {
                player_pos.x = nx;
            }
            if (can_walk(map, player_pos.x, ny, enemies)) {
                player_pos.y = ny;
            }
        }
        if (keys[SDL_SCANCODE_S]) {
            const Vec2Fixed b = player_dir * MOVE_SPEED;
            const auto nx = player_pos.x - b.x;
            const auto ny = player_pos.y - b.y;
            if (can_walk(map, nx, player_pos.y, enemies)) {
                player_pos.x = nx;
            }
            if (can_walk(map, player_pos.x, ny, enemies)) {
                player_pos.y = ny;
            }
        }

        const bool now_space = keys[SDL_SCANCODE_SPACE] != 0;
        if (now_space && !prev_space && ammo > 0) {
            --ammo;
            fire_anim_ticks = 6;
            if (hitscan_enemy(map, player_pos, player_dir, enemies)) {
                score += 15;
            }
        }
        prev_space = now_space;

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
                    const double step_x = ddx / len * 0.1;
                    const double step_y = ddy / len * 0.1;
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
                if (std::sqrt(dist_sq) < 1.2 && enemy.attack_cooldown == 0) {
                    --hp;
                    enemy.attack_cooldown = 25;
                }
            }
        }

        if (tick % 140 == 0 && enemies.size() < 8) {
            const auto& spawn = spawn_points[static_cast<std::size_t>((tick / 140) % static_cast<int>(spawn_points.size()))];
            const double ddx = spawn.x.to_double() - player_pos.x.to_double();
            const double ddy = spawn.y.to_double() - player_pos.y.to_double();
            if ((ddx * ddx + ddy * ddy) > 25.0 && can_walk(map, spawn.x, spawn.y, enemies)) {
                enemies.push_back(Enemy{spawn, 2, 0});
                if ((tick / 140) % 3 == 0) {
                    ++wave;
                }
            }
        }

        int w = 0;
        int h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        render_world(renderer, w, h, map, player_pos, player_dir, camera_plane, enemies, textures);
        draw_minimap(renderer, map, player_pos, enemies);
        draw_weapon(renderer, w, h, textures, fire_anim_ticks);
        SDL_RenderPresent(renderer);
        if (fire_anim_ticks > 0) {
            --fire_anim_ticks;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_title > std::chrono::milliseconds(200)) {
            last_title = now;
            const std::string title = base_title + " | HP:" + std::to_string(hp) + " Ammo:" + std::to_string(ammo) +
                                      " Score:" + std::to_string(score) + " Wave:" + std::to_string(wave);
            SDL_SetWindowTitle(window, title.c_str());
        }

        if (ammo <= 0 && enemies.empty()) {
            running = false;
        }
        ++tick;
    }

    destroy_textures(textures);
    destroy_framebuffer();
#ifdef BREACH_TEAM_HAS_SDL2_IMAGE
    IMG_Quit();
#endif
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return (hp > 0) ? 0 : 2;
}

}  // namespace breach_team::game
