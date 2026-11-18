#include "../firmware/src/playback.h"
#include <cassert>

int main() {
    AnnaPlayback queue;
    int16_t storage[AnnaPlayback::capacity][AnnaPlayback::chunkSamples]{};
    int16_t packet[AnnaPlayback::packetSamples]{};
    queue.storage = storage;
    for (int n = 0; n < 19; ++n) {
        packet[0] = n;
        assert(queue.append(packet));
    }
    assert(!queue.ready(false));
    packet[0] = 19;
    assert(queue.append(packet) && queue.ready(false));
    assert(queue.front()[0] == 0 && queue.front()[320] == 1 && queue.frontSize() == 1600);
    queue.submit();
    queue.submit();
    queue.retire(1);
    assert(queue.front()[0] == 10 && queue.submitted == 1);
    queue.clear();
    assert(queue.append(packet) && !queue.ready(false));
    queue.finish();
    assert(queue.ready(true) && queue.frontSize() == 320);
    queue.clear();
    for (size_t n = 0; n < AnnaPlayback::capacity * 5; ++n) assert(queue.append(packet));
    assert(!queue.append(packet));
    queue.submit();
    queue.retire(0);
    assert(queue.append(packet));
}
