#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

struct AnnaMotion {
    float x = 0, y = 0;

    bool dizzy(uint32_t now) const { return shaken && now - dizzyAt < 1800; }
    bool recentlyMoved(uint32_t now) const { return initialized && now - lastMotion < 50; }

    void update(uint32_t now, float ax, float ay, float az,
                float pitch = 0, float roll = 0, float yaw = 0) {
        if (!initialized) {
            gx = neutralX = ax;
            gy = neutralY = ay;
            gz = az;
            lastMotion = now;
            initialized = true;
        }
        gx += .12f * (ax - gx);
        gy += .12f * (ay - gy);
        gz += .12f * (az - gz);
        const float dx = ax - gx, dy = ay - gy, dz = az - gz;
        const float acceleration = dx * dx + dy * dy + dz * dz;
        const float gravity = ax * ax + ay * ay + az * az;
        if (acceleration > .012f || gravity < .80f || gravity > 1.21f ||
            pitch * pitch + roll * roll + yaw * yaw > 225.f) {
            lastMotion = now;
        } else if (now - lastMotion > 600) {
            // A stable grip becomes neutral, whether the face is upright or flat.
            neutralX += .16f * (gx - neutralX);
            neutralY += .16f * (gy - neutralY);
        }
        x += .31f * (std::clamp((neutralX - gx) * 138.f - dx * 195.f + roll * .18f + yaw * .24f,
                                -105.f, 105.f) - x);
        y += .31f * (std::clamp((neutralY - gy) * 95.f - dy * 145.f - pitch * .21f,
                                -62.f, 62.f) - y);
        if (dizzy(now)) return;
        if (now - lastImpulse > 430) impulses = 0;
        if (impulses && now - lastImpulse < 100) return;
        if (acceleration < .48f * .48f) return;
        if (impulses && dx * lastX + dy * lastY + dz * lastZ > -.18f) return;
        lastImpulse = now;
        lastX = dx; lastY = dy; lastZ = dz;
        if (++impulses >= 4) {
            dizzyAt = now;
            shaken = true;
            impulses = 0;
        }
    }

private:
    float gx = 0, gy = 0, gz = 1;
    float neutralX = 0, neutralY = 0;
    float lastX = 0, lastY = 0, lastZ = 0;
    uint32_t lastImpulse = 0, dizzyAt = 0, lastMotion = 0;
    unsigned impulses = 0;
    bool shaken = false, initialized = false;
};
