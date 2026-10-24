#pragma once

#include <cstddef>
#include "clock_sync.h"
#include "../dsp/multitap_patterns.h"

namespace echolab_nt {
namespace rhythm_labels_detail {

class Text {
public:
    Text(char* buffer, std::size_t capacity) noexcept : buffer_(buffer), capacity_(buffer ? capacity : 0) {
        if(capacity_) buffer_[0]='\0';
    }
    void append(char value) noexcept {
        if(size_+1<capacity_) { buffer_[size_++]=value;buffer_[size_]='\0'; }
    }
    void append(const char* value) noexcept { while(*value) append(*value++); }
    void number(unsigned value) noexcept {
        char digits[10];unsigned count=0;
        do { digits[count++]=static_cast<char>('0'+value%10);value/=10; } while(value);
        while(count) append(digits[--count]);
    }
    std::size_t size() const noexcept { return size_; }
private:
    char* buffer_;
    std::size_t capacity_,size_=0;
};

inline unsigned gcd(unsigned a,unsigned b) noexcept {
    while(b) { const unsigned remainder=a%b;a=b;b=remainder; }
    return a;
}

inline void fraction(Text& text,unsigned numerator,unsigned denominator,bool synced) noexcept {
    const unsigned factor=gcd(numerator,denominator);
    numerator/=factor;denominator/=factor;
    if(synced) {
        if(numerator==3 && denominator==8) { text.append("1/4D");return; }
        if(numerator==3 && denominator==16) { text.append("1/8D");return; }
        if(numerator==1 && denominator==6) { text.append("1/4T");return; }
        if(numerator==1 && denominator==12) { text.append("1/8T");return; }
        if(numerator==1 && denominator==24) { text.append("1/16T");return; }
    }
    text.number(numerator);
    if(synced || denominator!=1) { text.append('/');text.number(denominator); }
}

inline unsigned spanUnits(float offset) noexcept {
    return static_cast<unsigned>(offset*24.0f+0.5f);
}

inline bool rationalSpan(float offset) noexcept {
    const float error=offset*24.0f-static_cast<float>(spanUnits(offset));
    return error>=-0.0001f && error<=0.0001f;
}

inline void spanMultiplier(Text& text,float offset) noexcept {
    const unsigned thousandths=static_cast<unsigned>(offset*1000.0f+0.5f);
    text.number(thousandths/1000);
    unsigned remainder=thousandths%1000;
    if(remainder) {
        text.append('.');
        for(unsigned place=100;place && remainder;place/=10) {
            text.append(static_cast<char>('0'+remainder/place));remainder%=place;
        }
    }
    text.append('x');
}

} // namespace rhythm_labels_detail

// Returns stored characters, excluding NUL. Free timing is relative to the full
// Time span; synchronized values are note lengths, not gaps between the taps.
inline std::size_t formatTapPattern(int pattern,bool synced,int division,char* buffer,std::size_t capacity=64) noexcept {
    using namespace rhythm_labels_detail;
    Text text(buffer,capacity);
    const unsigned index=pattern>=0 && pattern<static_cast<int>(echolab::multitap::patternCount) ? static_cast<unsigned>(pattern) : 0;
    static constexpr const char* names[]={"Triplet","Four","Binary","Dotted","Golden","Gallop","Cluster","Single"};
    const auto& taps=echolab::multitap::pattern(index);
    bool rational=true;
    for(unsigned head=0;head<taps.count;++head) rational=rational && rationalSpan(taps.taps[head].offset);
    text.append(names[index]);text.append(": ");
    const unsigned quarterUnits=static_cast<unsigned>(ClockSync::quarterMultiplier(division)*12.0+0.5);
    for(unsigned head=0;head<taps.count;++head) {
        if(head) text.append(' ');
        const float offset=taps.taps[head].offset;
        if(!rational) spanMultiplier(text,offset);
        else if(synced) fraction(text,quarterUnits*spanUnits(offset),12*24*4,true);
        else fraction(text,spanUnits(offset),24,false);
    }
    if(!synced || !rational) text.append(" span");
    return text.size();
}

} // namespace echolab_nt
