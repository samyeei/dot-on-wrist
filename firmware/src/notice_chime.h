#pragma once
#include <algorithm>
#include <cmath>
#include "playback.h"

inline bool queueNoticeChime(AnnaPlayback& playback) {
    constexpr size_t rate = 16000;
    constexpr size_t warmup = rate / 10;
    constexpr size_t duration = rate / 2;
    constexpr float twoPi = 6.283185307f;
    int16_t packet[AnnaPlayback::packetSamples]{};
    for (size_t offset = 0; offset < duration; offset += AnnaPlayback::packetSamples) {
        for (size_t i = 0; i < AnnaPlayback::packetSamples; ++i) {
            const size_t sample = offset + i;
            if (sample < warmup) continue;
            const float t = float(sample - warmup) / rate;
            const float strike = std::min(t / .003f, 1.f);
            const float tail = std::clamp((.37f - t) / .05f, 0.f, 1.f);
            // The brighter metallic partials fade first after the single strike.
            const float bell = 15000.f * std::exp(-10.f * t) * std::sin(twoPi * 1260.f * t)
                             + 8100.f * std::exp(-23.f * t) * std::sin(twoPi * 2340.f * t)
                             + 4100.f * std::exp(-42.f * t) * std::sin(twoPi * 3520.f * t);
            packet[i] = int16_t(strike * tail * bell);
        }
        if (!playback.append(packet)) return false;
    }
    playback.finish();
    return true;
}
