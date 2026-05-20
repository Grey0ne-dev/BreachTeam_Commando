#include "breach_team/game/sdl_game.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <unistd.h>

#include <SDL2/SDL.h>
#ifdef BREACH_TEAM_HAS_SDL2_IMAGE
#include <SDL2/SDL_image.h>
#endif
#ifdef BREACH_TEAM_HAS_SDL2_MIXER
#include <SDL2/SDL_mixer.h>
#endif

#include "breach_team/core/math.hpp"
#include "breach_team/net/enet_transport.hpp"
#include "breach_team/net/protocol.hpp"

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

struct RemotePlayer {
    Vec2Fixed pos;
    Vec2Fixed dir;
    std::uint64_t last_tick = 0;
    std::uint64_t last_seen_tick = 0;
};

enum class SessionMode {
    solo,
    host,
    join,
};

struct TexturePack {
    SDL_Surface* wall = nullptr;
    SDL_Surface* floor = nullptr;
    SDL_Surface* enemy = nullptr;
    SDL_Surface* player = nullptr;
    std::vector<SDL_Texture*> weapon_frames;
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
    if (textures.player != nullptr) {
        SDL_FreeSurface(textures.player);
        textures.player = nullptr;
    }
    for (SDL_Texture*& frame : textures.weapon_frames) {
        if (frame != nullptr) {
            SDL_DestroyTexture(frame);
            frame = nullptr;
        }
    }
    textures.weapon_frames.clear();
}

std::vector<std::filesystem::path> asset_roots() {
    std::vector<std::filesystem::path> roots;

    if (const char* env_root = std::getenv("BREACH_TEAM_ASSET_ROOT"); env_root != nullptr && *env_root != '\0') {
        roots.emplace_back(env_root);
    }

#ifdef BREACH_TEAM_SOURCE_DIR
    roots.emplace_back(std::filesystem::path(BREACH_TEAM_SOURCE_DIR) / "src" / "assets");
    roots.emplace_back(std::filesystem::path(BREACH_TEAM_SOURCE_DIR) / "assets");
#endif

    std::array<char, 4096> exe_path{};
    const ssize_t exe_len = readlink("/proc/self/exe", exe_path.data(), exe_path.size() - 1);
    if (exe_len > 0) {
        exe_path[static_cast<std::size_t>(exe_len)] = '\0';
        const std::filesystem::path exe_dir = std::filesystem::path(exe_path.data()).parent_path();
        roots.push_back(exe_dir / "assets");
        roots.push_back(exe_dir / ".." / "assets");
        roots.push_back(exe_dir / ".." / ".." / "assets");
    }

    roots.push_back(std::filesystem::current_path() / "assets");
    roots.push_back(std::filesystem::current_path() / "src" / "assets");
    return roots;
}

