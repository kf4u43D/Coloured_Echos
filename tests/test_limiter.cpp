#include "dsp/output_limiter.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using echolab::safety::Limiter;
using echolab::safety::maximumCeiling;
void check(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
void near(float actual,float expected,float tolerance,const char* text) {
    check(std::isfinite(actual) && std::fabs(actual-expected)<=tolerance,text);
}
void unityKneeAndStereo() {
    Limiter limiter;
    limiter.setCeiling(.8f);
    float left=.5f,right=-.25f;
    limiter.process(left,right);
    near(left,.5f,0,"quiet reset signal retains exact unity gain");
    near(right,-.25f,0,"quiet stereo ratio and polarity survive");
    limiter.reset();left=.8f;right=-.4f;limiter.process(left,right);
    near(left,.76f,2e-7f,"knee begins reducing smoothly before the hard ceiling");
    near(right,-.38f,2e-7f,"knee uses a linked stereo gain");
    float previous=0;
    for(int i=0;i<=1600;++i) {
        limiter.reset();left=float(i)*.001f;right=left*-.25f;
        limiter.process(left,right);
        check(left+2e-7f>=previous && left<=.8f,"static knee is monotonic and bounded");
        near(right,-.25f*left,1e-7f,"stereo ratio stays linked throughout knee and overload");
        previous=left;
    }
    limiter.reset();left=.639f;right=0;limiter.process(left,right);const float below=left;
    limiter.reset();left=.641f;right=0;limiter.process(left,right);const float above=left;
    near((above-below)/.002f,1,0.005f,"knee joins unity without a slope jump");
    limiter.reset();left=.959f;right=0;limiter.process(left,right);const float endBelow=left;
    limiter.reset();left=.961f;right=0;limiter.process(left,right);
    check(std::fabs(left-endBelow)<1e-5f,"knee reaches the ceiling without a hard corner");
}
void twoTimeRelease() {
    // synthetic_fixture: an overload requests gain .25. After it, a quiet
    // probe follows 65% of a 30ms recovery and 35% of a 150ms recovery.
    for(int fs : {8000,44100,48000,96000}) {
        Limiter limiter;limiter.prepare(float(fs),150);limiter.setCeiling(.8f);
        float left=3.2f,right=-1.6f;limiter.process(left,right);
        near(limiter.gain(),.25f,1e-7f,"first overload is caught in the current sample");
        near(left,.8f,1e-7f,"overload is bounded without lookahead");
        near(right,-.4f,1e-7f,"first overload preserves stereo image");
        float previous=limiter.gain();
        const int samples=fs*3/20;
        for(int n=1;n<=samples;++n) {
            left=.1f;right=-.05f;limiter.process(left,right);
            check(limiter.gain()>=previous,"quiet release is monotonic");previous=limiter.gain();
            if(n==fs*3/100 || n==samples) {
                const double seconds=double(n)/fs;
                const double expected=1-.75*(.65*std::exp(-seconds/.03)+.35*std::exp(-seconds/.15));
                near(limiter.gain(),float(expected),2.5e-4f,"two physical recovery times are invariant with sample rate");
            }
        }
        const float before=limiter.gain();
        limiter.prepare(float(fs),300);near(limiter.gain(),before,0,"preparing release does not restart its envelope");
        limiter.releaseIdle();check(limiter.gain()>before,"unused postmix stage keeps releasing");
    }
    Limiter longRelease;longRelease.prepare(384000,5000);longRelease.setCeiling(.8f);
    float left=3.2f,right=0;longRelease.process(left,right);
    for(int n=0;n<384000*60;++n) longRelease.releaseIdle();
    check(longRelease.gain()>.99999f,"long recovery at maximum rate does not get stuck below unity");
}
void permanentLinkedGuard() {
    Limiter limiter;
    near(limiter.ceiling(),maximumCeiling,0,"default ceiling is exactly minus one dB");
    limiter.setCeiling(.95f);near(limiter.ceiling(),maximumCeiling,0,"old .95 presets respect the new system maximum");
    limiter.setCeiling(1);near(limiter.ceiling(),maximumCeiling,0,"unity cannot disable system headroom");
    float left=2,right=-.5f;limiter.process(left,right,false);
    near(left,maximumCeiling,1e-7f,"Off retains the hard guard");
    near(right,-maximumCeiling*.25f,1e-7f,"Off guard links stereo instead of independently clipping");
    left=.5f;right=-.125f;limiter.process(left,right,false);
    near(left,.5f,0,"Off below the ceiling is exact unity");near(right,-.125f,0,"Off does not retain old reduction");
    near(limiter.gain(),1,0,"Off resets musical envelope for later reactivation");
    limiter.setCeiling(0);near(limiter.ceiling(),.01f,0,"minimum ceiling remains finite and nonzero");
    limiter.setCeiling(std::numeric_limits<float>::quiet_NaN());
    near(limiter.ceiling(),maximumCeiling,0,"invalid ceiling falls back to system maximum");
    const float inputs[]={0,1,-1,1e12f,-1e12f,std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()};
    for(bool enabled : {false,true}) for(float l : inputs) for(float r : inputs) {
        left=l;right=r;limiter.process(left,right,enabled);
        check(std::isfinite(left) && std::isfinite(right) && std::fabs(left)<=maximumCeiling &&
              std::fabs(right)<=maximumCeiling,"every guard mode sanitizes and bounds extreme samples");
    }
    limiter.setCeiling(.2f);left=-1;right=.25f;limiter.guard(left,right);
    near(left,-.2f,0,"lowered ceiling guards immediately");near(right,.05f,0,"guard-only path is linked");
}
}
int main() {
    try {
        unityKneeAndStereo();twoTimeRelease();permanentLinkedGuard();
        std::cout<<"Limiter: unity, progressive knee, linked stereo, two-time recovery and permanent -1dB guard passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
