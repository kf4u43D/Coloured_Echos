#define ECHOLAB_NT_HOST_TEST
#include "../src/nt/echo_lab.cpp"
#include <vector>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <cstring>

extern "C" const _NT_globals NT_globals = {48000, 128, nullptr, 0, 0, 0};
static unsigned pageNotifications=0;
static const char* lastDrawText=nullptr;
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*) { return 0; }
extern "C" void NT_updateParameterPages(uint32_t) { ++pageNotifications; }
extern "C" void NT_drawText(int,int,const char* text,int,_NT_textAlignment,_NT_textSize) { lastDrawText=text; }
using namespace echolab_nt;

static void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
static void near(float a, float b, const char* text) { check(std::fabs(a-b) < 2e-6f, text); }
static void near(float a, float b, float tolerance, const char* text) { check(std::isfinite(a) && std::fabs(a-b)<=tolerance,text); }
struct Instance {
    _NT_algorithmRequirements req{};
    std::vector<uint8_t> sram, dram;
    std::vector<int16_t> values;
    _NT_algorithm* self;
    explicit Instance(int32_t seconds = 1, bool limiterEnabled = false) {
        calculateRequirements(req, &seconds);
        sram.resize(req.sram + 2, 0xad); dram.resize(req.dram + 2, 0xad);
        _NT_algorithmMemoryPtrs ptrs{sram.data()+1, dram.data()+1, nullptr, nullptr};
        self = construct(ptrs, req, &seconds);
        for (const auto& p : parameters) values.push_back(p.def);
        // Analytic fixtures disable musical gain reduction, keeping the guard.
        values[LimiterOn]=limiterEnabled ? 1 : 0;
        self->v = values.data();
        for (int i=0; i<ParameterCount; ++i) parameterChanged(self, i);
    }
    void set(int p, int v) { values[p] = static_cast<int16_t>(v); parameterChanged(self, p); }
};
static void routing() {
    for (int mode = 0; mode <= 1; ++mode) {
        Instance instance;
        instance.set(Bypass, 1);
        instance.set(InputL, 1); instance.set(InputR, 2);
        // Cross aliases: reading R must happen before writing L to R's bus.
        instance.set(OutputL, 2); instance.set(OutputR, 1);
        instance.set(OutputModeL, mode); instance.set(OutputModeR, mode);
        for (int n : {4, 20, 128}) {
            std::vector<float> b(kNT_lastBus*n, 0.0f);
            for (int i=0;i<n;++i) { b[i]=.01f*float(i+1); b[n+i]=-.01f*float(i+2); }
            step(instance.self, b.data(), n/4);
            for (int i=0;i<n;++i) {
                near(b[i], mode ? -.01f*float(i+2) : -.01f, "cross alias R output");
                near(b[n+i], mode ? .01f*float(i+1) : -.01f, "cross alias L output");
            }
        }
    }
    Instance mono;
    mono.set(Bypass, 1); mono.set(OutputL, kNT_lastBus); mono.set(OutputR, kNT_lastBus);
    mono.set(OutputModeL, 1); mono.set(OutputModeR, 0);
    std::vector<float> b(kNT_lastBus*4, 7.0f);
    for (int i=0;i<4;++i) {b[i]=2; b[4+i]=4;}
    step(mono.self,b.data(),1);
    for (int i=0;i<4;++i) near(b[(kNT_lastBus-1)*4+i],3,"same bus fold-down L mode wins");
    mono.set(InputL, 0); mono.set(InputR, 0);
    step(mono.self,b.data(),1);
    near(b[(kNT_lastBus-1)*4],0,"None inputs silent");
    mono.set(OutputL, 0); mono.set(OutputR, 0);
    const auto snapshot = b;
    step(mono.self,b.data(),1);
    check(b == snapshot,"None outputs untouched");
    mono.set(OutputL, -1); mono.set(OutputR, 100);
    step(mono.self,b.data(),1);
    check(b == snapshot,"invalid buses guarded");
    step(mono.self,b.data(),0); step(mono.self,b.data(),33);
    check(b == snapshot,"invalid block rejected");
}
static void sharedCoreAndInstances(int model) {
    Instance instance;
    instance.set(Model, model);
    near(static_cast<Algorithm*>(instance.self)->engine.parameters().feedback, .45f,
         "every factory model starts with 45 percent feedback");
    instance.set(OutputModeL, 1); instance.set(OutputModeR, 1);
    instance.set(Time, 10); instance.set(Mix, 800); instance.set(Feedback, 650);
    instance.set(Tone, 1800); instance.set(Drive, 350);
    check(static_cast<Algorithm*>(instance.self)->engine.parameters().model == static_cast<echolab::Model>(model),
          "every NT model selector reaches the intended core model");
    echolab::Engine expected;
    std::vector<float> memory(echolab::Engine::requiredSamples(48000,1000));
    check(expected.prepare(48000,1000,memory.data(),memory.size()),"reference prepare");
    expected.setParameters(static_cast<Algorithm*>(instance.self)->controls);
    for (int block=0;block<150;++block) {
        if(block==50 || block==100) {
            instance.set(ModulationEnabled,block==100 ? 1 : 0);
            expected.setParameters(static_cast<Algorithm*>(instance.self)->controls);
        }
        const int n = block%2 ? 20 : 128;
        std::vector<float> buses(kNT_lastBus*n,0.0f);
        for (int i=0;i<n;++i) {
            const float l=std::sin(float(block*128+i)*.017f)*.4f;
            const float r=std::cos(float(block*128+i)*.023f)*.3f;
            buses[i]=l*8; buses[n+i]=r*8;
        }
        auto input=buses;
        step(instance.self,buses.data(),n/4);
        for (int i=0;i<n;++i) {
            float l,r; expected.processSample(input[i]/8,input[n+i]/8,l,r);
            near(buses[12*n+i]/8,l,"shared core left"); near(buses[13*n+i]/8,r,"shared core right");
        }
    }
    Instance silent;
    silent.set(Model, model);
    silent.set(Mix,1000); silent.set(OutputModeL,1); silent.set(OutputModeR,1);
    std::vector<float> b(kNT_lastBus*128,0.0f);
    for (int i=0;i<30;++i) step(silent.self,b.data(),32);
    for (float v:b) near(v,0,"second instance isolated");
    check(instance.dram.front()==0xad && instance.sram.front()==0xad &&
          instance.dram.back()==0xad && instance.sram.back()==0xad,"unaligned allocation boundary canaries");
}

