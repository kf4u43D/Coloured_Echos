#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace echolab_nt {

// Time is counted in rendered samples. MIDI callbacks have no intra-block
// timestamp in the SDK callbacks; their timestamp is the current render counter.
class ClockSync {
public:
    void configure(float sampleRate, int mode, int pulsesPerQuarter, float bpm, int division) noexcept {
        const auto rate=static_cast<std::uint32_t>(std::isfinite(sampleRate) && sampleRate>=8000 && sampleRate<=384000 ? sampleRate : 48000);
        const int ppqn=pulsesPerQuarter==1 || pulsesPerQuarter==2 || pulsesPerQuarter==4 || pulsesPerQuarter==24 ? pulsesPerQuarter : 4;
        mode=std::max(0,std::min(3,mode));division=std::max(0,std::min(9,division));
        bpm=std::isfinite(bpm) ? std::max(30.0f,std::min(300.0f,bpm)) : 120.0f;
        if(rate!=sampleRate_ || mode!=mode_ || ppqn!=cvPpqn_) {
            midi_.invalidate();cv_.invalidate();cvHigh_=false;
        }
        if(rate!=sampleRate_ || mode!=mode_ || ppqn!=cvPpqn_ || bpm!=bpm_ || division!=division_) ++revision_;
        sampleRate_=rate;mode_=mode;cvPpqn_=ppqn;bpm_=bpm;division_=division;
    }
    void midiRealtime(std::uint8_t byte) noexcept {
        if(byte==0xfa || byte==0xfb) { midiRunning_=true;midi_.invalidate();++revision_; }
        else if(byte==0xfc) { midiRunning_=false;midi_.invalidate();++revision_; }
        else if(byte==0xf8 && midiRunning_ && mode_==1) pulse(midi_,24);
    }
    void cvSample(float volts) noexcept {
        if(mode_!=2) return;
        if(!std::isfinite(volts)) volts=0;
        if(cvHigh_) { if(volts<=1.0f) cvHigh_=false; }
        else if(volts>=2.0f) { cvHigh_=true;pulse(cv_,cvPpqn_); }
    }
    void clearCv() noexcept { cv_.invalidate();cvHigh_=false;++revision_; }
    void advanceSample() noexcept {
        ++counter_;
        expire(midi_);expire(cv_);
    }
    bool locked() const noexcept {
        return mode_==3 || (mode_==1 && midiRunning_ && midi_.valid) || (mode_==2 && cv_.valid);
    }
    float delayMs(float manualTime) const noexcept {
        if(mode_==0 || !locked()) return manualTime;
        const double quarter=mode_==3 ? 60.0/static_cast<double>(bpm_) :
            static_cast<double>(mode_==1 ? midi_.quarterFrames : cv_.quarterFrames)/sampleRate_;
        return static_cast<float>(quarter*1000.0*quarterMultiplier(division_));
    }
    float lastQuarterMs() const noexcept {
        if(mode_==0 || mode_==3) return 60000.0f/bpm_;
        const float frames=mode_==1 ? midi_.quarterFrames : cv_.quarterFrames;
        return frames>0 ? 1000.0f*frames/sampleRate_ : 60000.0f/bpm_;
    }
    std::uint64_t frame() const noexcept { return counter_; }
    std::uint32_t revision() const noexcept { return revision_; }
    int mode() const noexcept { return mode_; }
    static double quarterMultiplier(int division) noexcept {
        constexpr double ratios[]={4.0,2.0,1.0,0.5,0.25,1.5,0.75,2.0/3.0,1.0/3.0,1.0/6.0};
        return ratios[std::max(0,std::min(9,division))];
    }
private:
    struct Tracker {
        std::uint64_t last=0;
        std::uint32_t sum=0,intervals[24]{};
        unsigned count=0,index=0;
        float quarterFrames=0;
        bool anchored=false,valid=false;
        void invalidate() noexcept { sum=0;count=index=0;anchored=valid=false; }
    };
    void pulse(Tracker& tracker,int ppqn) noexcept {
        if(!tracker.anchored) { tracker.last=counter_;tracker.anchored=true;return; }
        const auto delta=counter_-tracker.last;
        if(delta==0) return; // No invented timestamps for callbacks batched at the same boundary.
        tracker.last=counter_;
        if(delta>std::uint64_t(sampleRate_)*2) {
            tracker.invalidate();tracker.last=counter_;tracker.anchored=true;++revision_;return;
        }
        const unsigned window=static_cast<unsigned>(ppqn);
        if(tracker.count==window) tracker.sum-=tracker.intervals[tracker.index];else ++tracker.count;
        const auto interval=static_cast<std::uint32_t>(delta);
        tracker.intervals[tracker.index]=interval;tracker.sum+=interval;
        tracker.index=(tracker.index+1)%window;
        const float quarter=std::max(sampleRate_*0.2f,std::min(sampleRate_*2.0f,
            static_cast<float>(static_cast<double>(tracker.sum)*ppqn/tracker.count)));
        if(!tracker.valid || quarter!=tracker.quarterFrames) ++revision_;
        tracker.quarterFrames=quarter;tracker.valid=true;
    }
    void expire(Tracker& tracker) noexcept {
        if(tracker.anchored && counter_-tracker.last>std::uint64_t(sampleRate_)*2) {
            tracker.invalidate();++revision_;
        }
    }
    Tracker midi_{},cv_{};
    std::uint64_t counter_=0;
    std::uint32_t sampleRate_=48000,revision_=1;
    float bpm_=120;
    int mode_=3,cvPpqn_=4,division_=2;
    bool cvHigh_=false,midiRunning_=true;
};

} // namespace echolab_nt
