#pragma once

#include <cmath>
#include "analog_color.h"

namespace echolab { namespace exciter {

// Original harmonic sidechain for a feedback path, not an Aphex/BBE algorithm.
// All rate-dependent coefficients are prepared at control cadence.
class State {
public:
    void reset() noexcept {
        for (unsigned channel = 0; channel != 2; ++channel)
            low_[channel] = shapePrevious_[channel] = dc_[channel] = post_[channel] = 0.0f;
        previousHigh_[0]=previousHigh_[1]=0.0f;
    }
    void prepare(float sampleRate, float tuneHz) noexcept {
        const float fs = bound(sampleRate, 8000.0f, 384000.0f, 48000.0f);
        const float maximum = 0.4f * fs < 10000.0f ? 0.4f * fs : 10000.0f;
        const float tune = bound(tuneHz, 200.0f, maximum, 3000.0f);
        constexpr float twoPi = 6.2831853071795864769f;
        splitPole_ = std::exp(-twoPi * tune / fs);
        dcPole_ = std::exp(-twoPi * 20.0f / fs);
        const float upper = 0.45f * fs < 12000.0f ? 0.45f * fs : 12000.0f;
        postPole_ = std::exp(-twoPi * upper / fs);
    }
    // Compensation is entirely inside the HIGH band. Dividing the complete
    // feedback signal by the A04 high-band gain would wrongly empty the bass.
    static float compensationFeedbackGain(float, float) noexcept { return 1.0f; }
    float process(float sample, unsigned channel, float amount, float drive, float feedback=0.75f) noexcept {
        if (!std::isfinite(sample)) sample = 0.0f;
        const float mix = bound(amount, 0.0f, 1.0f, 0.0f);
        const float intensity = bound(drive, 0.0f, 1.0f, 0.0f);
        // Exact identity and no state change when disabled. Invalid channel never
        // accesses state. Re-enable transitions are smoothed by the common core.
        if (channel >= 2 || mix == 0.0f || intensity == 0.0f) return sample;
        const float input = finite(sample);
        low_[channel] = finite(low_[channel] + (1.0f - splitPole_) * (input - low_[channel]));
        const float high = input - low_[channel];
        const float previous=previousHigh_[channel];previousHigh_[channel]=high;
        const float gain=1.0f+15.0f*intensity;
        // Upper bound on the static sidechain derivative. The .17 term bounds
        // the asymmetric harmonic generator, whose actual maximum is <.163.
        const float highGain=1.0f+2.0f*mix*(gain-1.0f+0.17f*intensity*gain);
        float tilt=1.0f+0.25f*mix;
        const float fb=bound(feedback,0.0f,2.0f,0.75f);
        if (fb>0.75f) {
            const float maximumTilt=fb<0.98f ? 0.98f/fb : 1.0f;
            if (tilt>maximumTilt) tilt=maximumTilt;
        }
        const float a=band(previous,gain,intensity,mix,highGain,tilt);
        const float b=band(0.5f*(previous+high),gain,intensity,mix,highGain,tilt);
        const float c=band(high,gain,intensity,mix,highGain,tilt);
        // The same 2x linear branch is subtracted, avoiding an unintended
        // low-band gain/phase change. Brilliance is modest, harmonics strong.
        const float shape=0.25f*a+0.5f*b+0.25f*c-0.5f*(previous+high);
        dc_[channel] = finite(shape - shapePrevious_[channel] + dcPole_ * dc_[channel]);
        shapePrevious_[channel] = shape;
        post_[channel] = finite(post_[channel] + (1.0f - postPole_) * (dc_[channel] - post_[channel]));
        return finite(input + post_[channel]);
    }

private:
    static float bound(float value, float minimum, float maximum, float fallback) noexcept {
        if (!std::isfinite(value)) return fallback;
        return value < minimum ? minimum : (value > maximum ? maximum : value);
    }
    static float finite(float value) noexcept { return bound(value, -1.0e12f, 1.0e12f, 0.0f); }
    static float band(float high,float gain,float drive,float amount,float compensation,float tilt) noexcept {
        const float u=gain*high;
        const float saturated=u/(1.0f+std::fabs(u));
        const float baseline=high/(1.0f+std::fabs(high));
        const float even=0.25f*drive*u*u/(1.0f+u*u);
        return tilt*(high+2.0f*amount*(saturated-baseline+even))/compensation;
    }
    // Defaults correspond to 48 kHz / 3 kHz, prepared again by the common core.
    float splitPole_ = 0.675231907f;
    float dcPole_ = 0.997385442f;
    float postPole_ = 0.207879576f;
    float previousHigh_[2]{};
    float low_[2]{};
    float shapePrevious_[2]{};
    float dc_[2]{};
    float post_[2]{};
};

} } // namespace echolab::exciter
