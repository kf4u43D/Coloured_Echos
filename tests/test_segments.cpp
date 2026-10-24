#include "dsp/segment_models.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
using echolab::segments::LoopState;
using echolab::segments::ReverseState;
bool audioGuard = false;
std::size_t audioAllocations = 0;
void check(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}
void near(float actual, float expected, const char* text) {
    check(std::isfinite(actual) && std::fabs(actual-expected) <= 2.0e-6f,text);
}
template<std::size_t Stride> struct GuardedMemory {
    std::array<float,2*Stride+2> storage;
    GuardedMemory() { storage.fill(137.0f); }
    float* data() { return storage.data()+1; }
    void canaries() const {
        check(storage.front()==137.0f && storage.back()==137.0f,"segment memory boundary canaries");
    }
};

void reverseKnownSequenceAndFeedback() {
    GuardedMemory<12> memory;
    ReverseState reverse;
    constexpr float expected[] = {0,0,0,0,4,3,2,1,8,7,6,5};
    for (int n=0;n<12;++n) {
        float l,r;
        reverse.process(float(n+1),float(-10*(n+1)),0,memory.data(),12,4,0,l,r);
        near(l,expected[n],"reverse captures one complete chunk before reversing the known sequence");
        near(r,-10*expected[n],"reverse stereo banks remain independent");
    }
    for (std::size_t c : {std::size_t(0),std::size_t(12)})
        for (std::size_t i : {std::size_t(4),std::size_t(5),std::size_t(10),std::size_t(11)})
            near(memory.data()[c+i],137,"reverse leaves per-channel guards untouched");
    memory.canaries();

    reverse.reset();
    constexpr float input[] = {1,2,0,0,0,0};
    constexpr float feedbackWet[] = {0,0,2,1,0.5f,1};
    for (int n=0;n<6;++n) {
        float l,r;
        reverse.process(input[n],-input[n],0.5f,memory.data(),12,2,0,l,r);
        near(l,feedbackWet[n],"reverse feedback stores input plus the emitted reverse wet sample");
        near(r,-feedbackWet[n],"reverse feedback is independent per channel");
    }
    memory.canaries();
}
void feedbackFilterPlacement() {
    GuardedMemory<12> memory;ReverseState reverse;
    const float excitation[]={1,2,0,0,0,0};
    const float expected[]={0,0,2,1,.125f,.25f};
    const auto filter=[](unsigned,float value) noexcept {return .25f*value;};
    for (unsigned n=0;n<6;++n) {
        float l,r;reverse.process(excitation[n],-excitation[n],.5f,memory.data(),12,2,0,l,r,filter);
        near(l,expected[n],"reverse feedback filtering preserves its first captured reverse pass");
        near(r,-expected[n],"reverse filtering touches feedback only and retains independent right memory");
    }
    LoopState loop;unsigned filterCalls=0;
    const auto loopFilter=[&](unsigned,float value) noexcept {++filterCalls;return .25f*value;};
    float l,r;
    loop.process(1,-1,1,.5f,memory.data(),12,2,l,r,loopFilter);
    loop.process(2,-2,1,.5f,memory.data(),12,2,l,r,loopFilter);
    loop.process(0,0,2,.5f,memory.data(),12,2,l,r,loopFilter);near(l,1,"loop playback bypasses feedback filtering");
    loop.process(0,0,2,.5f,memory.data(),12,2,l,r,loopFilter);near(l,2,"loop playback retains its original recorded audio");
    check(filterCalls == 0,"loop Record and Play never update feedback colour state");
    loop.process(0,0,3,.5f,memory.data(),12,2,l,r,loopFilter);near(l,1,"first overdub emits the unfiltered old loop");
    loop.process(0,0,3,.5f,memory.data(),12,2,l,r,loopFilter);near(l,2,"overdub filters only material stored for the next cycle");
    loop.process(0,0,2,.5f,memory.data(),12,2,l,r,loopFilter);near(l,.125f,"next loop cycle contains feedback-filtered overdub");
    loop.process(0,0,2,.5f,memory.data(),12,2,l,r,loopFilter);near(l,.25f,"overdub feedback gain follows the feedback filter");
    check(filterCalls == 4,"loop colour executes exactly once per channel only while overdubbing");
    memory.canaries();
}

