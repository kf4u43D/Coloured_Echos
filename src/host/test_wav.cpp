#include "wav.h"
#include "sha256.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void u16(std::vector<unsigned char>& b, std::uint16_t x) { b.push_back(static_cast<unsigned char>(x)); b.push_back(static_cast<unsigned char>(x >> 8)); }
void u32(std::vector<unsigned char>& b, std::uint32_t x) { u16(b,static_cast<std::uint16_t>(x)); u16(b,static_cast<std::uint16_t>(x >> 16)); }
void tag(std::vector<unsigned char>& b,const char* p) { for (unsigned i=0;i<4;++i) b.push_back(static_cast<unsigned char>(p[i])); }
std::vector<unsigned char> fixture(unsigned bits,unsigned format,unsigned channels,bool extensible=false) {
    std::vector<unsigned char> b;
    tag(b,"RIFF"); u32(b,0); tag(b,"WAVE");
    tag(b,"JUNK"); u32(b,1); b.push_back(123); b.push_back(0);
    tag(b,"fmt "); u32(b,extensible ? 40 : 16);
    u16(b,static_cast<std::uint16_t>(extensible ? 0xfffe : format)); u16(b,static_cast<std::uint16_t>(channels));
    u32(b,48000); u32(b,48000 * channels * bits/8); u16(b,static_cast<std::uint16_t>(channels * bits/8)); u16(b,static_cast<std::uint16_t>(bits));
    if (extensible) { u16(b,22); u16(b,static_cast<std::uint16_t>(bits)); u32(b,0); u32(b,format); u16(b,0); u16(b,0x10); const unsigned char suffix[8]={0x80,0,0,0xaa,0,0x38,0x9b,0x71}; b.insert(b.end(),suffix,suffix+8); }
    tag(b,"data"); u32(b,6 * channels * bits/8);
    for (unsigned i=0;i<6;++i) for (unsigned c=0;c<channels;++c) {
        if (format == 3) {
            const float values[6] = {-2,-0.5f,0,0.25f,1,1.75f};
            std::uint32_t raw; std::memcpy(&raw,&values[i],4); u32(b,raw);
        } else if (bits == 8) {
            const unsigned char raw[6] = {0,64,128,160,255,128};
            b.push_back(raw[i]);
        } else {
            const std::uint32_t peak = (std::uint32_t(1) << (bits-1));
            const std::uint32_t raw[6] = {peak,peak + peak/2,0,peak/4,peak-1,0};
            for (unsigned byte=0;byte<bits/8;++byte) b.push_back(static_cast<unsigned char>(raw[i] >> (8*byte)));
        }
    }
    const auto size=static_cast<std::uint32_t>(b.size()-8);
    for (unsigned i=0;i<4;++i) b[4+i]=static_cast<unsigned char>(size >> (i*8));
    return b;
}
void writeBytes(const std::filesystem::path& path,const std::vector<unsigned char>& b) {
    std::ofstream f(path,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));
    if (!f) throw std::runtime_error("fixture write failed");
}
void mustReject(const std::filesystem::path& path,const std::vector<unsigned char>& b,const char* message) {
    writeBytes(path,b);
    bool rejected=false;
    try { (void)echolab::host::readWave(path.string()); } catch (const std::exception&) { rejected=true; }
    check(rejected,message);
}
}

int main() {
    namespace fs = std::filesystem;
    const auto dir=fs::temp_directory_path() / ("echolab-wav-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(dir)) return 2;
    const auto input=dir / "fixture.wav", output=dir / "output.wav";
    try {
        const auto hash=[](const std::string& text) { return echolab::host::sha256Bytes(reinterpret_cast<const unsigned char*>(text.data()),text.size()); };
        check(hash("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA-256 empty known vector");
        check(hash("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 abc known vector");
        check(hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","SHA-256 multiblock known vector");
        check(hash(std::string(1000000,'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA-256 million-byte known vector");
        for (unsigned bits : {8U,16U,24U,32U}) for (unsigned channels : {1U,2U}) {
            writeBytes(input,fixture(bits,1,channels));
            const auto w=echolab::host::readWave(input.string());
            check(w.sampleRate == 48000 && w.channels == channels && w.frames() == 6,"PCM metadata decoded");
            for (unsigned c=0;c<channels;++c) {
                check(w.samples[c] == -1 && w.samples[channels+c] == -0.5f && w.samples[2*channels+c] == 0 && w.samples[3*channels+c] == 0.25f,"PCM unsigned offset/sign extension, silence and absolute gain");
                const float expected=static_cast<float>(1.0-std::ldexp(1.0,-static_cast<int>(bits-1)));
                check(w.samples[4*channels+c] == expected,"PCM maximum positive value decoded without normalization");
            }
        }
        for (unsigned channels : {1U,2U}) for (bool extensible : {false,true}) {
            writeBytes(input,fixture(32,3,channels,extensible));
            const auto w=echolab::host::readWave(input.string());
            check(w.samples[0] == -2 && w.samples[5*channels] == 1.75f,"float WAV preserves samples above full scale");
            echolab::host::writeWave(output.string(),w);
            const auto roundtrip=echolab::host::readWave(output.string());
            check(roundtrip.samples == w.samples && roundtrip.sampleRate == w.sampleRate && roundtrip.channels == w.channels,"float WAV exact round trip");
        }
        writeBytes(input,fixture(24,1,2,true));
        check(echolab::host::readWave(input.string()).samples[0] == -1,"extensible PCM24 supported");
        const auto original=fixture(24,1,2,true);
        check(echolab::host::sha256File(input.string()) == echolab::host::sha256Bytes(original.data(),original.size()),"streamed file SHA-256 matches bytes");
        auto invalid=fixture(16,1,1); invalid.pop_back();
        mustReject(input,invalid,"truncated RIFF rejected");
        invalid=fixture(16,1,1); invalid[12+4]=0xff;
        mustReject(input,invalid,"oversized unknown chunk rejected");
        invalid=fixture(16,1,3);
        mustReject(input,invalid,"unsupported channel count rejected");
        invalid=fixture(32,3,1); const float nan=std::numeric_limits<float>::quiet_NaN(); std::memcpy(invalid.data()+invalid.size()-4,&nan,4);
        mustReject(input,invalid,"nonfinite float WAV rejected");
        invalid=fixture(16,1,1); invalid[0]='X';
        mustReject(input,invalid,"non-RIFF input rejected");
    } catch (const std::exception& e) { ++failures; std::cerr << e.what() << '\n'; }
    fs::remove(input); fs::remove(output); fs::remove(dir);
    if (failures) return 1;
    std::cout << "PCM8/16/24/32 mono/stereo, float/extensible, absolute gain, roundtrip and malformed WAV checks passed\n";
    return 0;
}