static void configureNewModel(Instance& instance, int model, int time = 1) {
    instance.set(Model, model); instance.set(Time, time);
    instance.set(Mix, 1000); instance.set(Feedback, 500);
    instance.set(Tone, 1700); instance.set(Drive, 200);
    instance.set(FeedbackLPHz,0);instance.set(FeedbackHPHz,0);instance.set(Drift,0);
    instance.set(OutputModeL, 1); instance.set(OutputModeR, 1);
    // Fixture setup, outside step: establish an exact delay and wet mix.
    static_cast<Algorithm*>(instance.self)->engine.reset();
}

static void statefulModelAliases() {
    // Compare stateful routing to separate-bus rendering, including cross-channel
    // feedback. Merely passing dry samples cannot detect corrupted delay writes.
    for (int model : {3, 4, 13}) for (int route : {0, 1, 2}) for (int mode : {0, 1}) {
        Instance separate, aliased;
        configureNewModel(separate, model); configureNewModel(aliased, model);
        const int outputL = route == 0 ? 1 : 2;
        const int outputR = route == 1 ? 1 : 2;
        aliased.set(OutputL, outputL); aliased.set(OutputR, outputR);
        aliased.set(OutputModeL, mode); aliased.set(OutputModeR, route == 2 ? 1-mode : mode);
        size_t cursor = 0;
        float delayedPeak = 0.0f;
        for (int block = 0; block < 24; ++block) {
            const int n = block % 3 == 0 ? 128 : (block % 3 == 1 ? 20 : 4);
            std::vector<float> reference(kNT_lastBus*n, 0.0f);
            for (int i=0;i<n;++i) {
                const size_t frame = cursor + static_cast<size_t>(i);
                reference[i] = frame == 0 ? 4.0f : (frame == 17 ? 2.0f : 0.0f);
                reference[n+i] = frame == 3 ? -2.0f : (frame == 37 ? 1.0f : 0.0f);
            }
            auto actual = reference;
            const auto original = reference;
            step(separate.self,reference.data(),n/4);
            step(aliased.self,actual.data(),n/4);
            for (int i=0;i<n;++i) {
                const float l = reference[12*n+i], r = reference[13*n+i];
                if (outputL == outputR) {
                    const float expected = 0.5f*(l+r) + (mode ? 0.0f : original[(outputL-1)*n+i]);
                    near(actual[(outputL-1)*n+i],expected,"stateful same-bus mono alias/Add/Replace");
                } else {
                    near(actual[(outputL-1)*n+i],l+(mode ? 0.0f : original[(outputL-1)*n+i]),
                         "stateful left alias/Add/Replace");
                    near(actual[(outputR-1)*n+i],r+(mode ? 0.0f : original[(outputR-1)*n+i]),
                         "stateful right alias/Add/Replace");
                }
                if (cursor+static_cast<size_t>(i) >= 48)
                    delayedPeak = std::max(delayedPeak,std::max(std::fabs(l),std::fabs(r)));
            }
            cursor += static_cast<size_t>(n);
        }
        check(delayedPeak > 0.01f,"alias fixture exercises a stored delay tail");
    }
}