void reverseFadeAndInvalidation() {
    GuardedMemory<20> memory;
    ReverseState reverse;
    for(int n=0;n<8;++n) {
        float l,r;reverse.process(1,1,0,memory.data(),20,8,2,l,r);
        near(l,0,"first reverse capture never reads arbitrary old storage");
    }
    constexpr float envelope[] = {0,0.5f,1,1,1,1,0.5f,0};
    for(int n=0;n<8;++n) {
        float l,r;reverse.process(0,0,0,memory.data(),20,8,2,l,r);
        near(l,envelope[n],"reverse fade starts and ends each playback chunk at zero");
        near(r,envelope[n],"reverse fade applies equally to both channels");
    }
    // A request inside a chunk must finish the current interval. At the next
    // boundary the previous full capture remains audible, resampled to duration.
    reverse.reset();
    float l,r;
    constexpr float changedExpected[]={0,0,0,0,4,3,2,1,8,5,10,9,12,11.75f,11.5f,11.25f,11};
    for(int n=0;n<17;++n) {
        const std::size_t desired=n<5 ? 4 : (n<12 ? 2 : 5);
        reverse.process(float(n+1),-10.0f*float(n+1),0,memory.data(),20,desired,0,l,r);
        near(l,changedExpected[n],"reverse duration changes latch at a boundary and retain the previous capture");
        near(r,-10.0f*changedExpected[n],"reverse resampling preserves independent right-channel capture");
    }
    const auto beforeReset=memory.storage;
    reverse.reset();
    check(memory.storage==beforeReset,"reverse reset invalidates without clearing caller storage");
    reverse.process(1,1,0,memory.data(),20,2,0,l,r);
    near(l,0,"reverse reset cannot expose stale previous capture");
    GuardedMemory<20> replacement;
    reverse.process(1,1,0,replacement.data(),20,2,0,l,r);
    near(l,0,"reverse backing-memory change requires a fresh capture");
    memory.canaries();replacement.canaries();
}

void loopRecordAndAutoplay() {
    GuardedMemory<8> memory;
    LoopState loop;
    for(int n=0;n<3;++n) {
        float l,r;
        // Desired time changes during recording do not resize that recording.
        loop.process(float(n+1),float(-n-1),1,0.5f,memory.data(),8,n==0 ? 3 : 1,l,r);
        near(l,0,"loop recording is wet-silent until a complete buffer is captured");
        near(r,0,"loop record right wet is silent");
    }
    for(int n=0;n<9;++n) {
        float l,r;
        loop.process(99,99,1,0.5f,memory.data(),8,8,l,r);
        near(l,float(n%3+1),"held Record automatically plays instead of restarting or overwriting");
        near(r,-float(n%3+1),"loop preserves recorded stereo values");
    }
    float l,r;
    loop.process(0,0,0,0.5f,memory.data(),8,1,l,r);
    // Re-entering Record starts a new complete loop with the new desired length.
    loop.process(8,-8,1,0.5f,memory.data(),8,2,l,r);near(l,0,"record transition restarts capture once");
    loop.process(9,-9,1,0.5f,memory.data(),8,2,l,r);near(l,0,"second recording completes silently");
    loop.process(99,99,1,0.5f,memory.data(),8,2,l,r);near(l,8,"new recording replaces old loop length and content");
    loop.process(99,99,1,0.5f,memory.data(),8,2,l,r);near(l,9,"new recording auto-plays its complete second sample");
    memory.canaries();
}

