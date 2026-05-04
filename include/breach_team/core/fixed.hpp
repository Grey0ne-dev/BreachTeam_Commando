#pragma once

#include <cstdint>

namespace breach_team::core {

class Fixed16 {
public:
    static constexpr int FRACTION_BITS = 16;
    static constexpr std::int32_t SCALE = 1 << FRACTION_BITS;

    constexpr Fixed16() = default;

    static constexpr Fixed16 from_raw(std::int32_t raw) {
        Fixed16 value;
        value.raw_ = raw;
        return value;
    }

    static constexpr Fixed16 from_int(int value) {
        return from_raw(static_cast<std::int32_t>(value) * SCALE);
    }

    static constexpr Fixed16 from_double(double value) {
        return from_raw(static_cast<std::int32_t>(value * static_cast<double>(SCALE)));
    }

    constexpr std::int32_t raw() const {
        return raw_;
    }

    constexpr int to_int() const {
        return static_cast<int>(raw_ / SCALE);
    }

    constexpr double to_double() const {
        return static_cast<double>(raw_) / static_cast<double>(SCALE);
    }

    constexpr Fixed16 operator+(Fixed16 other) const {
        return from_raw(raw_ + other.raw_);
    }

    constexpr Fixed16 operator-(Fixed16 other) const {
        return from_raw(raw_ - other.raw_);
    }

    constexpr Fixed16 operator*(Fixed16 other) const {
        const std::int64_t product = static_cast<std::int64_t>(raw_) * static_cast<std::int64_t>(other.raw_);
        return from_raw(static_cast<std::int32_t>(product >> FRACTION_BITS));
    }

    constexpr Fixed16 operator/(Fixed16 other) const {
        const std::int64_t numerator = static_cast<std::int64_t>(raw_) << FRACTION_BITS;
        return from_raw(static_cast<std::int32_t>(numerator / other.raw_));
    }

    constexpr Fixed16& operator+=(Fixed16 other) {
        raw_ += other.raw_;
        return *this;
    }

    constexpr Fixed16& operator-=(Fixed16 other) {
        raw_ -= other.raw_;
        return *this;
    }

private:
    std::int32_t raw_ = 0;
};

}  // namespace breach_team::core
