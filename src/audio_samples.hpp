#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace quiet::sound {
constexpr unsigned rate = 44100;
inline std::vector<std::int16_t> samples(bool completion) {
    const double duration = completion ? 1.35 : 0.65;
    std::vector<std::int16_t> result(static_cast<std::size_t>(duration * rate));
    for (std::size_t i = 0; i < result.size(); ++i) {
        const double time = static_cast<double>(i) / rate;
        auto note = [time](double start, double hz) {
            const double t = time - start;
            if (t < 0 || t > 0.65) return 0.0;
            const double envelope = std::min(1.0, t / 0.012) * std::exp(-7.0 * t);
            return envelope * (std::sin(6.28318530718 * hz * t) + 0.18 * std::sin(6.28318530718 * hz * 2 * t));
        };
        double value = note(0, 660);
        if (completion) value += note(0.28, 880) + note(0.56, 1100);
        result[i] = static_cast<std::int16_t>(value * 9500);
    }
    return result;
}

// WAV is explicitly little endian, independent of the machine's byte order.
inline std::vector<unsigned char> wav(const std::vector<std::int16_t>& pcm) {
    std::vector<unsigned char> bytes;
    bytes.reserve(44 + pcm.size() * 2);
    auto text = [&](const char* s) { for (; *s; ++s) bytes.push_back(static_cast<unsigned char>(*s)); };
    auto word = [&](std::uint32_t value, int count) {
        for (int i = 0; i < count; ++i) bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
    };
    text("RIFF"); word(static_cast<std::uint32_t>(36 + pcm.size() * 2), 4); text("WAVEfmt ");
    word(16, 4); word(1, 2); word(1, 2); word(rate, 4); word(rate * 2, 4); word(2, 2); word(16, 2);
    text("data"); word(static_cast<std::uint32_t>(pcm.size() * 2), 4);
    for (auto sample : pcm) word(static_cast<std::uint16_t>(sample), 2);
    return bytes;
}
} // namespace quiet::sound