std::optional<std::filesystem::path> resolve_asset_path(std::string_view relative_path) {
    if (relative_path.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path input_path(relative_path);
    if (input_path.is_absolute() && std::filesystem::exists(input_path)) {
        return input_path;
    }
    if (std::filesystem::exists(input_path)) {
        return input_path;
    }

    for (const auto& root : asset_roots()) {
        const auto candidate = root / input_path;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

SDL_Surface* load_surface_any(const std::vector<std::string>& paths) {
#ifdef BREACH_TEAM_HAS_SDL2_IMAGE
    for (const auto& path : paths) {
        const auto resolved = resolve_asset_path(path);
        if (!resolved.has_value()) {
            continue;
        }
        SDL_Surface* surface = IMG_Load(resolved->string().c_str());
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
    if (texture != nullptr) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(surface);
    return texture;
}

std::vector<SDL_Texture*> load_texture_sequence(
    SDL_Renderer* renderer,
    const std::vector<std::vector<std::string>>& sequence_paths
) {
    std::vector<SDL_Texture*> frames;
    frames.reserve(sequence_paths.size());
    for (const auto& frame_paths : sequence_paths) {
        frames.push_back(load_texture_any(renderer, frame_paths));
    }
    return frames;
}

#ifdef BREACH_TEAM_HAS_SDL2_MIXER
Mix_Chunk* load_chunk_any(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        const auto resolved = resolve_asset_path(path);
        if (!resolved.has_value()) {
            continue;
        }
        Mix_Chunk* chunk = Mix_LoadWAV(resolved->string().c_str());
        if (chunk != nullptr) {
            return chunk;
        }
    }
    return nullptr;
}
#endif

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

void rotate_direction(Vec2Fixed& dir, bool left) {
    dir = rotation_matrix(left) * dir;
}

void apply_buttons(
    const std::vector<std::string>& map,
    std::uint16_t buttons,
    const std::vector<Enemy>& enemies,
    Vec2Fixed& pos,
    Vec2Fixed& dir,
    Vec2Fixed* camera_plane
) {
    if ((buttons & net::BUTTON_TURN_LEFT) != 0) {
        if (camera_plane != nullptr) {
            rotate_player(dir, *camera_plane, true);
        } else {
            rotate_direction(dir, true);
        }
    }
    if ((buttons & net::BUTTON_TURN_RIGHT) != 0) {
        if (camera_plane != nullptr) {
            rotate_player(dir, *camera_plane, false);
        } else {
            rotate_direction(dir, false);
        }
    }

    if ((buttons & net::BUTTON_MOVE_FORWARD) != 0) {
        const Vec2Fixed f = dir * MOVE_SPEED;
        const auto nx = pos.x + f.x;
        const auto ny = pos.y + f.y;
        if (can_walk(map, nx, pos.y, enemies)) {
            pos.x = nx;
        }
        if (can_walk(map, pos.x, ny, enemies)) {
            pos.y = ny;
        }
    }
    if ((buttons & net::BUTTON_MOVE_BACKWARD) != 0) {
        const Vec2Fixed b = dir * MOVE_SPEED;
        const auto nx = pos.x - b.x;
        const auto ny = pos.y - b.y;
        if (can_walk(map, nx, pos.y, enemies)) {
            pos.x = nx;
        }
        if (can_walk(map, pos.x, ny, enemies)) {
            pos.y = ny;
        }
    }
}

Vec2Fixed spawn_for_peer(std::string_view peer_id) {
    const std::size_t h = std::hash<std::string_view>{}(peer_id);
    const int sx = 3 + static_cast<int>(h % 20U);
    const int sy = 3 + static_cast<int>((h / 23U) % 14U);
    return {Fixed16::from_int(sx), Fixed16::from_int(sy)};
}

std::string endpoint_from_peer_id(std::string_view peer_id) {
    const std::size_t at = peer_id.rfind('@');
    if (at == std::string_view::npos || at + 1 >= peer_id.size()) {
        return {};
    }
    return std::string(peer_id.substr(at + 1));
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

void draw_weapon(
    SDL_Renderer* renderer,
    int w,
    int h,
    const TexturePack& textures,
    std::size_t weapon_frame_index,
    bool show_muzzle_flash,
    double bob_phase
) {
    const int bob = static_cast<int>(std::sin(bob_phase) * 4.0);
    const int kick = show_muzzle_flash ? 14 : 0;

    SDL_Texture* weapon_texture = nullptr;
    if (!textures.weapon_frames.empty()) {
        const std::size_t clamped = std::min(weapon_frame_index, textures.weapon_frames.size() - 1);
        weapon_texture = textures.weapon_frames[clamped];
    }

    if (weapon_texture != nullptr) {
        const int gun_h = std::max(100, h / 4);
        const int gun_w = gun_h * 2;
        SDL_Rect dst{w / 2 - gun_w / 2, h - gun_h - 10 + kick + bob, gun_w, gun_h};
        SDL_RenderCopy(renderer, weapon_texture, nullptr, &dst);
    } else {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 220);
        SDL_Rect body{w / 2 - 90, h - 90 + kick + bob, 180, 60};
        SDL_RenderFillRect(renderer, &body);

        SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
        SDL_Rect barrel{w / 2 - 14, h - 104 + kick + bob, 28, 28};
        SDL_RenderFillRect(renderer, &barrel);
    }

    if (show_muzzle_flash) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 210, 110, 220);
        SDL_Rect flash1{w / 2 - 6, h - 140 + kick + bob, 12, 28};
        SDL_Rect flash2{w / 2 - 20, h - 126 + kick + bob, 40, 12};
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
    const std::vector<Enemy>& enemies,
    const std::vector<Vec2Fixed>& remote_players
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

    SDL_SetRenderDrawColor(renderer, 40, 150, 255, 255);
    for (const auto& remote : remote_players) {
        SDL_Rect rr{
            ox + static_cast<int>(remote.x.to_double() * cell) - 2,
            oy + static_cast<int>(remote.y.to_double() * cell) - 2,
            4,
            4,
        };
        SDL_RenderFillRect(renderer, &rr);
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
    const std::vector<Vec2Fixed>& remote_players,
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
        int texture_kind = 0;  // 0 = flat color, 1 = enemy texture, 2 = player texture
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
            1,
        });
    }

    for (const auto& remote : remote_players) {
        const double sx = remote.x.to_double() - pos_x;
        const double sy = remote.y.to_double() - pos_y;

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
            40,
            150,
            255,
            2,
        });
    }

    std::sort(sprites.begin(), sprites.end(), [](const Sprite& a, const Sprite& b) { return a.depth > b.depth; });
    for (const auto& s : sprites) {
        const int center_x = s.rect.x + s.rect.w / 2;
        if (center_x >= 0 && center_x < w && s.depth < zbuffer[static_cast<std::size_t>(center_x)]) {
            if (textures.enemy != nullptr && s.texture_kind == 1) {
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
            } else if (textures.player != nullptr && s.texture_kind == 2) {
                for (int y = s.rect.y; y < s.rect.y + s.rect.h; ++y) {
                    if (y < 0 || y >= h) {
                        continue;
                    }
                    const int sy = (y - s.rect.y) * (textures.player->h - 1) / std::max(1, s.rect.h - 1);
                    for (int x = s.rect.x; x < s.rect.x + s.rect.w; ++x) {
                        if (x < 0 || x >= w || s.depth >= zbuffer[static_cast<std::size_t>(x)]) {
                            continue;
                        }
                        const int sx = (x - s.rect.x) * (textures.player->w - 1) / std::max(1, s.rect.w - 1);
                        std::uint32_t c = pixel_at(textures.player, sx, sy);
                        if (((c >> 24) & 0xFFU) > 16U) {
                            c = dim_color(c, 3, 4);
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
    SessionMode session_mode = SessionMode::solo;
    std::string self_endpoint = "127.0.0.1:30000";
    std::string remote_endpoint;
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
            session_mode = SessionMode::solo;
            break;
        }
        if (selected == '2') {
            std::cout << "Host listen endpoint [127.0.0.1:30000]: ";
            std::string endpoint;
            std::getline(std::cin, endpoint);
            if (!endpoint.empty()) {
                self_endpoint = endpoint;
            }
            mode_label = "HOST MODE [" + self_endpoint + "]";
            session_mode = SessionMode::host;
            break;
        }
        if (selected == '3') {
            std::cout << "Host endpoint (ip:port): ";
            std::getline(std::cin, remote_endpoint);
            if (remote_endpoint.empty()) {
                continue;
            }
            std::cout << "Local listen endpoint [127.0.0.1:30001]: ";
            std::string local_endpoint;
            std::getline(std::cin, local_endpoint);
            self_endpoint = local_endpoint.empty() ? "127.0.0.1:30001" : local_endpoint;
            mode_label = "JOIN MODE [" + remote_endpoint + "]";
            session_mode = SessionMode::join;
            break;
        }
        if (selected == 'q' || selected == 'Q') {
            return 0;
        }
    }

    std::string base_title = "BREACH TEAM SDL2 // " + mode_label;
    const bool multiplayer = session_mode != SessionMode::solo;
    const std::string self_peer_id =
        multiplayer ? ("peer-" + std::to_string(static_cast<unsigned long long>(std::hash<std::string>{}(self_endpoint))) +
                       "@" + self_endpoint)
                    : std::string{};
    net::EnetTransport transport;
    std::unordered_set<std::string> peer_routes;
    std::unordered_map<std::string, std::string> route_by_peer;
    std::unordered_map<std::string, RemotePlayer> remote_players;
    const bool network_online = multiplayer && transport.start(self_peer_id);
    if (multiplayer && !network_online) {
        mode_label += " [NET-OFFLINE]";
        std::cout << "Network offline: ENet is unavailable or bind failed for " << self_endpoint << "\n";
    } else if (network_online) {
        if (!remote_endpoint.empty()) {
            peer_routes.insert(remote_endpoint);
        }
        mode_label += " [P2P]";
        std::cout << "Network online: listening as " << self_peer_id << "\n";
        if (!remote_endpoint.empty()) {
            std::cout << "Connecting to " << remote_endpoint << "\n";
        }
    }
    base_title = "BREACH TEAM SDL2 // " + mode_label;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

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
        "textures/ashwall2.png",
        "textures/stone2.png",
        "textures/firewall.png",
        "sprites/TBLUA0.png",
        "sprites/TGRNA0.png",
        "textures/firewala.png",
    });
    textures.floor = load_surface_any({
        "textures/floor0_1.png",
        "textures/floor4_8.png",
        "textures/flat5_4.png",
        "textures/sflr7_4.png",
        "sprites/TGRNA0.png",
        "sprites/TBLUA0.png",
    });

    //textures/sp_dude6.png
    //testures/sp_dude7.png
    //textures/sp_dude8.png
    //sprites/CEYEA0.png        
    //
    //
    textures.enemy = load_surface_any({
        "textures/HEADE1.png"
    });
    textures.player = load_surface_any({
        "sprites/CEYEB0.png",
        "sprites/CEYEC0.png",
        "sprites/CEYEA0.png",
        "sprites/tredb0.png",
    });
    textures.weapon_frames = load_texture_sequence(renderer, {
        {"Sprites/craniumshaker/CSHKA0.png"},
        {"Sprites/craniumshaker/CSHKB0.png"},
        {"Sprites/craniumshaker/CSHKC0.png"},
        {"Sprites/craniumshaker/CSHKD0.png"},
        {"Sprites/craniumshaker/CSHKE0.png"},
        {"Sprites/craniumshaker/CSHKF0.png"},
        {"Sprites/craniumshaker/CSHKG0.png"},
        {"Sprites/craniumshaker/CSHKH0.png"},
        {"Sprites/craniumshaker/CSHKI0.png"},
        {"Sprites/craniumshaker/CSHKJ0.png"},
        {"Sprites/craniumshaker/CSHKK0.png"},
        {"Sprites/craniumshaker/CSHKL0.png"},
        {"Sprites/craniumshaker/CSHKM0.png"},
        {"Sprites/craniumshaker/CSHKN0.png"},
        {"Sprites/craniumshaker/CSHKO0.png"},
        {"Sprites/craniumshaker/CSHKP0.png"},
        {"Sprites/craniumshaker/CSHKQ0.png"},
        {"Sprites/craniumshaker/CSHKR0.png"},
        {"Sprites/craniumshaker/CSHKS0.png"},
        {"Sprites/craniumshaker/CSHKT0.png"},
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
    bool weapon_anim_active = false;
    enum class WeaponSfx {
        none,
        fire,
        cock,
        heavy_fire,
    };
    struct WeaponKeyframe {
        std::size_t frame;
        int duration_ms;
        bool muzzle_flash;
        WeaponSfx sfx;
    };
    constexpr std::array<WeaponKeyframe, 20> WEAPON_ANIM = {{
        {1, 35, true, WeaponSfx::fire},
        {2, 35, true, WeaponSfx::heavy_fire},
        {3, 40, false, WeaponSfx::none},
        {4, 45, false, WeaponSfx::none},
        {5, 45, false, WeaponSfx::none},
        {6, 45, false, WeaponSfx::cock},
        {7, 45, false, WeaponSfx::none},
        {8, 45, false, WeaponSfx::none},
        {9, 45, false, WeaponSfx::none},
        {10, 45, false, WeaponSfx::none},
        {11, 45, false, WeaponSfx::none},
        {12, 45, false, WeaponSfx::none},
        {13, 35, false, WeaponSfx::cock},
        {14, 45, false, WeaponSfx::none},
        {15, 45, false, WeaponSfx::none},
        {16, 45, false, WeaponSfx::none},
        {17, 45, false, WeaponSfx::none},
        {18, 45, false, WeaponSfx::none},
        {19, 45, false, WeaponSfx::none},
        {0, 55, false, WeaponSfx::none},
    }};
    std::size_t weapon_anim_key_index = 0;
    int weapon_anim_elapsed_ms = 0;
    std::size_t weapon_frame = 0;
    bool show_muzzle_flash = false;
#ifdef BREACH_TEAM_HAS_SDL2_MIXER
    bool mixer_ready = false;
    Mix_Chunk* weapon_fire = nullptr;
    Mix_Chunk* weapon_cock = nullptr;
    Mix_Chunk* weapon_heavy_fire = nullptr;
    int mixer_flags = 0;
#ifdef MIX_INIT_OGG
    mixer_flags |= MIX_INIT_OGG;
#endif
    if ((mixer_flags == 0 || (Mix_Init(mixer_flags) & mixer_flags) == mixer_flags) &&
        Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
        mixer_ready = true;
        Mix_AllocateChannels(8);
        weapon_fire = load_chunk_any({"Sounds/craniumshaker/CRFIRE.ogg", "sounds/DoublePumpGun/DBPGFIRE.ogg"});
        weapon_cock = load_chunk_any({"Sounds/craniumshaker/CRCOCK.ogg", "sounds/DoublePumpGun/DBPGOPEN.ogg"});
        weapon_heavy_fire = load_chunk_any({"Sounds/craniumshaker/DRFIRE.ogg", "sounds/DoublePumpGun/DBPGLOAD.ogg"});
        for (Mix_Chunk* chunk : {weapon_fire, weapon_cock, weapon_heavy_fire}) {
            if (chunk != nullptr) {
                Mix_VolumeChunk(chunk, (MIX_MAX_VOLUME * 3) / 4);
            }
        }
    }
    if (!mixer_ready) {
        mode_label += " [AUDIO-OFF]";
    } else if (weapon_fire == nullptr && weapon_cock == nullptr && weapon_heavy_fire == nullptr) {
        mode_label += " [SFX-MISSING]";
    }
#else
    mode_label += " [AUDIO-OFF]";
#endif

    auto play_weapon_sfx = [&](WeaponSfx sfx) {
#ifdef BREACH_TEAM_HAS_SDL2_MIXER
        if (!mixer_ready) {
            return;
        }
        Mix_Chunk* chunk = nullptr;
        switch (sfx) {
            case WeaponSfx::fire:
                chunk = weapon_fire;
                break;
            case WeaponSfx::cock:
                chunk = weapon_cock;
                break;
            case WeaponSfx::heavy_fire:
                chunk = weapon_heavy_fire;
                break;
            case WeaponSfx::none:
                break;
        }
        if (chunk != nullptr) {
            Mix_PlayChannel(-1, chunk, 0);
        }
#else
        (void)sfx;
#endif
    };

    base_title = "BREACH TEAM SDL2 // " + mode_label;
    SDL_SetWindowTitle(window, base_title.c_str());

    auto last_title = std::chrono::steady_clock::now();
    auto last_frame_time = std::chrono::steady_clock::now();
    while (running && hp > 0) {
        const auto frame_now = std::chrono::steady_clock::now();
        int frame_dt_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(frame_now - last_frame_time).count());
        if (frame_dt_ms < 0) {
            frame_dt_ms = 0;
        } else if (frame_dt_ms > 100) {
            frame_dt_ms = 100;
        }
        last_frame_time = frame_now;

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
        std::uint16_t local_buttons = 0;
        if (keys[SDL_SCANCODE_A]) {
            local_buttons = static_cast<std::uint16_t>(local_buttons | net::BUTTON_TURN_LEFT);
        }
        if (keys[SDL_SCANCODE_D]) {
            local_buttons = static_cast<std::uint16_t>(local_buttons | net::BUTTON_TURN_RIGHT);
        }
        if (keys[SDL_SCANCODE_W]) {
            local_buttons = static_cast<std::uint16_t>(local_buttons | net::BUTTON_MOVE_FORWARD);
        }
        if (keys[SDL_SCANCODE_S]) {
            local_buttons = static_cast<std::uint16_t>(local_buttons | net::BUTTON_MOVE_BACKWARD);
        }
        apply_buttons(map, local_buttons, enemies, player_pos, player_dir, &camera_plane);

        if (network_online) {
            if ((tick % 15) == 0) {
                const auto hello_payload = net::serialize_packet(net::Packet{net::HelloPacket{
                    .peer_id = self_peer_id,
                    .session_nonce = 1,
                    .protocol_version = net::PROTOCOL_VERSION,
                }});
                for (const auto& route : peer_routes) {
                    transport.send(route, hello_payload);
                }
            }

            const auto input_payload = net::serialize_packet(net::Packet{net::InputFramePacket{
                .peer_id = self_peer_id,
                .tick = static_cast<std::uint64_t>(tick),
                .buttons = local_buttons,
                .look_delta = 0,
            }});
            for (const auto& route : peer_routes) {
                transport.send(route, input_payload);
            }

            const auto state_payload = net::serialize_packet(net::Packet{net::StateCheckpointPacket{
                .host_peer_id = self_peer_id,
                .epoch = 0,
                .tick = static_cast<std::uint64_t>(tick),
                .player_x_raw = player_pos.x.raw(),
                .player_y_raw = player_pos.y.raw(),
                .facing_x_raw = player_dir.x.raw(),
                .facing_y_raw = player_dir.y.raw(),
            }});
            for (const auto& route : peer_routes) {
                transport.send(route, state_payload);
            }

            for (const auto& raw_packet : transport.poll()) {
                const auto decoded = net::deserialize_packet(raw_packet.payload);
                if (!decoded.has_value()) {
                    continue;
                }

                std::string logical_peer_id = raw_packet.peer_id;
                if (std::holds_alternative<net::HelloPacket>(*decoded)) {
                    logical_peer_id = std::get<net::HelloPacket>(*decoded).peer_id;
                } else if (std::holds_alternative<net::InputFramePacket>(*decoded)) {
                    logical_peer_id = std::get<net::InputFramePacket>(*decoded).peer_id;
                } else if (std::holds_alternative<net::StateCheckpointPacket>(*decoded)) {
                    logical_peer_id = std::get<net::StateCheckpointPacket>(*decoded).host_peer_id;
                }
                if (logical_peer_id.empty() || logical_peer_id == self_peer_id) {
                    continue;
                }

                const std::string endpoint_route = endpoint_from_peer_id(logical_peer_id);
                const std::string outbound_route = endpoint_route.empty() ? raw_packet.peer_id : endpoint_route;
                const bool new_peer = remote_players.find(logical_peer_id) == remote_players.end();
                route_by_peer[logical_peer_id] = outbound_route;
                peer_routes.insert(outbound_route);

                auto& remote = remote_players[logical_peer_id];
                remote.last_seen_tick = static_cast<std::uint64_t>(tick);
                if (new_peer) {
                    std::cout << "Peer joined: " << logical_peer_id << " via " << outbound_route << "\n";
                }
                if (remote.last_tick == 0 && !std::holds_alternative<net::StateCheckpointPacket>(*decoded)) {
                    remote.pos = spawn_for_peer(logical_peer_id);
                    remote.dir = {Fixed16::from_double(1.0), Fixed16::from_double(0.0)};
                }

                if (std::holds_alternative<net::InputFramePacket>(*decoded)) {
                    const auto& input = std::get<net::InputFramePacket>(*decoded);
                    apply_buttons(map, input.buttons, enemies, remote.pos, remote.dir, nullptr);
                    remote.last_tick = std::max(remote.last_tick, input.tick);
                    continue;
                }
                if (std::holds_alternative<net::StateCheckpointPacket>(*decoded)) {
                    const auto& state = std::get<net::StateCheckpointPacket>(*decoded);
                    remote.pos.x = Fixed16::from_raw(state.player_x_raw);
                    remote.pos.y = Fixed16::from_raw(state.player_y_raw);
                    remote.dir.x = Fixed16::from_raw(state.facing_x_raw);
                    remote.dir.y = Fixed16::from_raw(state.facing_y_raw);
                    remote.last_tick = std::max(remote.last_tick, state.tick);
                }
            }

            for (auto it = remote_players.begin(); it != remote_players.end();) {
                const std::uint64_t local_tick = static_cast<std::uint64_t>(tick);
                if (it->second.last_seen_tick <= local_tick && (local_tick - it->second.last_seen_tick) > 300U) {
                    const auto route_it = route_by_peer.find(it->first);
                    if (route_it != route_by_peer.end()) {
                        peer_routes.erase(route_it->second);
                        route_by_peer.erase(route_it);
                    }
                    std::cout << "Peer timed out: " << it->first << "\n";
                    it = remote_players.erase(it);
                } else {
                    ++it;
                }
            }
        }

        const bool now_space = keys[SDL_SCANCODE_SPACE] != 0;
        if (now_space && !prev_space && ammo > 0 && !weapon_anim_active) {
            --ammo;
            weapon_anim_active = true;
            weapon_anim_key_index = 0;
            weapon_anim_elapsed_ms = 0;
            weapon_frame = WEAPON_ANIM[0].frame;
            show_muzzle_flash = WEAPON_ANIM[0].muzzle_flash;
            play_weapon_sfx(WEAPON_ANIM[0].sfx);
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

        if (weapon_anim_active) {
            weapon_anim_elapsed_ms += frame_dt_ms;
            while (weapon_anim_active && weapon_anim_key_index < WEAPON_ANIM.size() &&
                   weapon_anim_elapsed_ms >= WEAPON_ANIM[weapon_anim_key_index].duration_ms) {
                weapon_anim_elapsed_ms -= WEAPON_ANIM[weapon_anim_key_index].duration_ms;
                ++weapon_anim_key_index;
                if (weapon_anim_key_index >= WEAPON_ANIM.size()) {
                    weapon_anim_active = false;
                    weapon_anim_key_index = 0;
                    weapon_anim_elapsed_ms = 0;
                    weapon_frame = 0;
                    show_muzzle_flash = false;
                } else {
                    weapon_frame = WEAPON_ANIM[weapon_anim_key_index].frame;
                    show_muzzle_flash = WEAPON_ANIM[weapon_anim_key_index].muzzle_flash;
                    play_weapon_sfx(WEAPON_ANIM[weapon_anim_key_index].sfx);
                }
            }
        }

        int w = 0;
        int h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        std::vector<Vec2Fixed> remote_positions;
        remote_positions.reserve(remote_players.size());
        for (const auto& [_, remote] : remote_players) {
            remote_positions.push_back(remote.pos);
        }
        render_world(renderer, w, h, map, player_pos, player_dir, camera_plane, enemies, remote_positions, textures);
        draw_minimap(renderer, map, player_pos, enemies, remote_positions);
        draw_weapon(renderer, w, h, textures, weapon_frame, show_muzzle_flash, static_cast<double>(tick) * 0.12);
        SDL_RenderPresent(renderer);

        const auto now = std::chrono::steady_clock::now();
        if (now - last_title > std::chrono::milliseconds(200)) {
            last_title = now;
            const std::string title = base_title + " | HP:" + std::to_string(hp) + " Ammo:" + std::to_string(ammo) +
                                      " Score:" + std::to_string(score) + " Wave:" + std::to_string(wave) +
                                      " Peers:" + std::to_string(remote_players.size());
            SDL_SetWindowTitle(window, title.c_str());
        }

        if (ammo <= 0 && enemies.empty()) {
            running = false;
        }
        ++tick;
    }

    destroy_textures(textures);
    destroy_framebuffer();
#ifdef BREACH_TEAM_HAS_SDL2_MIXER
    if (mixer_ready) {
        auto free_chunk = [](Mix_Chunk*& chunk) {
            if (chunk != nullptr) {
                Mix_FreeChunk(chunk);
                chunk = nullptr;
            }
        };
        free_chunk(weapon_fire);
        free_chunk(weapon_cock);
        free_chunk(weapon_heavy_fire);
        Mix_CloseAudio();
    }
    Mix_Quit();
#endif
#ifdef BREACH_TEAM_HAS_SDL2_IMAGE
    IMG_Quit();
#endif
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return (hp > 0) ? 0 : 2;
}

}  // namespace breach_team::game