void loopStopResumeAndOverdub() {
    GuardedMemory<8> memory;
    LoopState loop;
    float l,r;
    loop.process(1,-1,1,0.5f,memory.data(),8,3,l,r);
    loop.process(2,-2,1,0.5f,memory.data(),8,3,l,r);
    loop.process(3,-3,1,0.5f,memory.data(),8,3,l,r);
    loop.process(0,0,2,0.5f,memory.data(),8,3,l,r);near(l,1,"Play begins at captured loop start");
    const auto beforeStop=memory.storage;
    for(int n=0;n<5;++n) {
        loop.process(99,99,0,0.5f,memory.data(),8,3,l,r);
        near(l,0,"Stop is silent");near(r,0,"Stop right is silent");
    }
    check(memory.storage==beforeStop,"Stop makes no storage writes");
    loop.process(0,0,2,0.5f,memory.data(),8,3,l,r);near(l,2,"Stop/Play resumes its frozen cursor");
    loop.process(0,0,2,0.5f,memory.data(),8,3,l,r);near(l,3,"resumed Play reaches recorded end");
    loop.process(0,0,2,0.5f,memory.data(),8,3,l,r);near(l,1,"Play wraps at captured length");

    loop.reset();
    loop.process(1,-1,1,0.5f,memory.data(),8,2,l,r);
    loop.process(2,-2,1,0.5f,memory.data(),8,2,l,r);
    loop.process(10,-10,3,0.5f,memory.data(),8,2,l,r);
    near(l,1,"Overdub emits the old left sample before updating");near(r,-1,"Overdub emits old right sample");
    loop.process(20,-20,3,0.5f,memory.data(),8,2,l,r);near(l,2,"Overdub advances through existing loop");
    loop.process(0,0,2,0.5f,memory.data(),8,2,l,r);
    near(l,10.5f,"overdub retention plus new input is audible on the next pass");near(r,-10.5f,"overdub stereo storage is independent");
    loop.process(0,0,2,0.5f,memory.data(),8,2,l,r);near(l,21,"overdub equation applies to each loop sample");
    memory.canaries();
}

void loopClearPartialCaptureAndMemoryChange() {
    GuardedMemory<8> memory;
    LoopState loop;
    float l,r;
    loop.process(1,2,1,0.5f,memory.data(),8,4,l,r);
    loop.process(3,4,1,0.5f,memory.data(),8,4,l,r);
    loop.process(0,0,2,0.5f,memory.data(),8,4,l,r);
    near(l,0,"incomplete recording cannot expose uncaptured memory on Play");
    loop.process(0,0,3,0.5f,memory.data(),8,4,l,r);
    near(l,0,"incomplete recording cannot expose uncaptured memory on Overdub");
    loop.process(7,8,1,0.5f,memory.data(),8,1,l,r);
    loop.process(0,0,2,0.5f,memory.data(),8,1,l,r);near(l,7,"fresh complete capture becomes playable");
    const auto beforeClear=memory.storage;
    loop.process(999,999,4,0.5f,memory.data(),8,1,l,r);
    near(l,0,"Clear is silent");
    check(memory.storage==beforeClear,"Clear invalidates logically without reading or writing audio storage");
    for(int action : {2,3,4,0}) {
        loop.process(999,999,action,0.5f,memory.data(),8,1,l,r);
        near(l,0,"cleared loop remains silent until explicitly recorded");near(r,0,"cleared right loop remains silent");
        check(memory.storage==beforeClear,"invalid-loop Play/Overdub/Clear/Stop does not touch old data");
    }
    loop.process(5,6,1,0.5f,memory.data(),8,1,l,r);
    const auto beforeReset=memory.storage;
    loop.reset();
    check(memory.storage==beforeReset,"loop reset does not clear caller memory");
    loop.process(0,0,2,0.5f,memory.data(),8,1,l,r);near(l,0,"reset invalidates previously recorded audio");
    GuardedMemory<8> other;
    loop.process(0,0,2,0.5f,other.data(),8,1,l,r);near(l,0,"changed memory cannot inherit old validity");
    memory.canaries();other.canaries();
}

