#pragma once

#include <cmath>

namespace echolab { namespace color {

namespace detail {
inline float bound(float value, float low, float high, float fallback) noexcept {
    if (!std::isfinite(value)) return fallback;
    return value < low ? low : (value > high ? high : value);
}
inline float sample(float value) noexcept { return bound(value,-1.0e12f,1.0e12f,0.0f); }
}

// Original unity-small-signal soft saturation, inspired by gentle nonlinear
// blending rather than a recovered commercial transfer. No preamplification.
// A 2x linearly interpolated path and three-tap decimator reduce generated alias.
class Saturator {
public:
    void prepare(float sampleRate) noexcept {
        sampleRate_=detail::bound(sampleRate,8000.0f,384000.0f,48000.0f);
    }
    void reset() noexcept { previous_[0]=previous_[1]=0.0f; }
    float process(float input,unsigned channel,float drive) noexcept {
        return evaluate(input,channel,drive,false);
    }
    // Harmonic-only difference from the SAME 2x linear path. This removes the
    // first-order filtering term; residue tends cubically to zero with level.
    float residue(float input,unsigned channel,float drive) noexcept {
        return evaluate(input,channel,drive,true);
    }
    // Low-frequency group-delay approximation of the blended linear path.
    float lowFrequencyDelaySeconds(float drive) const noexcept {
        return 0.5f*detail::bound(drive,0.0f,1.0f,0.0f)/sampleRate_;
    }
    static float transfer(float input,float drive) noexcept {
        const float x=detail::sample(input),d=detail::bound(drive,0.0f,1.0f,0.0f);
        if (d==0) return x;
        const float scale=0.25f+1.75f*d;
        return (1.0f-d)*x+d*soft(scale*x)/scale;
    }

private:
    static float soft(float x) noexcept {
        if (x>=1.0f) return 2.0f/3.0f;
        if (x<=-1.0f) return -2.0f/3.0f;
        return x-x*x*x/3.0f;
    }
    float evaluate(float input,unsigned channel,float drive,bool residualOnly) noexcept {
        const float x=detail::sample(input),d=detail::bound(drive,0.0f,1.0f,0.0f);
        if (channel>=2) return residualOnly ? 0.0f : x;
        const float previous=previous_[channel];
        previous_[channel]=x;
        if (d==0) return residualOnly ? 0.0f : x;
        const float scale=0.25f+1.75f*d;
        const float middle=0.5f*(previous+x);
        const float shaped=(0.25f*soft(scale*previous)+0.5f*soft(scale*middle)+0.25f*soft(scale*x))/scale;
        if (residualOnly) return detail::sample(d*(shaped-0.5f*(previous+x)));
        return detail::sample((1.0f-d)*x+d*shaped);
    }
    float sampleRate_=48000.0f;
    float previous_[2]{};
};

// Unity-DC-gain feedback filter. Prepared pole interpolation provides the small
// cutoff drift without any per-sample exp/tan. It affects feedback only in Engine.
class FeedbackLowpass {
public:
    void reset() noexcept { state_[0]=state_[1]=0.0f; }
    void prepare(float sampleRate,float cutoffHz) noexcept {
        const float fs=detail::bound(sampleRate,8000.0f,384000.0f,48000.0f);
        bypass_=!std::isfinite(cutoffHz)||cutoffHz<=0;
        if (bypass_) { lowPole_=pole_=highPole_=0;return; }
        const float cutoff=detail::bound(cutoffHz,10.0f,0.45f*fs,12000.0f);
        constexpr float twoPi=6.2831853071795864769f;
        lowPole_=std::exp(-twoPi*(cutoff*0.98f)/fs);
        pole_=std::exp(-twoPi*cutoff/fs);
        highPole_=std::exp(-twoPi*(cutoff*1.02f)/fs);
    }
    float process(float input,unsigned channel,float cutoffMod=0.0f) noexcept {
        const float x=detail::sample(input);
        if (channel>=2) return x;
        if (bypass_) { state_[channel]=x;return x; }
        const float modulation=detail::bound(cutoffMod,-1.0f,1.0f,0.0f);
        const float p=modulation>=0 ? pole_+modulation*(highPole_-pole_) : pole_-modulation*(lowPole_-pole_);
        state_[channel]=detail::sample(state_[channel]+(1.0f-p)*(x-state_[channel]));
        return state_[channel];
    }
private:
    bool bypass_=true;
    float lowPole_=0,pole_=0,highPole_=0;
    float state_[2]{};
};

} } // namespace echolab::color
