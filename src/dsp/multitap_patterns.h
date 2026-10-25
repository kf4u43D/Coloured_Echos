#pragma once

namespace echolab { namespace multitap {

struct Tap { float offset; float gain; };
inline constexpr unsigned tapCapacity = 8;
struct Pattern { const char* name; unsigned count; Tap taps[tapCapacity]; };
inline constexpr unsigned patternCount = 11;
inline constexpr Pattern patterns[patternCount] = {
    {"Legacy triplet", 3, {{1.0f/3.0f,1.0f/3.0f},{2.0f/3.0f,1.0f/3.0f},{1,1.0f/3.0f},{1,0}}},
    {"Quarter train", 4, {{0.25f,0.25f},{0.5f,0.25f},{0.75f,0.25f},{1,0.25f}}},
    {"Binary", 2, {{0.5f,0.5f},{1,0.5f},{1,0},{1,0}}},
    {"Dotted", 3, {{0.375f,0.25f},{0.75f,0.375f},{1,0.375f},{1,0}}},
    {"Golden", 3, {{0.382f,1.0f/3.0f},{0.618f,1.0f/3.0f},{1,1.0f/3.0f},{1,0}}},
    {"Gallop", 3, {{0.25f,0.25f},{0.75f,0.25f},{1,0.5f},{1,0}}},
    {"Clustered", 3, {{0.75f,0.25f},{0.875f,0.25f},{1,0.5f},{1,0}}},
    {"Singleton", 1, {{1,1},{1,0},{1,0},{1,0}}},
    // Merge two uniform pulse grids over one feedback cycle. The common
    // endpoint contains both pulses; normalization keeps their total gain at1.
    {"Polyrhythm 3:2", 4, {{1.0f/3.0f,1.0f/5.0f},{1.0f/2.0f,1.0f/5.0f},
                           {2.0f/3.0f,1.0f/5.0f},{1,2.0f/5.0f}}},
    {"Polyrhythm 4:3", 6, {{1.0f/4.0f,1.0f/7.0f},{1.0f/3.0f,1.0f/7.0f},
                           {1.0f/2.0f,1.0f/7.0f},{2.0f/3.0f,1.0f/7.0f},
                           {3.0f/4.0f,1.0f/7.0f},{1,2.0f/7.0f}}},
    {"Polyrhythm 5:4", 8, {{1.0f/5.0f,1.0f/9.0f},{1.0f/4.0f,1.0f/9.0f},
                           {2.0f/5.0f,1.0f/9.0f},{1.0f/2.0f,1.0f/9.0f},
                           {3.0f/5.0f,1.0f/9.0f},{3.0f/4.0f,1.0f/9.0f},
                           {4.0f/5.0f,1.0f/9.0f},{1,2.0f/9.0f}}}
};

constexpr const Pattern& pattern(unsigned index) noexcept { return patterns[index < patternCount ? index : 0]; }
constexpr unsigned tapCount(unsigned index) noexcept { return pattern(index).count; }
constexpr Tap tap(unsigned index, unsigned head) noexcept {
    return head < pattern(index).count ? pattern(index).taps[head] : Tap{1,0};
}
constexpr const char* name(unsigned index) noexcept { return pattern(index).name; }
constexpr const char* patternName(unsigned index) noexcept { return name(index); }

} } // namespace echolab::multitap
