#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

struct AnnaPlayback {
    static constexpr size_t packetSamples = 320;
    static constexpr size_t chunkSamples = 5 * packetSamples;
    static constexpr size_t capacity = 120;
    int16_t (*storage)[chunkSamples] = nullptr;
    size_t submitted = 0;

    bool append(const void* packet) {
        if (!storage || pending + submitted == capacity) return false;
        const size_t slot = (head + submitted + pending) % capacity;
        std::memcpy(storage[slot] + filling, packet, packetSamples * sizeof(int16_t));
        filling += packetSamples;
        if (filling == chunkSamples) finish();
        return true;
    }

    void finish() {
        if (!filling) return;
        lengths[(head + submitted + pending) % capacity] = filling;
        ++pending;
        filling = 0;
    }
    bool ready(bool ending) const { return pending >= 4 || (ending && pending); }
    bool empty() const { return !pending && !submitted && !filling; }
    bool canSubmit() const { return pending && submitted < 2; }
    const int16_t* front() const { return storage[(head + submitted) % capacity]; }
    size_t frontSize() const { return lengths[(head + submitted) % capacity]; }
    void submit() { ++submitted; --pending; }
    void retire(size_t native) {
        while (submitted > native) {
            head = (head + 1) % capacity;
            --submitted;
        }
    }
    void clear() { head = pending = submitted = filling = 0; }

private:
    size_t head = 0, pending = 0, filling = 0;
    size_t lengths[capacity]{};
};
