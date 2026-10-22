#include "pointer.hpp"
#include <linux/input.h>
#include <cassert>
#include <iostream>
int main() {
    NativePointer pointer(100, 50);
    std::vector<SDL_Event> events;
    pointer.translate(EV_REL, REL_X, 1000, events);
    pointer.translate(EV_REL, REL_Y, -1000, events);
    assert(pointer.x == 99 && pointer.y == 0 && events.back().motion.x == 99);
    pointer.translate(EV_KEY, BTN_LEFT, 1, events);
    assert(events.back().type == SDL_MOUSEBUTTONDOWN && pointer.buttons == SDL_BUTTON_LMASK);
    pointer.translate(EV_REL, REL_X, -20, events);
    assert(events.back().motion.state == SDL_BUTTON_LMASK && events.back().motion.x == 79);
    pointer.translate(EV_KEY, BTN_LEFT, 0, events);
    assert(events.back().type == SDL_MOUSEBUTTONUP && pointer.buttons == 0);
    pointer.translate(EV_REL, REL_WHEEL, -2, events);
    assert(events.back().type == SDL_MOUSEWHEEL && events.back().wheel.y == -2);
    auto count = events.size();
    pointer.translate(EV_KEY, KEY_A, 1, events);
    assert(events.size() == count);
    assert(pointer.pump().empty()); // No device scan or grab without native focus.
    std::cout << "Native pointer translation, bounds, drag state and wheel passed\n";
}