void boundsFiniteAndAllocations() {
    GuardedMemory<4> memory;
    ReverseState reverse;
    LoopState loop;
    float l,r;
    reverse.process(7,-7,0,memory.data(),4,0,0,l,r);near(l,0,"zero reverse length becomes one frame");
    reverse.process(8,-8,0,memory.data(),4,0,0,l,r);near(l,7,"one-frame reverse remains causal");
    reverse.reset();
    reverse.process(2,-2,0,memory.data(),4,std::numeric_limits<std::size_t>::max(),0,l,r);near(l,0,"reverse length clamps to two-bank capacity");
    reverse.process(3,-3,0,memory.data(),4,std::numeric_limits<std::size_t>::max(),0,l,r);near(l,0,"bounded reverse completes its actual capture");
    reverse.process(4,-4,0,memory.data(),4,std::numeric_limits<std::size_t>::max(),0,l,r);near(l,3,"bounded reverse indexes only initialized samples");
    reverse.reset();
    reverse.process(2,-2,0,memory.data(),4,2,999,l,r);
    reverse.process(3,-3,0,memory.data(),4,2,999,l,r);
    reverse.process(4,-4,0,memory.data(),4,2,999,l,r);
    near(l,3,"short reverse chunks retain audible samples when requested fades exceed their length");
    loop.process(6,-6,1,0,memory.data(),4,0,l,r);near(l,0,"zero record length becomes one frame");
    loop.process(99,99,1,0,memory.data(),4,0,l,r);near(l,6,"one-frame loop auto-plays");
    loop.reset();
    for(int n=0;n<4;++n) {
        loop.process(float(n+1),0,1,0,memory.data(),4,std::numeric_limits<std::size_t>::max(),l,r);
        near(l,0,"oversized recording clamps to capacity without early stale playback");
    }
    for(int n=0;n<4;++n) {
        loop.process(99,99,1,0,memory.data(),4,0,l,r);
        near(l,float(n+1),"capacity-sized loop wraps without touching canaries");
    }
    const auto beforeInvalid=memory.storage;
    reverse.process(1,1,0,memory.data(),1,1,0,l,r);near(l,0,"reverse rejects insufficient bank capacity");
    reverse.process(1,1,0,nullptr,4,1,0,l,r);near(l,0,"reverse rejects null storage");
    reverse.process(1,1,0,memory.data(),std::numeric_limits<std::size_t>::max(),1,0,l,r);
    near(l,0,"reverse rejects overflowing byte counts before pointer arithmetic");
    loop.process(1,1,1,0,memory.data(),0,1,l,r);near(l,0,"loop rejects zero stride");
    loop.process(1,1,1,0,nullptr,4,1,l,r);near(l,0,"loop rejects null storage");
    loop.process(1,1,1,0,memory.data(),std::numeric_limits<std::size_t>::max(),1,l,r);
    near(l,0,"loop rejects overflowing byte counts before pointer arithmetic");
    check(memory.storage==beforeInvalid,"invalid storage calls perform no audio writes");

    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    reverse.reset();loop.reset();
    audioGuard=true;
    for(int n=0;n<1000;++n) {
        reverse.process(n%2 ? inf : 2.0f,n%3 ? nan : -2.0f,nan,memory.data(),4,2,999,l,r);
        if (!std::isfinite(l) || !std::isfinite(r)) { audioGuard=false;check(false,"reverse remains finite with invalid inputs and large fade"); }
        loop.process(n%2 ? inf : 2.0f,n%3 ? nan : -2.0f,n<2 ? 1 : 3,inf,memory.data(),4,2,l,r);
        if (!std::isfinite(l) || !std::isfinite(r)) { audioGuard=false;check(false,"loop remains finite with invalid inputs and feedback"); }
    }
    reverse.reset();loop.reset();
    audioGuard=false;
    check(audioAllocations==0,"segment process/reset paths perform no heap allocation");
    memory.canaries();
}
} // namespace

void* operator new(std::size_t count) {
    if(audioGuard) ++audioAllocations;
    if(void* p=std::malloc(count ? count : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t count) { return ::operator new(count); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

int main() {
    try {
        reverseKnownSequenceAndFeedback();reverseFadeAndInvalidation();feedbackFilterPlacement();
        loopRecordAndAutoplay();loopStopResumeAndOverdub();loopClearPartialCaptureAndMemoryChange();
        boundsFiniteAndAllocations();
        std::cout << "Synthetic segment fixtures: reverse banks/fade/feedback, loop actions, validity, bounds and allocation checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        audioGuard=false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
