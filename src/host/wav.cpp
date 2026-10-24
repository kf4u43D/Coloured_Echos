#include "wav.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace echolab::host {
namespace {
std::uint16_t u16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1]) << 8));
}
std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
void put16(std::ostream& stream, std::uint16_t x) {
    const char p[2] = {static_cast<char>(x), static_cast<char>(x >> 8)};
    stream.write(p, 2);
}
void put32(std::ostream& stream, std::uint32_t x) {
    const char p[4] = {static_cast<char>(x), static_cast<char>(x >> 8),
                       static_cast<char>(x >> 16), static_cast<char>(x >> 24)};
    stream.write(p, 4);
}
}

Wave readWave(const std::string& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Cannot open input WAV: " + path);
    const auto fileSize = stream.tellg();
    if (fileSize < 12 || static_cast<std::uint64_t>(fileSize) > 0xffffffffULL + 8ULL)
        throw std::runtime_error("Unsupported WAV size (RIFF only, no RF64)");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(fileSize));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream || std::memcmp(bytes.data(), "RIFF", 4) ||
        std::memcmp(bytes.data() + 8, "WAVE", 4))
        throw std::runtime_error("Input is not a little-endian RIFF/WAVE file");
    const std::uint64_t riffEnd64 = 8ULL + u32(bytes.data() + 4);
    if (riffEnd64 > bytes.size() || riffEnd64 < 12)
        throw std::runtime_error("Truncated/invalid RIFF size");
    const std::size_t riffEnd = static_cast<std::size_t>(riffEnd64);
    std::uint16_t format = 0, channels = 0, bits = 0, align = 0;
    std::uint32_t fs = 0;
    const unsigned char* data = nullptr;
    std::size_t dataSize = 0;
    bool foundFormat = false;
    for (std::size_t pos = 12; pos < riffEnd;) {
        if (riffEnd - pos < 8) throw std::runtime_error("Truncated WAV chunk header");
        const std::size_t size = u32(bytes.data() + pos + 4);
        const std::size_t body = pos + 8;
        if (size > riffEnd - body) throw std::runtime_error("Truncated WAV chunk body");
        if (std::memcmp(bytes.data() + pos, "fmt ", 4) == 0) {
            if (foundFormat || size < 16) throw std::runtime_error("Invalid/duplicate WAV fmt chunk");
            const auto* p = bytes.data() + body;
            format = u16(p); channels = u16(p+2); fs = u32(p+4);
            align = u16(p+12); bits = u16(p+14);
            if (format == 0xfffe) {
                const unsigned char suffix[14] = {0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if (size < 40 || u16(p+16) < 22 ||
                    std::memcmp(p+26, suffix, 14) != 0 || u16(p+18) != bits)
                    throw std::runtime_error("Unsupported extensible WAV subformat/valid bits");
                format = u16(p+24);
            }
            foundFormat = true;
        } else if (std::memcmp(bytes.data() + pos, "data", 4) == 0) {
            if (data) throw std::runtime_error("Multiple WAV data chunks are unsupported");
            data = bytes.data() + body;
            dataSize = size;
        }
        const std::size_t padded = size + (size & 1U);
        if (padded > riffEnd - body) throw std::runtime_error("Missing odd WAV chunk padding");
        pos = body + padded;
    }
    if (!foundFormat || !data || (channels != 1 && channels != 2) ||
        fs < 8000 || fs > 384000 ||
        !((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
          (format == 3 && bits == 32)) ||
        align != channels * (bits / 8) || dataSize % align)
        throw std::runtime_error("WAV requires mono/stereo PCM8/16/24/32 or float32, Fs 8-384 kHz");
    Wave wave;
    wave.sampleRate = fs;
    wave.channels = channels;
    wave.samples.resize(dataSize / (bits / 8));
    for (std::size_t i = 0; i != wave.samples.size(); ++i) {
        const auto* p = data + i * (bits / 8);
        float value = 0;
        if (format == 3) {
            const std::uint32_t raw = u32(p);
            std::memcpy(&value, &raw, sizeof(value));
            if (!std::isfinite(value)) throw std::runtime_error("WAV contains a non-finite float sample");
        } else if (bits == 8) {
            // RIFF PCM8 is unsigned: 128 is silence, 0 is -1, 255 is 127/128.
            value = static_cast<float>(static_cast<int>(p[0]) - 128) / 128.0f;
        } else if (bits == 16) {
            const std::uint32_t raw = u16(p);
            const std::int32_t signedValue = raw >= 0x8000U ? static_cast<std::int32_t>(raw) - 65536 : static_cast<std::int32_t>(raw);
            value = static_cast<float>(signedValue / 32768.0);
        } else if (bits == 24) {
            const std::uint32_t raw = p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16);
            const std::int32_t signedValue = raw >= 0x800000U ? static_cast<std::int32_t>(raw) - 16777216 : static_cast<std::int32_t>(raw);
            value = static_cast<float>(signedValue / 8388608.0);
        } else {
            const std::uint32_t raw = u32(p);
            const double signedValue = raw >= 0x80000000U ? static_cast<double>(raw) - 4294967296.0 : static_cast<double>(raw);
            value = static_cast<float>(signedValue / 2147483648.0);
        }
        wave.samples[i] = value;
    }
    return wave;
}

void writeWave(const std::string& path, const Wave& wave) {
    if ((wave.channels != 1 && wave.channels != 2) || !wave.sampleRate ||
        wave.samples.size() % wave.channels || wave.samples.size() > (0xffffffffULL - 50) / 4)
        throw std::runtime_error("Invalid/oversized output WAV");
    for (float sample : wave.samples)
        if (!std::isfinite(sample)) throw std::runtime_error("Cannot write non-finite WAV samples");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Cannot create output WAV: " + path);
    const auto dataBytes = static_cast<std::uint32_t>(wave.samples.size() * 4);
    stream.write("RIFF",4); put32(stream, 50 + dataBytes); stream.write("WAVE",4);
    stream.write("fmt ",4); put32(stream,18); put16(stream,3); put16(stream,wave.channels);
    put32(stream,wave.sampleRate); put32(stream,wave.sampleRate * wave.channels * 4);
    put16(stream,wave.channels * 4); put16(stream,32); put16(stream,0);
    stream.write("fact",4); put32(stream,4); put32(stream,static_cast<std::uint32_t>(wave.frames()));
    stream.write("data",4); put32(stream,dataBytes);
    for (float sample : wave.samples) {
        std::uint32_t raw;
        std::memcpy(&raw,&sample,sizeof(raw));
        put32(stream,raw);
    }
    if (!stream) throw std::runtime_error("Failed while writing WAV: " + path);
}
} // namespace echolab::host
