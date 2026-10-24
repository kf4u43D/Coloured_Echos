#pragma once

#include <cmath>
#include <cstdint>
#include "a04_color.h"
#include "legacy_voicings.h"

// Original, provisional rack-family structures. No recovered Line 6 DSP or
// measured product equivalence. The Engine owns the two delay memories.
namespace echolab {
namespace rack {

enum class Kind { Digital24, Platter, Stereo };

struct Controls {
    float delaySamples;
    float feedback;
    float tonePole;
    float drive;
    float modDepthSamples;
    float modPhase;
    float stereoRatio;
    float maximumSamples;
    int voicing=3;
};

struct Frame {
    float writeL;
    float writeR;
    float wetL;
    float wetR;
};

class State {
public:
    void prepare(float sampleRate,float internalRate=0) noexcept {
        const float fs=bound(sampleRate,8000.0f,384000.0f,48000.0f);
        const float rate=internalRate == 0 ? fs : bound(internalRate,1000.0f,fs,16000.0f);
        reducedRate_=rate < fs;rateRatio_=rate/fs;
        // Four gentle unity-DC anti-alias sections before the converter clock,
        // then two reconstruction sections after its hold. This is a coloured
        // converter model, not an ideal brickwall sampling theorem guarantee.
        ratePole_=reducedRate_ ? std::exp(-6.2831853071795864769f*0.45f*rate/fs) : 0;
        saturator_.prepare(fs);a05Saturator_.prepare(fs);
    }
    void reset() noexcept {
        for (unsigned channel = 0; channel != 2; ++channel)
            low_[channel] = dcInput_[channel] = dcOutput_[channel] = 0.0f;
        saturator_.reset();a05Saturator_.reset();
        ratePhase_=1;held_[0]=held_[1]=0;
        for (auto& channel:antiAlias_) for (auto& stage:channel) stage=0;
        for (auto& channel:reconstruction_) for (auto& stage:channel) stage=0;
    }

