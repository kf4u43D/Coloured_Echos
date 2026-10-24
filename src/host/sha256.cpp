#include "sha256.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace echolab::host {
namespace {
constexpr std::uint32_t constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
std::uint32_t rotate(std::uint32_t x,unsigned n) { return (x >> n) | (x << (32-n)); }
class Hash {
    std::uint32_t state_[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned char buffer_[64]{};
    std::size_t used_ = 0;
    std::uint64_t bytes_ = 0;
    void transform() {
        std::uint32_t w[64];
        for (unsigned i=0;i<16;++i) {
            const auto* p=buffer_+i*4;
            w[i]=(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];
        }
        for (unsigned i=16;i<64;++i) {
            const auto s0=rotate(w[i-15],7)^rotate(w[i-15],18)^(w[i-15]>>3);
            const auto s1=rotate(w[i-2],17)^rotate(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3];
        auto e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for (unsigned i=0;i<64;++i) {
            const auto sum1=rotate(e,6)^rotate(e,11)^rotate(e,25);
            const auto choose=(e&f)^(~e&g);
            const auto temp1=h+sum1+choose+constants[i]+w[i];
            const auto sum0=rotate(a,2)^rotate(a,13)^rotate(a,22);
            const auto majority=(a&b)^(a&c)^(b&c);
            h=g;g=f;f=e;e=d+temp1;d=c;c=b;b=a;a=temp1+sum0+majority;
        }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;
        state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
public:
    void update(const unsigned char* bytes,std::size_t count) {
        bytes_+=count;
        while (count) {
            const auto copied=std::min(count,64-used_);
            std::copy(bytes,bytes+copied,buffer_+used_);
            used_+=copied;bytes+=copied;count-=copied;
            if (used_ == 64) { transform(); used_=0; }
        }
    }
    std::string finish() {
        const std::uint64_t bits=bytes_*8;
        const unsigned char marker=0x80,zero=0;
        update(&marker,1);
        while (used_ != 56) update(&zero,1);
        unsigned char length[8];
        for (unsigned i=0;i<8;++i) length[i]=static_cast<unsigned char>(bits>>(56-i*8));
        update(length,8);
        std::string result;
        result.reserve(64);
        const char* hex="0123456789abcdef";
        for (auto word:state_) for (int shift=28;shift>=0;shift-=4) result.push_back(hex[(word>>shift)&15]);
        return result;
    }
};
}
std::string sha256Bytes(const unsigned char* bytes,std::size_t count) {
    if (!bytes && count) throw std::runtime_error("Null SHA-256 input");
    Hash hash;hash.update(bytes,count);return hash.finish();
}
std::string sha256File(const std::string& path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open file for SHA-256: "+path);
    Hash hash;
    std::array<unsigned char,65536> bytes{};
    while (input) {
        input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        hash.update(bytes.data(),static_cast<std::size_t>(input.gcount()));
    }
    if (!input.eof()) throw std::runtime_error("Read failed during SHA-256: "+path);
    return hash.finish();
}
}
