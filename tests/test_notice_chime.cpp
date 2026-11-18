#include "../firmware/src/notice_chime.h"
#include <cassert>
#include <cstdlib>

int main() {
    AnnaPlayback playback;
    int16_t storage[AnnaPlayback::capacity][AnnaPlayback::chunkSamples]{};
    playback.storage = storage;
    assert(queueNoticeChime(playback) && playback.ready(true));
    size_t samples = 0;
    int peak = 0;
    while (!playback.empty()) {
        const auto* chunk = playback.front();
        const auto size = playback.frontSize();
        for (size_t i = 0; i < size; ++i) {
            if (samples < 1600) assert(chunk[i] == 0);
            peak = std::max(peak, std::abs(int(chunk[i])));
            ++samples;
        }
        playback.submit();
        playback.retire(0);
    }
    assert(samples == 8000);
    assert(peak >= 12000 && peak < 30000);
}