    // read(channel, delay) reads OLD delay memory, including fractional delays.
    // This function never writes that memory. The caller commits BOTH writes
    // only after process() returns, preserving channel aliases and head reads.
    template<class Reader>
    Frame process(Kind kind, float inL, float inR, const Controls& c, Reader read) noexcept {
        return process(kind,inL,inR,c,read,[](unsigned,float value) noexcept {return value;});
    }
    template<class Reader,class FeedbackFilter>
    Frame process(Kind kind, float inL, float inR, const Controls& c, Reader read,FeedbackFilter feedbackFilter) noexcept {
        const float maximum = std::isfinite(c.maximumSamples) && c.maximumSamples >= 1.0f
                                  ? c.maximumSamples : 1.0f;
        const float time = delay(c.delaySamples, maximum);
        const float feedback = bound(c.feedback, 0.0f, 2.0f, 0.0f);
        if (kind == Kind::Digital24) {
            const float left = signal(read(0, time));
            const float right = signal(read(1, time));
            const float input[2]={signal(signal(inL)+feedback*feedbackFilter(0,left)),
                                  signal(signal(inR)+feedback*feedbackFilter(1,right))};
            float write[2];
            if (!reducedRate_) {
                // Preserve the old full-Fs path exactly, while warming scalar
                // converter state for a later rate change without clearing RAM.
                ratePhase_=1;
                for (unsigned channel=0;channel<2;++channel) {
                    write[channel]=held_[channel]=quantize24(input[channel]);
                    for (auto& stage:antiAlias_[channel]) stage=write[channel];
                    for (auto& stage:reconstruction_[channel]) stage=write[channel];
                }
            } else {
                const bool capture=ratePhase_ >= 1;
                if (capture) ratePhase_-=1;
                ratePhase_+=rateRatio_;
                for (unsigned channel=0;channel<2;++channel) {
                    float filtered=input[channel];
                    for (auto& stage:antiAlias_[channel]) {
                        stage=signal(stage+(1-ratePole_)*(filtered-stage));filtered=stage;
                    }
                    if (capture) held_[channel]=quantize24(filtered);
                    filtered=held_[channel];
                    for (auto& stage:reconstruction_[channel]) {
                        stage=signal(stage+(1-ratePole_)*(filtered-stage));filtered=stage;
                    }
                    write[channel]=quantize24(filtered);
                }
            }
            return {write[0],write[1],left,right};
        }
        if (kind == Kind::Stereo) {
            const float ratio = std::isfinite(c.stereoRatio) && c.stereoRatio > 0.0f
                                    ? c.stereoRatio : 1.0f;
            // Compare before multiplication so adversarial ratios cannot overflow.
            const float rightTime = ratio > maximum / time ? maximum : delay(time * ratio, maximum);
            const float left = signal(read(0, time));
            const float right = signal(read(1, rightTime));
            return {signal(inL + feedback * feedbackFilter(0,left)), signal(inR + feedback * feedbackFilter(1,right)), left, right};
        }

        // One rotation phase drives correlated wow plus a seventh-harmonic
        // triangular flutter. Coefficients/time preparation belongs to Engine;
        // there are no transcendental functions, allocation or I/O here.
        const float phase = bound(c.modPhase, 0.0f, 1.0f, 0.0f);
        float flutterPhase = 7.0f * phase;
        flutterPhase -= static_cast<unsigned>(flutterPhase);
        const float motion = 0.8f * triangle(phase) + 0.2f * triangle(flutterPhase);
        const float depth = bound(c.modDepthSamples, 0.0f, 0.1f * time, 0.0f);
        const float longTime = delay(time + depth * motion, maximum);
        const float halfTime = delay(0.5f * time + 0.5f * depth * motion, maximum);
        const float wetL = 0.75f * signal(read(0, longTime)) + 0.25f * signal(read(0, halfTime));
        const float wetR = 0.75f * signal(read(1, longTime)) + 0.25f * signal(read(1, halfTime));
        const float pole = bound(c.tonePole, 0.0f, 0.999999f, 0.0f);
        const float drive = bound(c.drive, 0.0f, 1.0f, 0.0f);
        // Derive a DC pole from the prepared tone pole. Its corner tracks Tone
        // at approximately 1/300; no fixed-rate coefficient is reused at a new Fs.
        const float dcScale = (1.0f - pole) / 300.0f;
        const float dcPole = (1.0f + pole - dcScale) / (1.0f + pole + dcScale);
        return {colour(0, signal(inL + feedback * feedbackFilter(0,wetL)), pole, drive, dcPole,c.voicing),
                colour(1, signal(inR + feedback * feedbackFilter(1,wetR)), pole, drive, dcPole,c.voicing), wetL, wetR};
    }

private:
    static float bound(float value, float minimum, float maximum, float fallback) noexcept {
        if (!std::isfinite(value)) return fallback;
        return value < minimum ? minimum : (value > maximum ? maximum : value);
    }
    static float signal(float value) noexcept {
        // Same emergency numerical ceiling as the common engine, outside the
        // normalized signal domain. Digital24 has its explicit audio saturation.
        return bound(value, -1.0e12f, 1.0e12f, 0.0f);
    }
    static float delay(float value, float maximum) noexcept {
        return bound(value, 1.0f, maximum, maximum);
    }
    static float triangle(float phase) noexcept {
        return 4.0f * std::fabs(phase - 0.5f) - 1.0f;
    }
    static float quantize24(float value) noexcept {
        constexpr float scale = 8388608.0f;
        value = bound(value, -1.0f, 1.0f - 1.0f / scale, 0.0f);
        // Truncation towards zero is an explicit prototype choice, not a claim
        // about the original hardware's rounding. Cast stays inside int32 range.
        return static_cast<float>(static_cast<std::int32_t>(value * scale)) / scale;
    }
    float colour(unsigned channel, float input, float pole, float drive, float dcPole,int voicing) noexcept {
        const float driven=voicing == 1 ? legacy::saturate(input,drive,5) :
            (voicing == 2 ? a05Saturator_.process(input,channel,drive) : saturator_.process(input,channel,drive,color::Flavor::Platter));
        low_[channel] = signal(low_[channel] + (1.0f - pole) * (driven - low_[channel]));
        const float output = signal(low_[channel] - dcInput_[channel] + dcPole * dcOutput_[channel]);
        dcInput_[channel] = low_[channel];
        dcOutput_[channel] = output;
        return output;
    }

    float low_[2]{};
    float dcInput_[2]{};
    float dcOutput_[2]{};
    color::A04Color saturator_{};
    color::Saturator a05Saturator_{};
    bool reducedRate_=false;
    float rateRatio_=1,ratePole_=0,ratePhase_=1;
    float antiAlias_[2][4]{};
    float reconstruction_[2][2]{};
    float held_[2]{};
};

} // namespace rack
} // namespace echolab
