#pragma once

#include <cmath>
#include "analog_color.h"

// Restores OUR archived prototype candidates. This is not recovered Line 6 DSP.
// A04 saturation/Tube equations were verified against the archived 0.4 ARM ELF;
// exciter equations against its ELF and packaged EXCITER_DESIGN. Original C++
// files are absent. A05 exciter follows the prior literal tool read, while its
// Saturator dependency is byte-identical to the archived 0.5 source hash.
namespace echolab { namespace legacy {
namespace detail {
inline float bound(float x,float low,float high,float fallback) noexcept {
    if (!std::isfinite(x)) return fallback;
    return x<low ? low : (x>high ? high : x);
}
inline float finite(float x) noexcept { return bound(x,-1.0e12f,1.0e12f,0.0f); }
}

inline float saturate(float input,float drive,float gainFactor=7.0f) noexcept {
    const float x=detail::finite(input),d=detail::bound(drive,0.0f,1.0f,0.0f);
    const float factor=detail::bound(gainFactor,0.0f,15.0f,7.0f);
    const float z=(1.0f+factor*d)*x;
    // Archived ELF .text+0x38..0x58: denominator includes Drive. Replacing this
    // by 1+abs(z) would silently change A04 and break its Drive0 identity.
    return z/(1.0f+d*std::fabs(z));
}
inline float tube(float input,float drive) noexcept {
    const float x=detail::finite(input),d=detail::bound(drive,0.0f,1.0f,0.0f);
    // ELF Engine::processSample +0xca2: asymmetry uses the PRE-saturation input.
    return saturate(x,d)+0.12f*d*x*x/(1.0f+x*x);
}

class ExciterA04 {
public:
    void prepare(float sampleRate,float tuneHz) noexcept {
        const float fs=detail::bound(sampleRate,8000.0f,384000.0f,48000.0f);
        const float maximum=0.4f*fs<10000.0f ? 0.4f*fs : 10000.0f;
        const float tune=detail::bound(tuneHz,200.0f,maximum,3000.0f);
        constexpr float twoPi=6.2831853071795864769f;
        splitPole_=std::exp(-twoPi*tune/fs);
        dcPole_=std::exp(-twoPi*20.0f/fs);
        const float upper=16000.0f<0.45f*fs ? 16000.0f : 0.45f*fs;
        postPole_=std::exp(-twoPi*upper/fs);
    }
    void reset() noexcept {
        for (unsigned c=0;c<2;++c) low_[c]=previous_[c]=dc_[c]=post_[c]=0;
    }
    float process(float sample,unsigned channel,float amount,float drive) noexcept {
        if (!std::isfinite(sample)) sample=0;
        const float mix=detail::bound(amount,0.0f,1.0f,0.0f),d=detail::bound(drive,0.0f,1.0f,0.0f);
        if (channel>=2 || mix==0 || d==0) return sample;
        const float input=detail::finite(sample);
        low_[channel]=detail::finite(low_[channel]+(1.0f-splitPole_)*(input-low_[channel]));
        const float high=input-low_[channel],u=(1.0f+15.0f*d)*high;
        const float square=u*u; // Archived ELF computes u squared before the weighted even term.
        const float shape=u/(1.0f+std::fabs(u))-high/(1.0f+std::fabs(high))+0.25f*d*square/(1.0f+square);
        dc_[channel]=detail::finite(shape-previous_[channel]+dcPole_*dc_[channel]);
        previous_[channel]=shape;
        post_[channel]=detail::finite(post_[channel]+(1.0f-postPole_)*(dc_[channel]-post_[channel]));
        return detail::finite(input+2.0f*mix*post_[channel]);
    }
private:
    float splitPole_=0.675231907f,dcPole_=0.997385442f,postPole_=0.123144711f;
    float low_[2]{},previous_[2]{},dc_[2]{},post_[2]{};
};

class ExciterA05 {
public:
    void prepare(float sampleRate,float tuneHz) noexcept {
        const float fs=detail::bound(sampleRate,8000.0f,384000.0f,48000.0f);
        const float maximum=0.4f*fs<10000.0f ? 0.4f*fs : 10000.0f;
        const float tune=detail::bound(tuneHz,200.0f,maximum,2500.0f);
        constexpr float twoPi=6.2831853071795864769f;
        splitPole_=std::exp(-twoPi*tune/fs);
        dcPole_=std::exp(-twoPi*20.0f/fs);
        const float upper=12000.0f<0.45f*fs ? 12000.0f : 0.45f*fs;
        postPole_=std::exp(-twoPi*upper/fs);
        saturation_.prepare(fs);
    }
    void reset() noexcept {
        for (unsigned c=0;c<2;++c) low_[c]=previous_[c]=dc_[c]=post_[c]=0;
        saturation_.reset();
    }
    float process(float sample,unsigned channel,float amount,float drive) noexcept {
        if (!std::isfinite(sample)) sample=0;
        const float mix=detail::bound(amount,0.0f,1.0f,0.0f),d=detail::bound(drive,0.0f,1.0f,0.0f);
        if (channel>=2 || mix==0 || d==0) return sample;
        const float input=detail::finite(sample);
        low_[channel]=detail::finite(low_[channel]+(1.0f-splitPole_)*(input-low_[channel]));
        const float high=input-low_[channel];
        const float shape=saturation_.residue(high,channel,d);
        dc_[channel]=detail::finite(shape-previous_[channel]+dcPole_*dc_[channel]);
        previous_[channel]=shape;
        post_[channel]=detail::finite(post_[channel]+(1.0f-postPole_)*(dc_[channel]-post_[channel]));
        return detail::finite(input+mix*post_[channel]);
    }
private:
    float splitPole_=0.720904052f,dcPole_=0.997385442f,postPole_=0.207879576f;
    color::Saturator saturation_{};
    float low_[2]{},previous_[2]{},dc_[2]{},post_[2]{};
};
} } // namespace echolab::legacy
