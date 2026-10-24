#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace echolab::segments {
namespace detail {
inline float finiteSample(float sample) noexcept {
    if (!std::isfinite(sample)) return 0.0f;
    return std::max(-1.0e12f, std::min(1.0e12f, sample));
}
inline float finiteFeedback(float feedback) noexcept {
    if (!std::isfinite(feedback)) return 0.0f;
    return std::max(-2.0f, std::min(2.0f, feedback));
}
inline float loopGuard(float sample,float feedback) noexcept {
    const float value=finiteSample(sample);
    return std::fabs(feedback) > 1.0f ? std::max(-4.0f,std::min(4.0f,value)) : value;
}
inline bool usableMemory(float* memory, std::size_t stride) noexcept {
    // The caller provides two channels, each containing stride floats.
    // Reject impossible byte counts before computing any channel offsets.
    return memory && stride &&
        stride <= std::numeric_limits<std::size_t>::max() / (2 * sizeof(float));
}
inline std::size_t boundedFrames(std::size_t requested, std::size_t capacity) noexcept {
    return std::max(std::size_t(1), std::min(requested, capacity));
}
} // namespace detail

// Two banks per channel: capture one complete chunk while playing the previous
// complete chunk backwards. Unwritten storage is never treated as captured audio.
// All memory belongs to the caller; reset and chunk changes only invalidate state.
class ReverseState {
public:
    void reset() noexcept {
        memory_ = nullptr;
        stride_ = capacity_ = chunk_ = playLength_ = position_ = 0;
        recordBank_ = 0;
        playable_ = false;
    }

    void process(float inL, float inR, float feedback, float* memory,
                 std::size_t stride, std::size_t chunkFrames, std::size_t fadeFrames,
                 float& wetL, float& wetR) noexcept {
        process(inL,inR,feedback,memory,stride,chunkFrames,fadeFrames,wetL,wetR,
            [](unsigned,float value) noexcept {return value;});
    }
    template<class FeedbackFilter>
    void process(float inL,float inR,float feedback,float* memory,std::size_t stride,
                 std::size_t chunkFrames,std::size_t fadeFrames,float& wetL,float& wetR,
                 FeedbackFilter feedbackFilter) noexcept {
        wetL = wetR = 0.0f;
        if (!detail::usableMemory(memory,stride) || stride < 2) {
            reset();
            return;
        }
        const std::size_t frames = detail::boundedFrames(chunkFrames,stride / 2);
        if (memory != memory_ || stride != stride_) {
            reset();
            memory_ = memory;
            stride_ = stride;
            capacity_ = stride/2;
        }
        // Time is latched only at a capture boundary. Fixed physical banks keep
        // the completed chunk valid when the next capture has a different size.
        if (position_ == 0) chunk_=frames;
        if (playable_) {
            const std::size_t base=(1u-recordBank_)*capacity_;
            float gain = 1.0f;
            // Keep at least one unattenuated sample even for very short chunks.
            const std::size_t fade = std::min(fadeFrames,(chunk_-1) / 2);
            if (fade) {
                if (position_ < fade) gain = static_cast<float>(position_) / static_cast<float>(fade);
                const std::size_t remaining = chunk_-1-position_;
                if (remaining < fade)
                    gain = std::min(gain,static_cast<float>(remaining) / static_cast<float>(fade));
            }
            if (playLength_ == chunk_) {
                const std::size_t read=base+playLength_-1-position_;
                wetL=detail::finiteSample(memory_[read])*gain;
                wetR=detail::finiteSample(memory_[stride_+read])*gain;
            } else {
                // Stretch the previous complete chunk over the newly latched
                // interval. It is never read beyond its captured valid length.
                const float readPosition=chunk_ <= 1 ? static_cast<float>(playLength_-1) :
                    static_cast<float>(playLength_-1)*(1.0f-static_cast<float>(position_)/static_cast<float>(chunk_-1));
                const std::size_t lower=std::min(playLength_-1,static_cast<std::size_t>(std::max(0.0f,readPosition)));
                const std::size_t upper=std::min(playLength_-1,lower+1);
                const float fraction=std::max(0.0f,std::min(1.0f,readPosition-static_cast<float>(lower)));
                const float l0=detail::finiteSample(memory_[base+lower]);
                const float l1=detail::finiteSample(memory_[base+upper]);
                const float r0=detail::finiteSample(memory_[stride_+base+lower]);
                const float r1=detail::finiteSample(memory_[stride_+base+upper]);
                wetL=(l0+fraction*(l1-l0))*gain;
                wetR=(r0+fraction*(r1-r0))*gain;
            }
        }
        const float gain = detail::finiteFeedback(feedback);
        const std::size_t write = recordBank_ * capacity_ + position_;
        memory_[write] = detail::loopGuard(detail::finiteSample(inL) + gain * feedbackFilter(0,wetL),gain);
        memory_[stride_+write] = detail::loopGuard(detail::finiteSample(inR) + gain * feedbackFilter(1,wetR),gain);
        if (++position_ == chunk_) {
            position_ = 0;
            playLength_=chunk_;
            recordBank_ = 1u-recordBank_;
            playable_ = true;
        }
    }

private:
    float* memory_ = nullptr;
    std::size_t stride_ = 0, capacity_ = 0, chunk_ = 0, playLength_ = 0, position_ = 0;
    unsigned recordBank_ = 0;
    bool playable_ = false;
};

