#include "../firmware/src/motion.h"
#include <cassert>

int main() {
    AnnaMotion upright;
    for (unsigned t = 0; t <= 2000; t += 40) upright.update(t, -.62f, -.76f, .19f);
    assert(std::abs(upright.x) < 6 && std::abs(upright.y) < 6 && !upright.dizzy(2000));
    assert(!upright.recentlyMoved(2000));
    upright.update(2040, -.62f, -.76f, .19f, 0, 0, 180);
    assert(upright.x > 10);
    assert(upright.recentlyMoved(2040));
    upright.update(2080, -.22f, -.76f, .19f);
    assert(upright.x < -10);
    for (unsigned t = 2120; t <= 5000; t += 40) upright.update(t, -.32f, -.93f, .19f);
    assert(std::abs(upright.x) < 6 && std::abs(upright.y) < 6);

    AnnaMotion bump;
    bump.update(0, 0, 0, 1);
    bump.update(120, 1.4f, 0, 1);
    bump.update(240, 0, 0, 1);
    assert(!bump.dizzy(250));

    AnnaMotion shake;
    shake.update(0, 0, 0, 1);
    for (unsigned t = 40; t <= 620; t += 40) {
        shake.update(t, (t / 120) % 2 ? 1.3f : -1.3f, 0, 1);
    }
    assert(shake.dizzy(620));
    assert(!shake.dizzy(3000));

    AnnaMotion sway;
    sway.update(0, 0, 0, 1);
    sway.update(25, .30f, 0, 1);
    assert(sway.x < -15 && !sway.dizzy(25));
    for (unsigned t = 50; t < 300; t += 25) sway.update(t, -.30f, 0, 1);
    assert(sway.x > 20);
}
