#pragma once

#include "breach_team/core/fixed.hpp"

namespace breach_team::core {

struct Vec2Fixed {
    Fixed16 x;
    Fixed16 y;

    constexpr Vec2Fixed operator+(Vec2Fixed other) const {
        return {x + other.x, y + other.y};
    }

    constexpr Vec2Fixed operator-(Vec2Fixed other) const {
        return {x - other.x, y - other.y};
    }

    constexpr Vec2Fixed operator*(Fixed16 scalar) const {
        return {x * scalar, y * scalar};
    }
};

struct Mat2Fixed {
    Fixed16 m00;
    Fixed16 m01;
    Fixed16 m10;
    Fixed16 m11;

    constexpr Vec2Fixed operator*(Vec2Fixed value) const {
        return {
            (m00 * value.x) + (m01 * value.y),
            (m10 * value.x) + (m11 * value.y),
        };
    }
};

}  // namespace breach_team::core
