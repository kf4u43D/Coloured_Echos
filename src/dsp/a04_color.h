#pragma once

#include "analog_color.h"

namespace echolab { namespace color {

enum class A04ColorFlavor { Analog, Platter };
using Flavor = A04ColorFlavor;

// Original A04-inspired rational colour. The caller divides the feedback branch
// by compensationFeedbackGain BEFORE adding new input. This retains the strong
// first pass while removing the extra small-signal loop gain.
class A04Color {
public:
    void prepare(float) noexcept {}
    void reset() noexcept { previous_[0]=previous_[1]=0.0f; }
    static float compensationFeedbackGain(float drive,A04ColorFlavor flavor=A04ColorFlavor::Analog) noexcept {
        const float d=detail::bound(drive,0.0f,1.0f,0.0f);
        return 1.0f+(flavor==A04ColorFlavor::Platter ? 5.0f : 7.0f)*d;
    }
    static float transfer(float input,float drive,A04ColorFlavor flavor=A04ColorFlavor::Analog) noexcept {
        const float x=detail::sample(input),d=detail::bound(drive,0.0f,1.0f,0.0f);
        if (d==0) return x;
        // A short control transition makes Drive=0 continuous. At Drive>=.25,
        // this is the exact A04 rational transfer, before anti-alias filtering.
        const float blend=d<0.25f ? 4.0f*d : 1.0f;
        const float z=compensationFeedbackGain(d,flavor)*x;
        return z/(1.0f+blend*std::fabs(z));
    }
    float process(float input,unsigned channel,float drive,A04ColorFlavor flavor=A04ColorFlavor::Analog) noexcept {
        const float x=detail::sample(input),d=detail::bound(drive,0.0f,1.0f,0.0f);
        if (channel>=2) return x;
        const float p=previous_[channel];previous_[channel]=x;
        if (d==0) return x;
        const float blend=d<0.25f ? 4.0f*d : 1.0f;
        const float shaped=0.25f*transfer(p,d,flavor)+0.5f*transfer(0.5f*(p+x),d,flavor)+0.25f*transfer(x,d,flavor);
        return (1.0f-blend)*transfer(x,d,flavor)+blend*shaped;
    }
private:
    float previous_[2]{};
};

// Phenomenological two-stage valve-like colour, not a solved 12AX7 circuit.
// Unequal positive/negative headroom represents bias. Each static stage has
// derivative <= its gain; AC coupling is normalized to peak gain <=1. Sag only
// reduces gain. The compensation is a conservative bound, not a measured valve.
class TubeColor {
public:
    void prepare(float sampleRate) noexcept {
        const float fs=detail::bound(sampleRate,8000.0f,384000.0f,48000.0f);
        constexpr float pi=3.14159265358979323846f;
        couplingPole_=std::exp(-pi*25.0f/fs); // 25 Hz at the internal 2x rate.
        dcPole_=std::exp(-pi*15.0f/fs);
        attackPole_=std::exp(-1.0f/(0.030f*fs));
        releasePole_=std::exp(-1.0f/(0.180f*fs));
    }
    void reset() noexcept { for (auto& state:state_) state=Channel{}; }
    static float compensationFeedbackGain(float drive) noexcept {
        const float d=detail::bound(drive,0.0f,1.0f,0.0f);
        const float blend=d<0.25f ? 4.0f*d : 1.0f;
        return (1.0f-blend)+blend*(1.0f+3.0f*d)*(1.0f+2.0f*d);
    }
    float process(float input,unsigned channel,float drive) noexcept {
        const float x=detail::sample(input),d=detail::bound(drive,0.0f,1.0f,0.0f);
        if (channel>=2) return x;
        Channel& s=state_[channel];
        if (d==0) { s=Channel{};s.previous=x;return x; }
        const float magnitude=std::fabs(x);
        const float level=magnitude/(1.0f+magnitude);
        const float pole=level>s.envelope ? attackPole_ : releasePole_;
        s.envelope=level+pole*(s.envelope-level);
        const float sag=1.0f/(1.0f+0.75f*d*s.envelope);
        const float middle=0.5f*(s.previous+x);
        const float a=subsample(middle,d,sag,s);
        const float b=subsample(x,d,sag,s);
        const float filtered=0.25f*s.previousOutput+0.5f*a+0.25f*b;
        s.previous=x;s.previousOutput=b;
        const float blend=d<0.25f ? 4.0f*d : 1.0f;
        return detail::sample((1.0f-blend)*x+blend*filtered);
    }
private:
    struct Channel { float previous=0,previousOutput=0,envelope=0,couplingInput=0,couplingOutput=0,dcInput=0,dcOutput=0; };
    static float biased(float x,float bias) noexcept {
        const float coefficient=x>=0 ? 1.0f+bias : 1.0f-bias;
        return x/(1.0f+coefficient*std::fabs(x));
    }
    float subsample(float input,float drive,float sag,Channel& s) const noexcept {
        const float first=biased((1.0f+3.0f*drive)*sag*input,0.65f*drive);
        const float coupled=0.5f*(1.0f+couplingPole_)*(first-s.couplingInput)+couplingPole_*s.couplingOutput;
        s.couplingInput=first;s.couplingOutput=coupled;
        const float second=biased((1.0f+2.0f*drive)*sag*coupled,0.35f*drive);
        const float output=0.5f*(1.0f+dcPole_)*(second-s.dcInput)+dcPole_*s.dcOutput;
        s.dcInput=second;s.dcOutput=output;
        return output;
    }
    float couplingPole_=0.9983651f,dcPole_=0.9990187f;
    float attackPole_=0.9993058f,releasePole_=0.9998843f;
    Channel state_[2]{};
};

// Musical regeneration acts on the feedback contribution, already multiplied
// by feedback and colour compensation, BEFORE adding new input. At FB>=1.1 the
// return is bounded to +/-1.25, so normalized fresh input cannot reach +/-4.
// A slow detector controls nearly uniform gain across each waveform cycle;
// the soft curve is a transient backstop, not the sustained oscillation shaper.
// The 1..1.1 transition blends into this process; FB<=1 is exact identity.
class Regenerator {
public:
    void prepare(float sampleRate) noexcept {
        const float fs=detail::bound(sampleRate,8000.0f,384000.0f,48000.0f);
        constexpr float twoPi=6.2831853071795864769f;
        dcPole_=std::exp(-twoPi*5.0f/fs);
        const float cutoff=6000.0f<0.45f*fs ? 6000.0f : 0.45f*fs;
        lowPole_=std::exp(-twoPi*cutoff/fs);
        attackPole_=std::exp(-1.0f/(0.010f*fs));
        releasePole_=std::exp(-1.0f/(0.150f*fs));
    }
    void reset() noexcept { for(auto& s:state_) s=Channel{}; }
    static float transfer(float input) noexcept {
        const float x=detail::sample(input),magnitude=std::fabs(x);
        if (magnitude<=0.95f) return x;
        if (magnitude>=1.85f) return x>0 ? 1.25f : -1.25f;
        const float u=(magnitude-0.95f)/0.9f;
        const float y=0.95f+0.9f*(u-u*u+u*u*u/3.0f);
        return x>0 ? y : -y;
    }
    float process(float input,unsigned channel,float feedback) noexcept {
        const float x=detail::sample(input),fb=detail::bound(feedback,0.0f,2.0f,0.0f);
        if (channel>=2) return x;
        Channel& s=state_[channel];
        if (fb<=1.0f) { s=Channel{};s.dcInput=x;return x; }
        const float high=detail::sample(0.5f*(1.0f+dcPole_)*(x-s.dcInput)+dcPole_*s.dcOutput);
        s.dcInput=x;s.dcOutput=high;
        const float level=std::fabs(high),pole=level>s.envelope ? attackPole_ : releasePole_;
        s.envelope=level+pole*(s.envelope-level);
        constexpr float target=0.8f,start=0.8f*target,end=1.2f*target;
        float gain=1.0f;
        if (s.envelope>=end) gain=target/s.envelope;
        else if (s.envelope>start) {
            const float distance=s.envelope-start;
            gain=(s.envelope-distance*distance/(0.8f*target))/s.envelope;
        }
        const float regulated=high*gain;
        const float shaped=0.25f*transfer(s.previous)+0.5f*transfer(0.5f*(s.previous+regulated))+0.25f*transfer(regulated);
        s.previous=regulated;s.low+=(1.0f-lowPole_)*(shaped-s.low);
        const float blend=fb>=1.1f ? 1.0f : 10.0f*(fb-1.0f);
        return detail::sample((1.0f-blend)*x+blend*s.low);
    }
private:
    struct Channel { float dcInput=0,dcOutput=0,previous=0,low=0,envelope=0; };
    float dcPole_=0.9993457f,lowPole_=0.4559381f;
    float attackPole_=0.9979188f,releasePole_=0.9998611f;
    Channel state_[2]{};
};

} } // namespace echolab::color
