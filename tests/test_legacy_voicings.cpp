#include "dsp/legacy_voicings.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

// synthetic_fixture only: mathematical restoration of OUR previous candidate.
// This is not a Line 6 capture or a commercial sonic-equivalence test.
namespace {
constexpr double pi=3.1415926535897932384626433832795;
int failures=0;
void check(bool condition,const char* text) {if(!condition){std::cerr<<"FAIL: "<<text<<'\n';++failures;}}
double rational(double x,double drive,double factor) {
    const double z=(1+factor*drive)*x;
    return z/(1+drive*std::fabs(z));
}
void archivedTransfers() {
    for(double d:{0.0,.01,.1,.4,1.0})for(double factor:{5.0,7.0}) {
        double maximum=0;
        for(unsigned n=0;n<=10000;++n) {
            const float x=-4+8.0f*n/10000;
            const float drive=static_cast<float>(d);
            const double actual=echolab::legacy::saturate(x,drive,static_cast<float>(factor));
            maximum=std::fmax(maximum,std::fabs(actual-rational(x,drive,factor)));
        }
        check(maximum<3e-6,"A04 static transfer matches independent float64 ELF-derived equation");
    }
    for(float x:{0.0f,-0.0f,.1f,-1.0f,4.0f}) {
        const float y=echolab::legacy::saturate(x,0);
        check(std::memcmp(&x,&y,sizeof x)==0,"archived Drive0 is bit-exact identity");
    }
    const float x=.4f,d=.4f;
    const float incorrect=(1+7*d)*x/(1+std::fabs((1+7*d)*x));
    check(std::fabs(echolab::legacy::saturate(x,d)-incorrect)>.1f,"fixture distinguishes the historically incorrect missing-Drive denominator");
    for(float input:{-.8f,-.1f,0.0f,.1f,.8f}) {
        const double expected=rational(input,d,7)+.12*static_cast<double>(d)*input*input/(1+static_cast<double>(input)*input);
        check(std::fabs(echolab::legacy::tube(input,d)-expected)<1e-6,"A04 Tube asymmetry uses the pre-saturation input as verified in ELF");
    }
}

// Fixed analytical component oracle in float64, with independent storage and
// equations. No model selection, delay engine, events or production audio API.
struct ReferenceExciter {
    bool a04;
    double split=0,dcPole=0,postPole=0,low=0,previousShape=0,dc=0,post=0,previousHigh=0;
    explicit ReferenceExciter(bool original):a04(original){}
    void prepare(double fs,double tune) {
        split=std::exp(-2*pi*tune/fs);dcPole=std::exp(-2*pi*20/fs);
        postPole=std::exp(-2*pi*std::fmin(a04?16000.0:12000.0,.45*fs)/fs);
    }
    static double soft(double u) {return u>=1?2.0/3:(u<=-1?-2.0/3:u-u*u*u/3);}
    double process(double x,double amount,double drive) {
        low=(1-split)*x+split*low;
        const double high=x-low;
        double shape=0;
        if(a04) {
            const double u=(1+15*drive)*high;
            shape=u/(1+std::fabs(u))-high/(1+std::fabs(high))+.25*drive*u*u/(1+u*u);
        } else {
            const double s=.25+1.75*drive,middle=(previousHigh+high)/2;
            const double nonlinear=(soft(s*previousHigh)+2*soft(s*middle)+soft(s*high))/(4*s);
            shape=drive*(nonlinear-(previousHigh+high)/2);previousHigh=high;
        }
        dc=shape-previousShape+dcPole*dc;previousShape=shape;
        post=(1-postPole)*dc+postPole*post;
        return x+(a04?2:1)*amount*post;
    }
};
template<class State>
void samplewiseExciter(bool a04) {
    for(double fs:{8000.0,44100.0,48000.0,96000.0}) {
        const double tune=std::fmin(a04?4000.0:2500.0,.4*fs);
        State actual;actual.prepare(static_cast<float>(fs),static_cast<float>(tune));actual.reset();
        ReferenceExciter reference(a04);reference.prepare(fs,tune);
        double maximum=0;
        for(unsigned n=0;n<50000;++n) {
            const float x=static_cast<float>(.5*std::sin(2*pi*997*n/fs)+.15*std::sin(2*pi*271*n/fs));
            const float y=actual.process(x,0,.1f,.4f);
            const double expected=reference.process(x,static_cast<double>(.1f),static_cast<double>(.4f));
            maximum=std::fmax(maximum,std::fabs(y-expected));
            check(std::isfinite(y),"restored exciter remains finite");
        }
        check(maximum<1e-5,"restored exciter agrees samplewise with independent float64 recurrence");
        std::cout<<"synthetic_fixture legacy "<<(a04?"A04":"A05")<<" Fs="<<fs<<" max_error="<<maximum<<'\n';
        actual.reset();check(actual.process(0,0,.1f,.4f)==0,"reset removes every exciter state");
        check(actual.process(0,1,.1f,.4f)==0,"right channel state is independent");
        State untouched=actual;
        for(float x:{0.0f,-0.0f,.25f,-.5f}) {
            const float y=actual.process(x,0,0,.4f);
            check(std::memcmp(&x,&y,sizeof x)==0,"Amount0 exact bypass");
            check(actual.process(x,0,.1f,0)==x,"Drive0 exact bypass");
            check(actual.process(x,2,.1f,.4f)==x,"invalid channel bypasses without state access");
        }
        check(actual.process(.2f,0,.1f,.4f)==untouched.process(.2f,0,.1f,.4f),"disabled legacy exciter freezes its states");
    }
}
void archivedA04Harmonics() {
    echolab::legacy::ExciterA04 state;state.prepare(48000,3000);
    double re2=0,im2=0,re3=0,im3=0,mean=0;
    for(unsigned n=0;n<52800;++n) {
        const double phase=2*pi*1000*n/48000;
        const float x=.5f*static_cast<float>(std::sin(phase));
        const double residue=static_cast<double>(state.process(x,0,.6f,.6f))-x;
        if(n>=48000){re2+=residue*std::cos(2*phase);im2-=residue*std::sin(2*phase);re3+=residue*std::cos(3*phase);im3-=residue*std::sin(3*phase);mean+=residue;}
    }
    const double h2=2*std::sqrt(re2*re2+im2*im2)/4800,h3=2*std::sqrt(re3*re3+im3*im3)/4800;
    check(std::fabs(h2-.0527772)<2e-6 && std::fabs(h3-.0895707)<2e-6,"restoration reproduces the archived A04 harmonic fixture");
    check(std::fabs(mean/4800)<1e-5,"restored A04 blocks steady DC in its harmonic branch");
    std::cout<<"synthetic_fixture archived A04 H2="<<h2<<" H3="<<h3<<" mean="<<mean/4800<<'\n';
}
} // namespace
int main() {
    archivedTransfers();samplewiseExciter<echolab::legacy::ExciterA04>(true);samplewiseExciter<echolab::legacy::ExciterA05>(false);archivedA04Harmonics();
    if(failures){std::cerr<<failures<<" legacy synthetic_fixture failures\n";return 1;}
    std::cout<<"legacy synthetic_fixture checks passed\n";return 0;
}
