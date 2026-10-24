#include "dsp/rack_models.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>

// All signals and readers in this file are synthetic_fixture, independently
// chosen to expose routing, read positions, state and quantization behaviour.
namespace {
using echolab::rack::Controls;
using echolab::rack::Frame;
using echolab::rack::Kind;
using echolab::rack::State;
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void near(float actual, float expected, float tolerance, const char* message) {
    check(std::fabs(actual - expected) <= tolerance, message);
}
Controls controls() { return {40.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.5f, 100.0f}; }
struct Requests {
    struct Item { unsigned channel; float delay; };
    std::array<Item, 8> calls{};
    unsigned count = 0;
    void add(unsigned channel, float delay) noexcept {
        if (count < calls.size()) calls[count] = {channel, delay};
        ++count;
    }
};
void digital24() {
    State state;
    Requests requests;
    auto read = [&](unsigned channel, float delay) noexcept {
        requests.add(channel, delay);
        return channel == 0 ? 0.25f : -0.5f;
    };
    const auto result = state.process(Kind::Digital24, 0.125f, -0.25f, controls(), read);
    check(requests.count == 2, "Digital24 reads one old sample per channel");
    near(requests.calls[0].delay, 40, 0, "Digital24 left time");
    near(requests.calls[1].delay, 40, 0, "Digital24 right time");
    near(result.writeL, 0.25f, 0, "Digital24 feedback stays in left channel");
    near(result.writeR, -0.5f, 0, "Digital24 feedback stays in right channel");
    near(result.wetL, 0.25f, 0, "Digital24 wet is old quantized memory");
    near(result.wetR, -0.5f, 0, "Digital24 wet right is old memory");
    const auto zero = [](unsigned, float) noexcept { return 0.0f; };
    constexpr float step = 1.0f / 8388608.0f;
    auto value = state.process(Kind::Digital24, 2.0f, -2.0f, controls(), zero);
    near(value.writeL, 1.0f - step, 0, "24-bit positive saturation stays below +1");
    near(value.writeR, -1.0f, 0, "24-bit negative saturation includes -1");
    value = state.process(Kind::Digital24, 1.75f * step, -1.75f * step, controls(), zero);
    near(value.writeL, step, 0, "24-bit explicit truncation towards zero positive");
    near(value.writeR, -step, 0, "24-bit explicit truncation towards zero negative");
    for (unsigned i = 1; i != 1000; ++i) {
        const float input = static_cast<float>(i) / 1001.0f;
        value = state.process(Kind::Digital24, input, -input, controls(), zero);
        const float gridL = value.writeL / step, gridR = value.writeR / step;
        near(gridL, static_cast<float>(static_cast<int>(gridL)), 0, "left writes lie on 24-bit grid");
        near(gridR, static_cast<float>(static_cast<int>(gridR)), 0, "right writes lie on 24-bit grid");
    }
}
void stereoRouting() {
    State state;
    Requests requests;
    auto read = [&](unsigned channel, float delay) noexcept {
        requests.add(channel, delay);
        return channel == 0 ? 0.3f : -0.4f;
    };
    auto c = controls();
    auto result = state.process(Kind::Stereo, 0.1f, -0.2f, c, read);
    check(requests.count == 2 && requests.calls[0].channel == 0 && requests.calls[1].channel == 1,
          "Stereo reads both channels independently");
    near(requests.calls[0].delay, 40, 0, "Stereo L time uses T");
    near(requests.calls[1].delay, 60, 0, "Stereo R time uses ratio*T");
    near(result.writeL, 0.25f, 1e-7f, "Stereo left feedback is independent");
    near(result.writeR, -0.4f, 1e-7f, "Stereo right feedback is independent");
    near(result.wetL, 0.3f, 0, "Stereo uncoloured wet left");
    near(result.wetR, -0.4f, 0, "Stereo uncoloured wet right");
    c.drive = 1; c.tonePole = 0.99f;
    const auto uncoloured = state.process(Kind::Stereo, 0.1f, -0.2f, c, read);
    near(uncoloured.writeL, result.writeL, 0, "Stereo ignores added drive/tone colour");
    requests.count = 0; c.stereoRatio = 100;
    state.process(Kind::Stereo, 0, 0, c, read);
    near(requests.calls[1].delay, 100, 0, "Stereo ratio clamps to reserved capacity");
}
void platterRoutingAndReset() {
    State state;
    auto c = controls(); c.tonePole = 0;
    Requests requests;
    auto read = [&](unsigned channel, float delay) noexcept {
        requests.add(channel, delay);
        if (channel == 0) return delay > 30 ? 0.4f : 0.8f;
        return delay > 30 ? -0.2f : 0.4f;
    };
    const auto result = state.process(Kind::Platter, 0.1f, 0.2f, c, read);
    check(requests.count == 4, "Platter reads two heads in each existing line");
    near(requests.calls[0].delay, 40, 0, "Platter long head at T");
    near(requests.calls[1].delay, 20, 0, "Platter short head at T/2");
    near(result.wetL, 0.5f, 1e-7f, "Platter left head weights .75/.25");
    near(result.wetR, -0.05f, 1e-7f, "Platter right head weights .75/.25");
    near(result.writeL, 0.35f, 1e-7f, "Platter feedback uses mixed heads, not longest only");
    near(result.writeR, 0.175f, 1e-7f, "Platter right feedback uses its own mixed heads");
    const auto zero = [](unsigned, float) noexcept { return 0.0f; };
    const auto residual = state.process(Kind::Platter, 0, 0, c, zero);
    check(residual.writeL != 0 || residual.writeR != 0, "Platter retains colour/DC state between frames");
    state.reset();
    const auto silence = state.process(Kind::Platter, 0, 0, c, zero);
    near(silence.writeL, 0, 0, "Platter reset clears left colour state");
    near(silence.writeR, 0, 0, "Platter reset clears right colour state");
    requests.count = 0; c.modDepthSamples = 4; c.modPhase = 0;
    state.process(Kind::Platter, 0, 0, c, read);
    near(requests.calls[0].delay, 44, 0, "Platter phase drives bounded wow/flutter long head");
    near(requests.calls[1].delay, 22, 0, "Platter short-head motion scales with path length");
    requests.count = 0; c.modPhase = 0.5f;
    state.process(Kind::Platter, 0, 0, c, read);
    near(requests.calls[0].delay, 36, 0, "Platter opposite phase moves head the other way");
    near(requests.calls[1].delay, 18, 0, "Platter opposite short-head movement");
}
void boundariesAndInstances() {
    for (const auto kind : {Kind::Digital24, Kind::Platter, Kind::Stereo}) {
        State state;
        Controls c = controls();
        c.delaySamples = -100; c.maximumSamples = 1;
        c.stereoRatio = std::numeric_limits<float>::max();
        c.modDepthSamples = std::numeric_limits<float>::max();
        c.feedback = 100; c.drive = 10; c.tonePole = 4;
        bool bounded = true;
        auto read = [&](unsigned channel, float time) noexcept {
            bounded = bounded && channel < 2 && std::isfinite(time) && time >= 1 && time <= 1;
            return channel == 0 ? 0.25f : -0.25f;
        };
        for (unsigned n = 0; n != 1000; ++n) {
            const Frame frame = state.process(kind, n % 2 ? 1.0f : -1.0f, 0, c, read);
            check(std::isfinite(frame.writeL) && std::isfinite(frame.writeR) &&
                  std::isfinite(frame.wetL) && std::isfinite(frame.wetR), "rack extreme controls remain finite");
            check(std::fabs(frame.writeL) <= 1.0e12f && std::fabs(frame.writeR) <= 1.0e12f,
                  "rack writes stay inside common numerical ceiling");
        }
        check(bounded, "all requested heads stay within prepared memory");
        c.delaySamples = c.maximumSamples = c.feedback = c.tonePole = c.drive = c.modDepthSamples =
            c.modPhase = c.stereoRatio = std::numeric_limits<float>::quiet_NaN();
        state.reset();
        const auto nanRead = [](unsigned, float) noexcept { return std::numeric_limits<float>::infinity(); };
        const auto frame = state.process(kind, 0, 0, c, nanRead);
        check(std::isfinite(frame.writeL) && std::isfinite(frame.writeR), "defensive nonfinite controls/read fallback");
    }
    State a, b;
    auto c = controls();
    const auto zero = [](unsigned, float) noexcept { return 0.0f; };
    a.process(Kind::Platter, 0.5f, -0.25f, c, zero);
    const auto silent = b.process(Kind::Platter, 0, 0, c, zero);
    near(silent.writeL, 0, 0, "Platter state is independent between instances");
    near(silent.writeR, 0, 0, "Platter right state is independent between instances");
}
void feedbackFilterPlacement() {
    for (const auto kind:{Kind::Digital24,Kind::Stereo,Kind::Platter}) {
        State state;state.prepare(48000);state.reset();
        auto c=controls();c.tonePole=0;c.drive=0;
        unsigned calls=0;
        const auto reader=[](unsigned channel,float) noexcept {return channel == 0 ? .4f : -.2f;};
        const auto filter=[&](unsigned,float value) noexcept {++calls;return .25f*value;};
        const auto output=state.process(kind,.1f,.2f,c,reader,filter);
        near(output.wetL,.4f,1e-7f,"rack feedback filter does not colour the emitted wet signal");
        near(output.wetR,-.2f,1e-7f,"rack feedback filter preserves the independent emitted right signal");
        near(output.writeL,.15f,2e-7f,"rack filter affects only the reinjected component before feedback gain");
        near(output.writeR,.175f,2e-7f,"rack fresh input bypasses the feedback filter");
        check(calls == 2,"rack applies one feedback filter update per channel and frame");
    }
}
void rackConverterClockAndGrid() {
    State state;state.prepare(48000,16000);state.reset();
    auto c=controls();c.feedback=0;
    const auto zero=[](unsigned,float) noexcept {return 0.0f;};
    const double pole=std::exp(-6.2831853071795864769*.45*16000/48000);
    double previous=0,previous2=0,previousHeld=0;
    unsigned holdChanges=0;
    for (unsigned n=0;n<600;++n) {
        const float input=.1f+.0001f*static_cast<float>(n);
        const auto f=state.process(Kind::Digital24,input,-input,c,zero);
        near(f.writeR,-f.writeL,0,"Rack reduced-rate converter uses one coherent stereo clock");
        const float code=f.writeL*8388608.0f;
        near(code,static_cast<float>(static_cast<std::int32_t>(code)),0,"reconstructed Rack writes retain the explicit24-bit grid");
        // Undo the known two reconstruction poles, independently of the four
        // anti-alias sections. A16kHz hold at48kHz changes once every3samples.
        const double held=(f.writeL-2*pole*previous+pole*pole*previous2)/((1-pole)*(1-pole));
        if (n>30) {
            if (n%3 == 0) {check(held>previousHeld+1e-4,"Rack converter captures a new anti-aliased sample at its internal clock tick");++holdChanges;}
            else check(std::fabs(held-previousHeld)<2e-6,"Rack converter holds between internal clock ticks before reconstruction");
        }
        previous2=previous;previous=f.writeL;previousHeld=held;
    }
    check(holdChanges == 189,"Rack16kHz converter performs exactlyone stereo capture per3 host samples");
    state.prepare(48000,17000);state.reset();
    const double fractionalPole=std::exp(-6.2831853071795864769*.45*17000/48000);
    previous=previous2=previousHeld=0;unsigned fractionalChanges=0;
    for (unsigned n=0;n<600;++n) {
        const auto f=state.process(Kind::Digital24,.1f+.0001f*static_cast<float>(n),0,c,zero);
        const double held=(f.writeL-2*fractionalPole*previous+fractionalPole*fractionalPole*previous2)/((1-fractionalPole)*(1-fractionalPole));
        if (n>30 && held>previousHeld+1e-4) ++fractionalChanges;
        previous2=previous;previous=f.writeL;previousHeld=held;
    }
    check(fractionalChanges>=201 && fractionalChanges<=203,"Rack uses a fractional17kHz clock rather than rounding the converter to a whole host-sample interval");
    State full,same;full.prepare(48000,0);same.prepare(48000,48000);full.reset();same.reset();
    for (unsigned n=0;n<1000;++n) {
        const float x=.3f*std::sin(.11f*n);
        const auto a=full.process(Kind::Digital24,x,-x,c,zero);
        const auto b=same.process(Kind::Digital24,x,-x,c,zero);
        near(a.writeL,b.writeL,0,"full-Fs Rack sentinel and an explicit host rate bypass the converter identically");
        const float expected=static_cast<float>(static_cast<std::int32_t>(x*8388608.0f))/8388608.0f;
        near(a.writeL,expected,0,"full-Fs Rack preserves the previous quantize-only write path bitwise");
    }
}
void rackAntiAliasAndReset() {
    const auto zero=[](unsigned,float) noexcept {return 0.0f;};
    auto c=controls();c.feedback=0;
    double gains[2]{};unsigned index=0;
    for (unsigned frequency:{1000U,12000U}) {
        State state;state.prepare(48000,16000);state.reset();
        double real=0,imag=0;
        const unsigned measured=frequency == 12000 ? 4000 : frequency;
        for (unsigned n=0;n<48000;++n) {
            const float input=.25f*std::sin(6.2831853071795864769f*static_cast<float>(frequency)*n/48000);
            const auto out=state.process(Kind::Digital24,input,0,c,zero);
            if (n>=24000) {
                const double phase=6.2831853071795864769*measured*n/48000;
                real+=out.writeL*std::cos(phase);imag-=out.writeL*std::sin(phase);
            }
        }
        gains[index++]=2*std::sqrt(real*real+imag*imag)/(24000*.25);
        state.reset();
        for (unsigned n=0;n<200;++n) {
            const auto silence=state.process(Kind::Digital24,0,0,c,zero);
            near(silence.writeL,0,0,"Rack reset clears the held, anti-alias and reconstruction histories");
            near(silence.writeR,0,0,"Rack reset restores stereo silence");
        }
    }
    check(gains[0]>.85 && gains[0]<1.01,"Rack16kHz converter preserves the useful1kHz band with unity-DC filters");
    check(gains[1]<.12,"Rack anti-alias filtering attenuates the12kHz to4kHz fold by more than18dB");
    State dc;dc.prepare(48000,16000);dc.reset();Frame out{};
    for (unsigned n=0;n<5000;++n) out=dc.process(Kind::Digital24,.25f,-.125f,c,zero);
    near(out.writeL,.25f,2e-6f,"Rack converter and reconstruction maintain unity DC gain");
    near(out.writeR,-.125f,2e-6f,"Rack converter preserves its independent right DC level");
}
} // namespace

int main() {
    digital24(); stereoRouting(); platterRoutingAndReset(); boundariesAndInstances();feedbackFilterPlacement();
    rackConverterClockAndGrid();rackAntiAliasAndReset();
    if (failures) { std::cerr << failures << " rack synthetic_fixture failures\n"; return 1; }
    std::cout << "rack synthetic_fixture: quantizer/routing/heads/reset/bounds/instances passed\n";
    return 0;
}
