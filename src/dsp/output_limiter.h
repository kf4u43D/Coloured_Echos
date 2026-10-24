#pragma once

#include <cmath>

namespace echolab { namespace safety {

inline constexpr float maximumCeiling = 0.891250938f; // 10^(-1 dB / 20).

// Linked stereo sample-peak protection, without lookahead or latency. The
// musical stage has a progressive knee and fast/slow release envelopes. The
// final linked guard remains active when that stage is disabled.
class Limiter {
public:
    void reset() noexcept { fastReduction_ = slowReduction_ = 0.0f; }
    void prepare(float sampleRate, float releaseMs) noexcept {
        const float fs = bound(sampleRate, 8000.0f, 384000.0f, 48000.0f);
        const float release = bound(releaseMs, 1.0f, 10000.0f, 150.0f);
        const float fast = release * 0.2f < 1.0f ? 1.0f : release * 0.2f;
        fastPole_ = std::exp(-1000.0f / (fast * fs));
        slowPole_ = std::exp(-1000.0f / (release * fs));
    }
    void setCeiling(float linear) noexcept {
        ceiling_ = bound(linear, 0.01f, maximumCeiling, maximumCeiling);
    }
    void releaseIdle() noexcept {
        fastReduction_ = quiet(fastReduction_ * fastPole_);
        slowReduction_ = quiet(slowReduction_ * slowPole_);
    }
    void guard(float& left, float& right) const noexcept {
        left = std::isfinite(left) ? left : 0.0f;
        right = std::isfinite(right) ? right : 0.0f;
        const float l = std::fabs(left), r = std::fabs(right);
        const float peak = l > r ? l : r;
        if (peak > ceiling_) {
            const float scale = ceiling_ / peak;
            left *= scale;
            right *= scale;
        }
        // Roundoff cannot breach the ceiling, including after a ceiling change.
        left = bound(left, -ceiling_, ceiling_, 0.0f);
        right = bound(right, -ceiling_, ceiling_, 0.0f);
    }
    void process(float& left, float& right, bool musicalEnabled = true) noexcept {
        left = std::isfinite(left) ? left : 0.0f;
        right = std::isfinite(right) ? right : 0.0f;
        if (!musicalEnabled) {
            reset();
            guard(left, right);
            return;
        }
        const float l = std::fabs(left), r = std::fabs(right);
        const float peak = l > r ? l : r;
        const float start = 0.8f * ceiling_, end = 1.2f * ceiling_;
        float requested = 1.0f;
        if (peak >= end) requested = ceiling_ / peak;
        else if (peak > start) {
            const float distance = peak - start;
            const float desired = peak - distance * distance / (0.8f * ceiling_);
            requested = desired / peak;
        }
        follow(fastReduction_, 1.0f - requested, fastPole_);
        follow(slowReduction_, 1.0f - requested, slowPole_);
        const float applied = gain();
        left *= applied;
        right *= applied;
        guard(left, right);
    }
    float gain() const noexcept { return 1.0f - (0.65f * fastReduction_ + 0.35f * slowReduction_); }
    float ceiling() const noexcept { return ceiling_; }

private:
    static void follow(float& state, float requested, float pole) noexcept {
        state = quiet(requested > state ? requested : requested + pole * (state - requested));
    }
    static float quiet(float reduction) noexcept { return reduction < 1.0e-12f ? 0.0f : reduction; }
    static float bound(float value, float minimum, float maximum, float fallback) noexcept {
        if (!std::isfinite(value)) return fallback;
        return value < minimum ? minimum : (value > maximum ? maximum : value);
    }
    // Store reduction toward zero, rather than gain toward one: a long release
    // must not get stuck below unity because of cancellation at high rates.
    float fastReduction_ = 0.0f, slowReduction_ = 0.0f;
    float ceiling_ = maximumCeiling;
    float fastPole_ = 0.999305785f; // 30 ms at 48 kHz until prepare().
    float slowPole_ = 0.999861121f; // 150 ms at 48 kHz until prepare().
};

} } // namespace echolab::safety
