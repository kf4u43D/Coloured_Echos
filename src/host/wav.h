#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace echolab::host {
struct Wave {
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::vector<float> samples; // Interleaved, amplitude preserved.
    std::size_t frames() const { return channels ? samples.size() / channels : 0; }
};
Wave readWave(const std::string& path);
void writeWave(const std::string& path, const Wave& wave);
} // namespace echolab::host
