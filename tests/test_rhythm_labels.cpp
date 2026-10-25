#include "nt/rhythm_labels.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

static void check(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
static void expect(int pattern,bool synced,int division,const char* wanted) {
    char text[64];
    const auto length=echolab_nt::formatTapPattern(pattern,synced,division,text);
    check(std::strcmp(text,wanted)==0,wanted);
    check(length==std::strlen(wanted),"label length excludes NUL");
    check(length<sizeof(text),"complete label fits NT parameter buffer");
}

static void musicalTimes() {
    // Synthetic arithmetic checks: actual tap positions in a quarter-note span.
    const char* quarter[]={
        "Triplet: 1/8T 1/4T 1/4", "Four: 1/16 1/8 1/8D 1/4",
        "Binary: 1/8 1/4", "Dotted: 3/32 1/8D 1/4",
        "Golden: 0.382x 0.618x 1x span", "Gallop: 1/16 1/8D 1/4",
        "Cluster: 1/8D 7/32 1/4", "Single: 1/4",
        "Poly 3:2: 1/8T 1/8 1/4T 1/4", "Poly 4:3: 1/16 1/8T 1/8 1/4T 1/8D 1/4",
        "Poly 5:4: 1/20 1/16 1/10 1/8 3/20 1/8D 1/5 1/4"
    };
    for(int pattern=0;pattern<static_cast<int>(echolab::multitap::patternCount);++pattern) expect(pattern,true,2,quarter[pattern]);
    expect(0,true,5,"Triplet: 1/8 1/4 1/4D");
    expect(1,true,5,"Four: 3/32 1/8D 9/32 1/4D");
    expect(0,true,7,"Triplet: 1/18 1/9 1/4T");
    expect(1,true,7,"Four: 1/16T 1/8T 1/8 1/4T");
    expect(1,true,8,"Four: 1/48 1/16T 1/16 1/8T");
    const char* divisions[]={"Single: 1/1","Single: 1/2","Single: 1/4","Single: 1/8","Single: 1/16",
                             "Single: 1/4D","Single: 1/8D","Single: 1/4T","Single: 1/8T","Single: 1/16T"};
    for(int division=0;division<10;++division) expect(7,true,division,divisions[division]);
}
static void freeTimingAndFallbacks() {
    expect(0,false,2,"Triplet: 1/3 2/3 1 span");
    expect(1,false,5,"Four: 1/4 1/2 3/4 1 span");
    expect(3,false,7,"Dotted: 3/8 3/4 1 span");
    expect(4,false,2,"Golden: 0.382x 0.618x 1x span");
    expect(4,true,5,"Golden: 0.382x 0.618x 1x span");
    expect(6,false,0,"Cluster: 3/4 7/8 1 span");
    expect(7,false,9,"Single: 1 span");
    expect(8,false,2,"Poly 3:2: 1/3 1/2 2/3 1 span");
    expect(9,false,2,"Poly 4:3: 1/4 1/3 1/2 2/3 3/4 1 span");
    expect(10,false,2,"Poly 5:4: 1/5 1/4 2/5 1/2 3/5 3/4 4/5 1 span");
    expect(10,true,0,"Poly 5:4: 1/5 1/4 2/5 1/2 3/5 3/4 4/5 1/1");
    expect(10,true,7,"Poly 5:4: 1/30 1/16T 1/15 1/8T 1/10 1/8 2/15 1/4T");
    expect(-1,true,2,"Triplet: 1/8T 1/4T 1/4");
    expect(11,false,2,"Triplet: 1/3 2/3 1 span");
    expect(7,true,-1,"Single: 1/1");
    expect(7,true,10,"Single: 1/16T");
}
static void boundedBuffers() {
    for(int pattern=0;pattern<static_cast<int>(echolab::multitap::patternCount);++pattern)
    for(bool synced : {false,true}) for(int division=0;division<10;++division) {
        char full[256];
        const auto fullLength=echolab_nt::formatTapPattern(pattern,synced,division,full,sizeof(full));
        check(fullLength<64,"all complete pattern labels fit 64 bytes");
        for(std::size_t capacity=0;capacity<=64;++capacity) {
            char guarded[66];std::memset(guarded,'#',sizeof(guarded));
            const auto length=echolab_nt::formatTapPattern(pattern,synced,division,guarded+1,capacity);
            check(guarded[0]=='#' && guarded[capacity+1]=='#',"formatter preserves both buffer canaries");
            if(capacity==0) check(length==0 && guarded[1]=='#',"zero capacity performs no write");
            else {
                const auto expected=fullLength<capacity ? fullLength : capacity-1;
                check(length==expected && guarded[length+1]=='\0',"truncated labels return stored length and terminate");
                check(std::memcmp(guarded+1,full,length)==0,"truncation keeps the exact label prefix");
            }
        }
    }
    check(echolab_nt::formatTapPattern(0,true,2,nullptr,64)==0,"null buffer is accepted");
    check(echolab_nt::formatTapPattern(0,true,2,nullptr,0)==0,"null zero-capacity buffer is accepted");
}
int main() {
    try {
        musicalTimes();freeTimingAndFallbacks();boundedBuffers();
        std::cout << "Rhythm labels: note arithmetic, free spans, Golden, aliases and bounded buffers passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
}