// Actions: 0 Stop, 1 Record, 2 Play, 3 Overdub, 4 Clear. Record starts once on
// entry, captures a fixed requested length, and then auto-plays while held at 1.
// Time changes affect the next recording. Stop/Play preserves the play position.
// Overdub emits the old loop sample and stores old*feedback+input for the next
// pass. An incomplete/cleared loop is silent until a full recording completes.
class LoopState {
public:
    void reset() noexcept {
        memory_ = nullptr;
        stride_ = length_ = cursor_ = 0;
        lastAction_ = 0;
        valid_ = recording_ = false;
    }

    void process(float inL, float inR, int action, float feedback, float* memory,
                 std::size_t stride, std::size_t desiredFrames,
                 float& wetL, float& wetR) noexcept {
        process(inL,inR,action,feedback,memory,stride,desiredFrames,wetL,wetR,
            [](unsigned,float value) noexcept {return value;});
    }
    template<class FeedbackFilter>
    void process(float inL,float inR,int action,float feedback,float* memory,std::size_t stride,
                 std::size_t desiredFrames,float& wetL,float& wetR,FeedbackFilter feedbackFilter) noexcept {
        wetL = wetR = 0.0f;
        if (!detail::usableMemory(memory,stride)) {
            reset();
            return;
        }
        if (memory != memory_ || stride != stride_) {
            reset();
            memory_ = memory;
            stride_ = stride;
        }
        if (action < 0 || action > 4) action = 0;
        const bool beginRecord = action == 1 && lastAction_ != 1;
        lastAction_ = action;
        if (action == 4) {
            length_ = cursor_ = 0;
            valid_ = recording_ = false;
            return;
        }
        if (action == 0) return;
        if (beginRecord) {
            length_ = detail::boundedFrames(desiredFrames,stride_);
            cursor_ = 0;
            valid_ = false;
            recording_ = true;
        }
        if (action == 1 && recording_) {
            memory_[cursor_] = detail::loopGuard(inL,feedback);
            memory_[stride_+cursor_] = detail::loopGuard(inR,feedback);
            if (++cursor_ == length_) {
                cursor_ = 0;
                recording_ = false;
                valid_ = true;
            }
            return;
        }
        if (action != 1) recording_ = false;
        if (!valid_) return;
        wetL = detail::finiteSample(memory_[cursor_]);
        wetR = detail::finiteSample(memory_[stride_+cursor_]);
        if (action == 3) {
            const float gain = detail::finiteFeedback(feedback);
            memory_[cursor_] = detail::loopGuard(gain * feedbackFilter(0,wetL) + detail::finiteSample(inL),gain);
            memory_[stride_+cursor_] = detail::loopGuard(gain * feedbackFilter(1,wetR) + detail::finiteSample(inR),gain);
        }
        if (++cursor_ == length_) cursor_ = 0;
    }

private:
    float* memory_ = nullptr;
    std::size_t stride_ = 0, length_ = 0, cursor_ = 0;
    int lastAction_ = 0;
    bool valid_ = false, recording_ = false;
};

} // namespace echolab::segments