static void analyticalNewModelRouting() {
    // synthetic_fixture: both inputs carry an identical half-scale impulse. The
    // provisional mono-excitation ping-pong emits L@T, R@2T, L@3T, etc.
    Instance pingpong;
    configureNewModel(pingpong,3);
    for (int block=0;block<88;++block) {
        std::vector<float> b(kNT_lastBus*4,0.0f);
        if (block==0) { b[0]=4; b[4]=4; }
        step(pingpong.self,b.data(),1);
        for (int i=0;i<4;++i) {
            const int frame=block*4+i;
            const int repeat=frame/48;
            const float gain=frame>0 && frame%48==0 ? 4.0f*std::pow(0.5f,float(repeat-1)) : 0.0f;
            near(b[12*4+i],repeat%2==1 ? gain : 0.0f,"ping-pong independent left repeat time/gain");
            near(b[13*4+i],repeat%2==0 ? gain : 0.0f,"ping-pong independent right repeat time/gain");
        }
    }
    // synthetic_fixture: with feedback/drive zero, each of the three equally
    // weighted taps carries a shifted one-pole impulse, independently of R.
    Instance multihead;
    configureNewModel(multihead,4,3);
    multihead.set(Feedback,0); multihead.set(Drive,0); multihead.set(Tone,4500);
    static_cast<Algorithm*>(multihead.self)->engine.reset();
    constexpr double pi=3.14159265358979323846;
    const double pole=std::exp(-2.0*pi*4500.0/48000.0);
    for (int block=0;block<60;++block) {
        std::vector<float> b(kNT_lastBus*4,0.0f);
        if (block==0) b[0]=8;
        step(multihead.self,b.data(),1);
        for (int i=0;i<4;++i) {
            const int frame=block*4+i;
            double expected=0;
            for (int tap : {48,96,144}) if(frame>=tap)
                expected += 8.0*(1.0-pole)*std::pow(pole,frame-tap)/3.0;
            near(b[12*4+i],static_cast<float>(expected),"multi-head independent tap timing/filter/gain");
            near(b[13*4+i],0,"multi-head does not cross-feed unexcited R");
        }
    }
}
static void bypassAndNonfinite() {
    for (int model=0;model<echolab::modelCount;++model) for (int mode : {0, 1, 2}) {
        Instance i;
        i.set(Model, model);
        i.set(Bypass, mode); i.set(OutputModeL, 1); i.set(OutputModeR, 1);
        std::vector<float> b(kNT_lastBus*4,0.0f);
        b[0]=std::numeric_limits<float>::infinity();
        b[4]=std::numeric_limits<float>::quiet_NaN();
        step(i.self,b.data(),1);
        check(std::isfinite(b[12*4]) && std::isfinite(b[13*4]),"nonfinite NT input sanitized");
    }
    Instance fallback;
    auto* a=static_cast<Algorithm*>(fallback.self);
    a->ready=false;
    fallback.set(Bypass,2); fallback.set(OutputModeL,1); fallback.set(OutputModeR,1);
    std::vector<float> b(kNT_lastBus*4,0.0f);
    b[0]=2; b[4]=4;
    step(fallback.self,b.data(),1);
    near(b[12*4],2,"unready tails preserves single dry L");
    near(b[13*4],4,"unready tails preserves single dry R");
    // A pre-existing delay tail is audible in Tails and hidden in Dry.
    Instance tails, dry;
    for (Instance* i : {&tails,&dry}) {
        i->set(Time,1); i->set(Mix,1000); i->set(Feedback,0);
        auto* state=static_cast<Algorithm*>(i->self);
        state->engine.reset(); // Test setup only, outside audio callback.
        i->set(OutputModeL,1); i->set(OutputModeR,1);
    }
    b.assign(kNT_lastBus*4,0.0f); b[0]=4;
    auto other=b;
    step(tails.self,b.data(),1); step(dry.self,other.data(),1);
    tails.set(Bypass,2); dry.set(Bypass,1);
    float peak=0;
    for(int n=0;n<20;++n) {
        b.assign(kNT_lastBus*4,0.0f); other=b;
        step(tails.self,b.data(),1); step(dry.self,other.data(),1);
        for(int k=0;k<4;++k) {peak=std::max(peak,std::fabs(b[12*4+k])); near(other[12*4+k],0,"Dry hides tail");}
    }
    check(peak>3.9f,"Tails emits existing wet tail");
}
static void appendedControls() {
    Instance i;
    i.set(Model,5); i.set(BitDepth,12); i.set(ModRate,125); i.set(ModDepth,375);
    i.set(DuckThreshold,-300); i.set(DuckAmount,1000); i.set(Attack,12); i.set(Release,200);
    i.set(Swell,450); i.set(StereoRatio,175); i.set(LoopAction,2);
    i.set(FeedbackLPHz,8900);i.set(Drift,375);
    i.set(FeedbackHPHz,125);i.set(LoResEndHz,900);i.set(LoResDecayMs,2300);i.set(LoResEnvelopeAmount,650);
    i.set(RackSampleRateHz,12000);
    i.set(LoResCutoffHz,1350);i.set(Voicing,3);i.set(ModulationEnabled,0);i.set(Mod1Destination,8);
    const auto& p=static_cast<Algorithm*>(i.self)->engine.parameters();
    check(p.bit_depth==12 && p.loop_action==2,"integer controls reach shared core");
    near(p.mod_rate_hz,1.25f,"modulation rate hundredths"); near(p.mod_depth_ms,3.75f,"modulation depth hundredths");
    near(p.duck_threshold_db,-30,"duck threshold tenths dB"); near(p.duck_amount,1,"duck amount percent");
    near(p.attack_ms,12,"attack ms"); near(p.release_ms,200,"release ms"); near(p.swell_ms,450,"swell ms");
    near(p.stereo_ratio,1.75f,"stereo ratio hundredths");
    near(p.feedback_lowpass_hz,8900,"feedback low-pass Hz reaches core");
    near(p.drift_amount,.375f,"slow drift uses permille amount");
    near(p.feedback_highpass_hz,125,"feedback high-pass Hz reaches core");
    near(p.lores_rate_end_hz,900,"Lo Res endpoint Hz reaches core");
    near(p.lores_rate_decay_ms,2300,"Lo Res rate decay uses milliseconds");
    near(p.lores_rate_envelope_amount,.65f,"Lo Res envelope amount uses permille");
    near(p.digital_rack_rate_hz,12000,"Rack sample rate has its own Hz field");
    near(p.lores_cutoff_hz,1350,"Lo Res wet low-pass has its own Hz field");
    check(p.voicing==3 && !p.modulation_enabled && p.mod1_destination==8,"voicing, modulation toggle and appended Fe destination reach core");
}
static bool pageHas(const _NT_algorithm* self,int parameter) {
    for(uint32_t page=0;page<self->parameterPages->numPages;++page) {
        const auto& p=self->parameterPages->pages[page];
        for(unsigned i=0;i<p.numParams;++i) if(p.params[i]==parameter) return true;
    }
    return false;
}
static void dynamicPagesAndControls() {
    Instance i;
    check(!pageHas(i.self,BitDepth) && !pageHas(i.self,LoopAction),"Digital pages hide unrelated model controls");
    check(!pageHas(i.self,FeedbackLPHz) && !pageHas(i.self,FeedbackHPHz) && !pageHas(i.self,Drift),
          "Digital Auto A05 hides B06 regeneration filters it does not use");
    check(pageHas(i.self,Voicing) && pageHas(i.self,ModulationEnabled),"voice and internal modulation switch are common controls");
    check(!pageHas(i.self,LoResEndHz) && !pageHas(i.self,LoResDecayMs),"rate envelope is absent from Digital pages");
    const auto before=pageNotifications;
    i.set(Model,5);
    check(pageNotifications==before+1 && pageHas(i.self,BitDepth) && pageHas(i.self,SampleRateHz),
          "Lo Res model publishes its own parameter pages through the host callback");
    check(pageHas(i.self,LoResEndHz) && pageHas(i.self,LoResDecayMs) && pageHas(i.self,LoResEnvelopeAmount),
          "Lo Res exposes its sample-rate decay envelope");
    i.set(Mix,500);check(pageNotifications==before+1,"ordinary control changes do not rebuild pages");
    i.set(Model,16);check(pageHas(i.self,LoopAction) && !pageHas(i.self,BitDepth),"Looper has its action page");
    check(pageHas(i.self,FeedbackLPHz) && pageHas(i.self,Drift),"Looper exposes its Overdub feedback colour");
    i.set(Model,17);check(pageHas(i.self,ExciterAmount) && pageHas(i.self,ExciterTuneHz),"Exciter has its own controls");
    i.set(Model,18);check(pageHas(i.self,Tone) && pageHas(i.self,Drive) && pageHas(i.self,ModRate) && !pageHas(i.self,ModDepth),
                          "Sweep Up exposes colour and one-shot sweep duration controls");
    const int automaticVoices[]={2,1,3,3,3,3,3,1,3,1,1,3,2,3,1,3,3,1,3,1};
    for(int voice=0;voice<=3;++voice) for(int model=0;model<echolab::modelCount;++model) {
        i.set(Voicing,voice);
        i.set(Model,model);
        const int resolved=model==19 ? 1 : (model==2 ? 3 : (voice ? voice : automaticVoices[model]));
        check(echolab::resolvedVoicing(static_cast<echolab::Model>(model),voice)==resolved,"published automatic voice selection and fixed Tube A04");
        check(pageHas(i.self,Voicing)==(model!=2 && model!=19),"both fixed Tube models hide the voicing override");
        check(pageHas(i.self,FeedbackLPHz)==(resolved==3 || (resolved==2 && model!=0)) &&
              pageHas(i.self,FeedbackHPHz)==(resolved==3) && pageHas(i.self,Drift)==(resolved!=1 && model!=0),
              "feedback colour pages follow the actual model scope");
        check(pageHas(i.self,LoResEnvelopeAmount)==(model==5 && resolved==3) && pageHas(i.self,LoResCutoffHz)==(model==5 && resolved==3),
              "Lo Res envelope and wet cutoff are scoped to its B06 voice");
        check(pageHas(i.self,RackSampleRateHz)==(model==13 && resolved==3),"Rack internal rate is scoped to its B06 voice");
        check(i.self->parameterPages->numPages<=ARRAY_SIZE(static_cast<Algorithm*>(i.self)->pageStorage),"dynamic page storage capacity");
        for(uint32_t page=0;page<i.self->parameterPages->numPages;++page) {
            const auto& p=i.self->parameterPages->pages[page];
            for(unsigned id=0;id<p.numParams;++id) check(p.params[id]<ParameterCount,"all page indices are valid and stable");
        }
    }
    i.set(Feedback,2000);i.set(SampleRateHz,0);i.set(ExciterAmount,750);i.set(ExciterTuneHz,6000);i.set(TapPattern,7);
    i.set(Mod1Source,3);i.set(Mod1Destination,5);i.set(Mod1Amount,-500);
    i.set(Mod2Source,5);i.set(Mod2Destination,4);i.set(Mod2Amount,250);
    const auto& p=static_cast<Algorithm*>(i.self)->engine.parameters();
    near(p.feedback,2.0f,"200 percent feedback reaches the core");near(p.sample_rate_hz,0,"zero sample rate requests full Fs");
    near(p.exciter_amount,.75f,"Exciter amount permille");near(p.exciter_tune_hz,6000,"Exciter tune Hz");
    check(p.tap_pattern==7 && p.mod1_source==3 && p.mod1_destination==5 && p.mod2_source==5,"tap and modulation route IDs");
    near(p.mod1_amount,-.5f,"negative modulation amount");near(p.mod2_amount,.25f,"positive modulation amount");
    char text[kNT_parameterStringSize]{};
    i.set(Model,1);i.set(Voicing,0);
    check(parameterString(i.self,Voicing,0,text)>0 && std::strcmp(text,"Auto: A04")==0,"automatic voice display names the effective per-model sound");
    const auto beforeVoice=pageNotifications;
    i.set(Voicing,3);
    check(pageNotifications==beforeVoice+1 && pageHas(i.self,FeedbackHPHz),"changing effective voice refreshes model pages without changing the model");
    i.set(Model,19);
    check(parameterString(i.self,Voicing,3,text)>0 && std::strcmp(text,"Fixed: A04")==0,"Tube A04 exposes its fixed voice when an override requests another");
    i.set(Model,2);
    check(parameterString(i.self,Voicing,1,text)>0 && std::strcmp(text,"Fixed: B06")==0,"Tube B06 remains fixed when an override requests A04");
    check(parameterString(i.self,SampleRateHz,0,text)>0 && std::strcmp(text,"Full Fs")==0,"Full Fs is explicit in parameter display");
    check(parameterString(i.self,RackSampleRateHz,0,text)>0 && std::strcmp(text,"Full Fs")==0,"Rack rate bypass displays Full Fs");
    check(parameterString(i.self,RackSampleRateHz,16000,text)>0 && std::strcmp(text,"16000 Hz")==0,"Rack displays its actual internal rate");
    check(parameterString(i.self,RackSampleRateHz,1,text)>0 && std::strcmp(text,"1000 Hz")==0,"Rack subminimum preset rate displays its effective lower bound");
    check(parameterString(i.self,FeedbackLPHz,0,text)>0 && std::strcmp(text,"Off")==0,"feedback low-pass diagnostic bypass is explicit");
    check(parameterString(i.self,FeedbackHPHz,0,text)>0 && std::strcmp(text,"Off")==0,"feedback high-pass bypass is explicit");
    check(parameterString(i.self,FeedbackHPHz,80,text)>0 && std::strcmp(text,"80 Hz")==0,"feedback high-pass displays its effective frequency");
    check(parameterString(i.self,LoResCutoffHz,0,text)>0 && std::strcmp(text,"Off")==0,"Lo Res wet low-pass bypass is explicit");
    check(parameterString(i.self,LoResCutoffHz,2000,text)>0 && std::strcmp(text,"2000 Hz")==0,"Lo Res wet low-pass frequency is explicit");
    i.set(Model,13);
    check(parameterString(i.self,FeedbackLPHz,12000,text)>0 && std::strcmp(text,"6000 Hz")==0,
          "Digital Rack displays its actual lower feedback cutoff");
    i.set(Voicing,2);
    check(parameterString(i.self,FeedbackLPHz,12000,text)>0 && std::strcmp(text,"7800 Hz")==0 && !pageHas(i.self,RackSampleRateHz),
          "Rack A05 keeps its historical cutoff and full-Fs path");
    i.set(Voicing,3);
    i.set(Model,1);
    check(parameterString(i.self,FeedbackLPHz,12000,text)>0 && std::strcmp(text,"12000 Hz")==0,
          "other models display nominal feedback cutoff below the sample-rate limit");
    draw(i.self);check(lastDrawText && std::strcmp(lastDrawText,"Free Time")==0,"draw reports clock state");
}
static void finalBusLimiter() {
    for(bool enabled : {false,true}) for(int bypass : {0,1,2}) for(bool same : {false,true}) for(int mode : {0,1}) {
        Instance i(1,enabled);i.set(Bypass,bypass);i.set(Mix,0);
        i.set(OutputL,13);i.set(OutputR,same ? 13 : 14);
        i.set(OutputModeL,mode);i.set(OutputModeR,mode);
        i.set(LimiterCeiling,500);
        std::vector<float> b(kNT_lastBus*4,0.0f);
        for(int k=0;k<4;++k) { b[k]=80;b[4+k]=-20;b[12*4+k]=16;b[13*4+k]=8; }
        step(i.self,b.data(),1);
        for(int k=0;k<4;++k) {
            check(std::fabs(b[12*4+k])<=4.0f+1e-6f,"permanent guard covers Limiter Off/On, Active/Dry/Tails, Add/Replace/fold-down");
            if(!same) check(std::fabs(b[13*4+k])<=4.0f+1e-6f,"permanent guard bounds the right target bus");
        }
    }
    Instance linked(1,true);linked.set(Bypass,1);linked.set(OutputModeL,1);linked.set(OutputModeR,1);linked.set(LimiterCeiling,500);
    std::vector<float> b(kNT_lastBus*4,0.0f);b[0]=16;b[4]=4;
    step(linked.self,b.data(),1);near(b[12*4],4,"limiter instantaneous left attack");near(b[13*4],1,"postmix stereo limiter uses linked gain");
    linked.set(LimiterOn,0);b.assign(kNT_lastBus*4,0.0f);b[0]=16;b[4]=4;
    step(linked.self,b.data(),1);near(b[12*4],4,"Off still guards Dry");near(b[13*4],1,"Off guard preserves stereo image");
    linked.set(LimiterCeiling,950);
    auto* state=static_cast<Algorithm*>(linked.self);
    near(state->controls.limiter_ceiling,.891f,1e-7f,"loaded old ceiling950 is clamped to the NT maximum");
    check(state->engine.parameters().limiter_ceiling<=echolab::safety::maximumCeiling,
          "loaded old ceiling cannot evade system headroom");
}
static void singleMusicalStage() {
    for(int route : {0,1,2}) {
        Instance instance(1,true);
        instance.set(Model,0);instance.set(Mix,0);
        instance.set(OutputModeL,1);instance.set(OutputModeR,route==1 ? 0 : 1);
        if(route==1) instance.set(OutputR,13); // Its ignored R Add must not re-limit L.
        if(route==2) { instance.set(OutputR,0);instance.set(OutputModeR,0); }
        auto* state=static_cast<Algorithm*>(instance.self);
        state->engine.reset();
        echolab::Engine reference;
        std::vector<float> memory(echolab::Engine::requiredSamples(48000,1000));
        check(reference.prepare(48000,1000,memory.data(),memory.size()),"single-stage reference prepares");
        reference.setParameters(state->controls);reference.reset();
        std::vector<float> buses(kNT_lastBus*4,0.0f);
        for(int k=0;k<4;++k) { buses[k]=6.4f;buses[4+k]=3.2f; }
        step(instance.self,buses.data(),1);
        for(int k=0;k<4;++k) {
            float l=0,r=0;reference.processSample(.8f,.4f,l,r);
            check(l<.8f && l>.7f,"fixture reaches the progressive knee");
            near(buses[12*4+k],8*(route==1 ? .5f*(l+r) : l),
                 "Active Replace has no second musical reduction");
            if(route==0) near(buses[13*4+k],8*r,"stereo Replace retains one linked musical stage");
        }
        near(state->outputLimiter.gain(),1,0,"unused postmix musical envelope remains at unity");
    }
}
static void clockAndCvAliasing() {
    Instance i;i.set(Bypass,1);i.set(OutputModeL,1);i.set(OutputModeR,1);i.set(ClockBus,13);i.set(SyncMode,2);i.set(ClockPpqn,2);
    i.set(CV1Bus,13);i.set(CV2Bus,14);
    auto* state=static_cast<Algorithm*>(i.self);
    // Clock/CV on output buses must be sampled before Dry overwrites them.
    for(int block=0;block<1501;++block) {
        std::vector<float> b(kNT_lastBus*4,0.0f);
        b[12*4]=block%1500==0 ? 5.0f : 0.0f;
        step(i.self,b.data(),1);
    }
    check(state->clock.locked(),"CV clock input aliases a written output bus without losing edges");
    near(state->controls.delay_ms,500,1e-4f,"CV4PPQN updates shared delay controls");
    i.set(Drive,900);check(state->clock.locked(),"Drive changes preserve clock lock");
    i.set(ClockBus,15);check(!state->clock.locked(),"clock bus changes require reacquisition");
    i.set(SyncMode,3);i.set(TempoBpm,120);i.set(Division,6);near(state->controls.delay_ms,375,1e-4f,"internal BPM dotted division");
    i.set(SyncMode,1);midiRealtime(i.self,0xfa);midiRealtime(i.self,0xf8);
    for(int pulse=0;pulse<24;++pulse) {
        for(int block=0;block<250;++block) { std::vector<float> b(kNT_lastBus*4,0.0f);step(i.self,b.data(),1); }
        midiRealtime(i.self,0xf8);
    }
    std::vector<float> last(kNT_lastBus*4,0.0f);step(i.self,last.data(),1);
    near(state->controls.delay_ms,375,1e-4f,"MIDI 24PPQN timestamps use the rendered frame counter");
    midiRealtime(i.self,0xfc);step(i.self,last.data(),1);near(state->controls.delay_ms,350,0,"MIDI Stop falls back to manual Time");
    midiMessage(i.self,0xb7,1,127);near(state->wheel,1,0,"CC1 modulation wheel accepts every MIDI channel");
    midiMessage(i.self,0xb1,2,0);near(state->wheel,1,0,"other CCs leave the wheel unchanged");
}
static void modulationCvAliasing() {
    Instance separate,aliased;
    for(Instance* i : {&separate,&aliased}) {
        i->set(Model,1);i->set(OutputModeL,1);i->set(OutputModeR,1);
        i->set(Mod1Source,3);i->set(Mod1Destination,5);i->set(Mod1Amount,500);
        i->set(Mod2Source,4);i->set(Mod2Destination,4);i->set(Mod2Amount,300);
    }
    separate.set(CV1Bus,15);separate.set(CV2Bus,16);
    aliased.set(CV1Bus,13);aliased.set(CV2Bus,14);
    for(int block=0;block<160;++block) {
        const int n=block%2 ? 20 : 128;
        std::vector<float> a(kNT_lastBus*n,0.0f),b(kNT_lastBus*n,0.0f);
        for(int k=0;k<n;++k) {
            a[k]=b[k]=2.0f*std::sin(float(block*128+k)*.03f);
            a[n+k]=b[n+k]=1.5f*std::cos(float(block*128+k)*.02f);
            a[14*n+k]=b[12*n+k]=(k%3 ? 2.5f : -5.0f);
            a[15*n+k]=b[13*n+k]=(k%5 ? -2.5f : 5.0f);
        }
        step(separate.self,a.data(),n/4);step(aliased.self,b.data(),n/4);
        for(int k=0;k<n;++k) {
            near(a[12*n+k],b[12*n+k],"CV1 snapshot survives output-bus aliasing at audio rate");
            near(a[13*n+k],b[13*n+k],"CV2 snapshot survives output-bus aliasing at audio rate");
        }
    }
}
static void depthCvAndWheelRouting() {
    Instance instance;
    instance.set(Model,9);instance.set(Time,5);instance.set(Mix,1000);instance.set(Feedback,500);
    instance.set(Drive,0);instance.set(Tone,18000);instance.set(Drift,0);
    instance.set(FeedbackLPHz,0);instance.set(FeedbackHPHz,0);
    instance.set(OutputModeL,1);instance.set(OutputModeR,1);
    instance.set(CV1Bus,13); // CV must be read before its output alias is written.
    instance.set(Mod1Source,3);instance.set(Mod1Destination,6);instance.set(Mod1Amount,10);
    instance.set(Mod2Source,5);instance.set(Mod2Destination,6);instance.set(Mod2Amount,30);
    auto* state=static_cast<Algorithm*>(instance.self);
    state->engine.reset();
    echolab::Engine expected;
    std::vector<float> memory(echolab::Engine::requiredSamples(48000,1000));
    check(expected.prepare(48000,1000,memory.data(),memory.size()),"Depth routing reference prepares");
    expected.setParameters(state->controls);expected.reset();
    constexpr int n=128;
    int wheel=0;
    for(int block=0;block<96;++block) {
        if(block==24 || block==64) { wheel=block==24 ? 96 : 0;midiMessage(instance.self,0xb3,1,static_cast<uint8_t>(wheel)); }
        std::vector<float> b(kNT_lastBus*n,0.0f);
        for(int k=0;k<n;++k) {
            const float frame=float(block*n+k);
            b[k]=2.0f*std::sin(frame*.037f);b[n+k]=1.5f*std::cos(frame*.023f);
            b[12*n+k]=5.0f*std::sin(frame*.00017f);
        }
        const auto original=b;
        step(instance.self,b.data(),n/4);
        for(int k=0;k<n;++k) {
            expected.setExternalModulation(original[12*n+k]*.2f,0,wheel/127.0f);
            float l=0,r=0;expected.processSample(original[k]/8,original[n+k]/8,l,r);
            near(b[12*n+k],l*8,"CV to Depth keeps the audio-rate snapshot and shared core scaling");
            near(b[13*n+k],r*8,"wheel to Depth uses CC1 without changing its target to Drive");
        }
        near(state->controls.delay_ms,5,0,"Depth routing leaves manual Time unchanged");
        near(state->controls.drive,0,0,"Depth routing leaves Drive unchanged");
    }
}
static void digitalRackInternalRate() {
    Instance full,reduced,independent;
    for(Instance* i : {&full,&reduced,&independent}) {
        i->set(Model,13);i->set(Time,1);i->set(Mix,1000);i->set(Feedback,0);
        i->set(Drift,0);i->set(FeedbackLPHz,0);i->set(FeedbackHPHz,0);
        i->set(OutputModeL,1);i->set(OutputModeR,1);
    }
    full.set(RackSampleRateHz,0);
    reduced.set(RackSampleRateHz,16000);independent.set(RackSampleRateHz,16000);
    independent.set(SampleRateHz,500); // ID26 remains exclusive to Lo Res.
    for(Instance* i : {&full,&reduced,&independent}) static_cast<Algorithm*>(i->self)->engine.reset();
    near(static_cast<Algorithm*>(full.self)->engine.parameters().digital_rack_rate_hz,0,0,"zero Rack rate selects full Fs");
    near(static_cast<Algorithm*>(reduced.self)->engine.parameters().digital_rack_rate_hz,16000,0,"Rack16kHz reaches core unchanged");
    constexpr int n=128;
    double difference=0;
    std::size_t compared=0;
    const auto quantizedBus=[](float input) {
        constexpr float scale=8388608.0f;
        return static_cast<float>(static_cast<int32_t>((input/8)*scale))/scale*8;
    };
    for(int block=0;block<32;++block) {
        std::vector<float> a(kNT_lastBus*n,0.0f);
        for(int k=0;k<n;++k) {
            const float frame=float(block*n+k);
            a[k]=2*std::sin(frame*.91f);a[n+k]=1.5f*std::cos(frame*.71f);
        }
        auto b=a,c=a;
        step(full.self,a.data(),n/4);step(reduced.self,b.data(),n/4);step(independent.self,c.data(),n/4);
        for(int k=0;k<n;++k) {
            const int frame=block*n+k;
            const float l=frame>=48 ? quantizedBus(2*std::sin(float(frame-48)*.91f)) : 0;
            const float r=frame>=48 ? quantizedBus(1.5f*std::cos(float(frame-48)*.71f)) : 0;
            near(a[12*n+k],l,"Full Fs Rack preserves independent delayed24bit left samples");
            near(a[13*n+k],r,"Full Fs Rack preserves independent delayed24bit right samples");
            near(b[12*n+k],c[12*n+k],"Lo Res rate does not change Rack left internal clock");
            near(b[13*n+k],c[13*n+k],"Lo Res rate does not change Rack right internal clock");
            if(frame>=256) { const double error=a[12*n+k]-b[12*n+k];difference+=error*error;++compared; }
        }
    }
    check(std::sqrt(difference/compared)>.05,"Rack16kHz audibly changes high-frequency content compared with full Fs");
    reduced.set(RackSampleRateHz,1);
    near(static_cast<Algorithm*>(reduced.self)->engine.parameters().digital_rack_rate_hz,1000,0,"old subminimum Rack preset rate is bounded to1kHz");
}
static void loresFeMappings() {
    for(bool fullRate : {false,true}) for(bool enabled : {false,true}) for(int source=1;source<=5;++source) {
        Instance instance;
        instance.set(Model,5);instance.set(Voicing,3);instance.set(Time,1);instance.set(Mix,1000);instance.set(Feedback,0);
        instance.set(FeedbackLPHz,0);instance.set(FeedbackHPHz,0);instance.set(Drift,0);instance.set(LoResCutoffHz,0);
        instance.set(LoResEnvelopeAmount,0);instance.set(SampleRateHz,fullRate ? 0 : 8000);instance.set(BitDepth,12);
        instance.set(ModulationEnabled,enabled ? 1 : 0);
        instance.set(OutputModeL,1);instance.set(OutputModeR,1);instance.set(CV1Bus,13);instance.set(CV2Bus,14);
        instance.set(Mod1Source,source);instance.set(Mod1Destination,8);instance.set(Mod1Amount,500);
        instance.set(Mod2Source,source>=3 ? (source==5 ? 4 : 5) : 0);
        instance.set(Mod2Destination,8);instance.set(Mod2Amount,250);
        midiMessage(instance.self,0xb2,1,96);
        auto* state=static_cast<Algorithm*>(instance.self);state->engine.reset();
        echolab::Engine expected,unrouted;
        const auto count=echolab::Engine::requiredSamples(48000,1000);
        std::vector<float> memory(count),plainMemory(count);
        check(expected.prepare(48000,1000,memory.data(),memory.size()) &&
              unrouted.prepare(48000,1000,plainMemory.data(),plainMemory.size()),"Fe references prepare");
        expected.setParameters(state->controls);expected.reset();
        auto plain=state->controls;plain.mod1_source=plain.mod2_source=0;
        plain.mod1_destination=plain.mod2_destination=0;plain.mod1_amount=plain.mod2_amount=0;
        unrouted.setParameters(plain);unrouted.reset();
        constexpr int n=128;
        double difference=0;
        for(int block=0;block<12;++block) {
            std::vector<float> buses(kNT_lastBus*n,0.0f);
            for(int k=0;k<n;++k) {
                const float frame=float(block*n+k);
                buses[k]=2*std::sin(frame*.19f);buses[n+k]=1.5f*std::cos(frame*.23f);
                buses[12*n+k]=5*std::sin(frame*.003f);buses[13*n+k]=5*std::cos(frame*.002f);
            }
            const auto input=buses;step(instance.self,buses.data(),n/4);
            for(int k=0;k<n;++k) {
                expected.setExternalModulation(input[12*n+k]*.2f,input[13*n+k]*.2f,96/127.0f);
                float l=0,r=0,plainL=0,plainR=0;
                expected.processSample(input[k]/8,input[n+k]/8,l,r);
                unrouted.processSample(input[k]/8,input[n+k]/8,plainL,plainR);
                near(buses[12*n+k],l*8,"every Fe source uses normalized CV/wheel and snapshots its output alias");
                near(buses[13*n+k],r*8,"both Fe slots preserve stereo shared-core parity");
                const double error=buses[12*n+k]-plainL*8;difference+=error*error;
            }
        }
        const double rms=std::sqrt(difference/(12*n));
        if(fullRate || (!enabled && source<=2))
            check(rms<1e-7,"Full Fs ignores Fe mapping; Mod Off silences internal LFO/envelope mapping sources");
        else check(rms>.01,"Fe mapping remains audible for every enabled source and for external CV/wheel with Mod Off");
    }
}
static void firstPresetLoopLength() {
    for(bool cvTime : {false,true}) {
        Instance i(3);
        i.set(Model,16);i.set(Time,1451);i.set(Mix,1000);i.set(LoopAction,1);
        i.set(OutputModeL,1);i.set(OutputModeR,1);
        if(cvTime) { i.set(CV1Bus,3);i.set(Mod1Source,3);i.set(Mod1Destination,1);i.set(Mod1Amount,1000); }
        // Deliberately no Engine::reset after loading controls: this is the real
        // NT preset/first-block path, including the previous 350ms glide state.
        const std::size_t expected=cvTime ? 104472u : 69648u; // 1451ms *48; +CV gives1.5x.
        std::size_t firstWet=std::numeric_limits<std::size_t>::max();
        constexpr int n=128;
        std::vector<float> b(kNT_lastBus*n,0.0f);
        for(std::size_t cursor=0;cursor<expected+128;cursor+=n) {
            std::fill(b.begin(),b.end(),0.0f);
            if(cursor==0) b[0]=8.0f;
            if(cvTime) for(int k=0;k<n;++k) b[2*n+k]=5.0f;
            step(i.self,b.data(),n/4);
            for(int k=0;k<n;++k) if(cursor+static_cast<std::size_t>(k)>0 &&
                std::fabs(b[12*n+k])>1.0f && firstWet==std::numeric_limits<std::size_t>::max())
                firstWet=cursor+static_cast<std::size_t>(k);
        }
        check(firstWet==expected,"first NT Loop Record latches preset Time plus CV without capturing the previous delay glide");
    }
}
int main() {
    try {
        check(pluginEntry(kNT_selector_version,0)==13,"firmware 1.18 API 13 compatibility");
        check(pluginEntry(kNT_selector_numFactories,0)==1,"factory count");
        check(pluginEntry(kNT_selector_factoryInfo,1)==0,"invalid factory index");
        const auto* f = reinterpret_cast<const _NT_factory*>(pluginEntry(kNT_selector_factoryInfo,0));
        check(f && f->guid == NT_MULTICHAR('E','L','n','t'),"stable GUID");
        check(std::strcmp(f->name,"Coloured_Echos")==0,"exact displayed project name");
        static_assert(Model==6 && Time==7 && Bypass==12 && BitDepth==13 && LoopAction==22 && LimiterOn==23 && Division==42 && FeedbackLPHz==43 && Drift==44 && FeedbackHPHz==45 && LoResEndHz==46 && LoResDecayMs==47 && LoResEnvelopeAmount==48 && RackSampleRateHz==49 && LoResCutoffHz==50 && Voicing==51 && ModulationEnabled==52 && ParameterCount==53,"published parameter IDs remain stable; controls appended");
        check(parameters[Model].max==19 && parameters[Model].def==0 && ARRAY_SIZE(modelNames)==20,
              "20 selectors without changing existing model IDs");
        check(parameters[Feedback].def==450 && echolab::Parameters{}.feedback==.45f && parameters[ModRate].def==100 && parameters[ModDepth].def==600 && parameters[ExciterAmount].def==100 &&
              parameters[ExciterTuneHz].def==4000 && parameters[Swell].def==250 && parameters[LimiterReleaseMs].def==150 &&
              parameters[LimiterCeiling].max==891 && parameters[LimiterCeiling].def==891 &&
              parameters[FeedbackLPHz].def==12000 && parameters[Drift].def==250 && parameters[FeedbackHPHz].def==80 &&
              parameters[LoResEndHz].def==1200 && parameters[LoResDecayMs].def==1500 && parameters[LoResEnvelopeAmount].def==1000 && parameters[RackSampleRateHz].def==16000 &&
              parameters[LoResCutoffHz].def==2000 && parameters[Voicing].def==0 && parameters[ModulationEnabled].def==1,
              "published musical defaults, Lo Res envelope and -1dB NT maximum");
        check(std::strcmp(modelNames[8],"Sweep Down (provisional)")==0 && std::strcmp(modelNames[18],"Sweep Up (provisional)")==0,
              "Sweep Down keeps ID8 and Sweep Up appends ID18");
        check(std::strcmp(modelNames[19],"Tube A04 (provisional)")==0 && parameters[Mod1Destination].max==8 && parameters[Mod2Destination].max==8 &&
              std::strcmp(destinationNames[8],"Fe Lo Res")==0,"Tube A04 and Fe destination are appended without changing earlier selectors");
        check(std::strcmp(modelNames[0],"Digital control")==0 &&
              std::strcmp(modelNames[1],"Analog (provisional)")==0 &&
              std::strcmp(modelNames[2],"Tube B06 (provisional)")==0 &&
              std::strcmp(modelNames[3],"Ping Pong (provisional)")==0 &&
              std::strcmp(modelNames[4],"Multi-Head (provisional)")==0,"stable and explicit model labels");
        int32_t a=1,b=8;
        _NT_algorithmRequirements low{},high{};
        calculateRequirements(low,&a); calculateRequirements(high,&b);
        check(high.dram>low.dram && low.numParameters==ParameterCount,"bounded spec memory");
        routing();
        for(int model=0;model<echolab::modelCount;++model) sharedCoreAndInstances(model);
        statefulModelAliases(); analyticalNewModelRouting(); bypassAndNonfinite(); appendedControls();
        dynamicPagesAndControls();finalBusLimiter();singleMusicalStage();clockAndCvAliasing();modulationCvAliasing();depthCvAndWheelRouting();
        digitalRackInternalRate();loresFeMappings();firstPresetLoopLength();
        std::cout << "NT adapter: complete catalog, appended controls, analytical ping-pong/multi-head, stateful bus aliases, modes, None, bounds, instance isolation, core parity passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
