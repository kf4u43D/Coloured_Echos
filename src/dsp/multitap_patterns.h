#pragma once

namespace echolab { namespace multitap {

struct Tap { float offset; float gain; };
struct Pattern { const char* name; unsigned count; Tap taps[4]; };
inline constexpr unsigned patternCount = 8;
inline constexpr Pattern patterns[patternCount] = {
    {"Legacy triplet", 3, {{1.0f/3.0f,1.0f/3.0f},{2.0f/3.0f,1.0f/3.0f},{1,1.0f/3.0f},{1,0}}},
    {"Quarter train", 4, {{0.25f,0.25f},{0.5f,0.25f},{0.75f,0.25f},{1,0.25f}}},
    {"Binary", 2, {{0.5f,0.5f},{1,0.5f},{1,0},{1,0}}},
    {"Dotted", 3, {{0.375f,0.25f},{0.75f,0.375f},{1,0.375f},{1,0}}},
    {"Golden", 3, {{0.382f,1.0f/3.0f},{0.618f,1.0f/3.0f},{1,1.0f/3.0f},{1,0}}},
    {"Gallop", 3, {{0.25f,0.25f},{0.75f,0.25f},{1,0.5f},{1,0}}},
    {"Clustered", 3, {{0.75f,0.25f},{0.875f,0.25f},{1,0.5f},{1,0}}},
    {"Singleton", 1, {{1,1},{1,0},{1,0},{1,0}}}
};

constexpr const Pattern& pattern(unsigned index) noexcept { return patterns[index < patternCount ? index : 0]; }
constexpr unsigned tapCount(unsigned index) noexcept { return pattern(index).count; }
constexpr Tap tap(unsigned index, unsigned head) noexcept {
    return head < pattern(index).count ? pattern(index).taps[head] : Tap{1,0};
}
constexpr const char* name(unsigned index) noexcept { return pattern(index).name; }
constexpr const char* patternName(unsigned index) noexcept { return name(index); }

} } // namespace echolab::multitap
