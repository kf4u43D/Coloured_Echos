#include "nt/clock_sync.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using echolab_nt::ClockSync;
void check(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
void near(float actual,float expected,float tolerance,const char* text) {
    check(std::isfinite(actual) && std::fabs(actual-expected)<=tolerance,text);
}
void advance(ClockSync& clock,std::uint64_t frames) { for(std::uint64_t i=0;i<frames;++i) clock.advanceSample(); }
void midiTransportAndTimeout() {
    ClockSync clock;clock.configure(48000,1,4,120,2);
    near(clock.delayMs(350),350,0,"MIDI waits for timing before replacing manual Time");
    clock.midiRealtime(0xfa);clock.midiRealtime(0xf8);
    for(int i=0;i<48;++i) { advance(clock,1000);clock.midiRealtime(0xf8); }
    check(clock.locked(),"MIDI 24PPQN locks");near(clock.delayMs(350),500,1e-4f,"24 MIDI pulses form a 120 BPM quarter");
    const auto frame=clock.frame();clock.midiRealtime(0xf8);
    check(clock.frame()==frame,"MIDI callbacks do not invent sample offsets");
    near(clock.delayMs(350),500,1e-4f,"same-boundary duplicate clock does not invent zero period");
    clock.midiRealtime(0xfc);check(!clock.locked(),"MIDI Stop falls back immediately");near(clock.delayMs(350),350,0,"Stop returns manual Time");
    clock.midiRealtime(0xfb);clock.midiRealtime(0xf8);advance(clock,2000);clock.midiRealtime(0xf8);
    near(clock.delayMs(350),1000,1e-4f,"Continue relocks from new MIDI intervals");
    const float last=clock.lastQuarterMs();advance(clock,96001);
    check(!clock.locked(),"MIDI expires after two seconds of rendered silence");near(clock.delayMs(350),350,0,"timeout uses manual Time");
    near(clock.lastQuarterMs(),last,0,"last tempo remains observable after timeout");
    clock.midiRealtime(0xf8);advance(clock,1000);clock.midiRealtime(0xf8);
    near(clock.delayMs(350),500,1e-4f,"MIDI relocks without including the long timeout gap");
}
void cvSchmittAndJitter() {
    ClockSync clock;clock.configure(48000,2,4,120,2);
    clock.cvSample(2.0f);advance(clock,6000);
    clock.cvSample(1.5f);clock.cvSample(2.5f);
    check(!clock.locked(),"Schmitt middle band cannot retrigger a held high clock");
    clock.cvSample(1.0f);clock.cvSample(2.0f);
    near(clock.delayMs(350),500,1e-4f,"CV four pulses per quarter gives 120 BPM");
    for(int interval : {5990,6010,5980,6020}) {
        clock.cvSample(0);advance(clock,static_cast<std::uint64_t>(interval));clock.cvSample(5);
    }
    near(clock.delayMs(350),500,1e-4f,"quarter-sized interval window cancels balanced CV jitter");
    clock.cvSample(std::numeric_limits<float>::quiet_NaN());
    advance(clock,96001);check(!clock.locked(),"CV timeout returns to free Time");
    clock.cvSample(5);clock.cvSample(0);advance(clock,12000);clock.cvSample(5);
    near(clock.delayMs(350),1000,1e-4f,"CV relocks from fresh edges after timeout");
    clock.clearCv();check(!clock.locked(),"clock bus changes invalidate old CV timing");
}
float cvPartition(float fs,int block) {
    ClockSync clock;clock.configure(fs,2,2,120,6);
    const int interval=static_cast<int>(fs/4);
    const int frames=interval*8+1;
    for(int start=0;start<frames;start+=block) for(int i=start;i<std::min(frames,start+block);++i) {
        clock.cvSample(i%interval==0 ? 5.0f : 0.0f);clock.advanceSample();
    }
    check(clock.locked(),"CV synthetic fixture acquires lock across block sizes");return clock.delayMs(350);
}
void ratesDivisionsAndBlocks() {
    for(float fs : {32000.0f,44100.0f,48000.0f,96000.0f}) for(int block : {1,4,20,24,128})
        near(cvPartition(fs,block),375,0.01f,"per-sample CV clock is Fs/block invariant (dotted eighth)");
    ClockSync clock;
    for(int division=0;division<10;++division) {
        clock.configure(48000,3,4,120,division);
        near(clock.delayMs(350),static_cast<float>(500*ClockSync::quarterMultiplier(division)),1e-4f,
             "internal BPM dotted/triplet division conversion");
    }
    clock.configure(48000,0,4,120,2);near(clock.delayMs(123),123,0,"free mode preserves manual Time");
    clock.configure(std::numeric_limits<float>::infinity(),99,99,std::numeric_limits<float>::quiet_NaN(),99);
    check(std::isfinite(clock.delayMs(350)),"invalid clock settings are bounded");
}
void internalTempoAfterExternalClock() {
    ClockSync clock;clock.configure(48000,2,1,120,2);
    clock.cvSample(5);clock.cvSample(0);advance(clock,48000);clock.cvSample(5);
    near(clock.lastQuarterMs(),1000,1e-4f,"CV fixture locks to a one-second quarter note");
    clock.configure(48000,3,1,120,2);
    near(clock.lastQuarterMs(),500,1e-4f,"Internal BPM quarter ignores the previous CV tempo");
    near(clock.delayMs(731),500,1e-4f,"internal delay and reported quarter agree after CV");
    clock.configure(48000,0,1,90,2);
    near(clock.lastQuarterMs(),60000.0f/90,1e-4f,"Free mode reports its configured internal tempo rather than stale CV");
}
} // namespace
int main() {
    try { midiTransportAndTimeout();cvSchmittAndJitter();ratesDivisionsAndBlocks();internalTempoAfterExternalClock();
        std::cout << "Clock synthetic fixtures: MIDI transport/24PPQN, CV Schmitt/jitter, timeout, Fs/divisions/block invariance passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
