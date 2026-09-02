#include <assert.h>
#include <stdbool.h>
#include "liquid_glass_motion.h"

static void assert_same_frame(liquid_glass_frame_t first,
                              liquid_glass_frame_t second)
{
    assert(first.x == second.x);
    assert(first.y == second.y);
    assert(first.width == second.width);
    assert(first.height == second.height);
    assert(first.surface_opacity == second.surface_opacity);
    assert(first.border_opacity == second.border_opacity);
    assert(first.content_opacity == second.content_opacity);
}

int main(void)
{
    for (uint8_t selected = 0; selected < LIQUID_GLASS_WINDOW_COUNT; ++selected) {
        bool seen[LIQUID_GLASS_WINDOW_COUNT] = { false };
        for (uint8_t window = 0; window < LIQUID_GLASS_WINDOW_COUNT; ++window) {
            uint8_t rank = liquid_glass_window_rank(window, selected);
            assert(rank < LIQUID_GLASS_WINDOW_COUNT);
            assert(!seen[rank]);
            seen[rank] = true;
        }
    }

    liquid_glass_frame_t front = liquid_glass_frame_for_rank(0);
    liquid_glass_frame_t middle = liquid_glass_frame_for_rank(1);
    liquid_glass_frame_t back = liquid_glass_frame_for_rank(2);
    assert(front.width > middle.width && middle.width > back.width);
    assert(front.height > middle.height && middle.height > back.height);
    assert(front.y < middle.y && middle.y < back.y);
    assert(front.width - middle.width == middle.width - back.width);
    assert(front.width - middle.width == 4);
    assert(middle.width - back.width == 4);
    assert(front.height - middle.height == 2);
    assert(middle.height - back.height == 2);
    assert(middle.width * 100 >= front.width * 98);
    assert(back.width * 100 >= front.width * 96);
    assert(middle.y - front.y == 10);
    assert(back.y - middle.y == 10);
    assert(back.y - front.y == 20);
    int front_bottom = front.y + front.height;
    int middle_bottom = middle.y + middle.height;
    int back_bottom = back.y + back.height;
    assert(middle_bottom - front_bottom >= 8);
    assert(middle_bottom - front_bottom <= 10);
    assert(back_bottom - middle_bottom >= 8);
    assert(back_bottom - middle_bottom <= 10);
    assert(front.surface_opacity == middle.surface_opacity);
    assert(middle.surface_opacity == back.surface_opacity);
    assert(front.border_opacity > middle.border_opacity);
    assert(middle.border_opacity > back.border_opacity);
    assert(front.content_opacity == 255);
    assert(middle.content_opacity == 0 && back.content_opacity == 0);
    assert(liquid_glass_frame_for_rank(99).width == back.width);

    liquid_glass_frame_t direct_mid = liquid_glass_transition_frame(
        front, back, false, LIQUID_GLASS_DEPTH_CROSS_PROGRESS);
    liquid_glass_frame_t wrap_mid = liquid_glass_transition_frame(
        front, back, true, LIQUID_GLASS_DEPTH_CROSS_PROGRESS);
    assert(wrap_mid.y < direct_mid.y);
    assert(wrap_mid.x >= 0);
    assert(wrap_mid.x + wrap_mid.width <= 240);
    assert(wrap_mid.surface_opacity > 0);
    assert(wrap_mid.border_opacity > 0);

    // The front-to-back route is the exact temporal inverse of back-to-front;
    // no endpoint or intermediate frame is created from an invisible state.
    for (int32_t progress = 0;
         progress <= LIQUID_GLASS_MOTION_PROGRESS_MAX;
         progress += 64) {
        liquid_glass_frame_t forward = liquid_glass_transition_frame(
            front, back, true, progress);
        liquid_glass_frame_t backward = liquid_glass_transition_frame(
            back, front, true,
            LIQUID_GLASS_MOTION_PROGRESS_MAX - progress);
        assert_same_frame(forward, backward);
        assert(forward.x >= 0);
        assert(forward.x + forward.width <= 240);
        int horizontal_center_twice = forward.x * 2 + forward.width;
        assert(horizontal_center_twice >= 239 && horizontal_center_twice <= 241);
        assert(forward.surface_opacity == front.surface_opacity);
        assert(forward.border_opacity >= back.border_opacity);
    }

    const uint8_t material_opacity[] = { 52, 67, 84 };
    for (unsigned material = 0;
         material < sizeof(material_opacity) / sizeof(material_opacity[0]);
         ++material) {
        liquid_glass_frame_t material_front = front;
        liquid_glass_frame_t material_back = back;
        material_front.surface_opacity = material_opacity[material];
        material_back.surface_opacity = material_opacity[material];
        for (int32_t progress = 0;
             progress <= LIQUID_GLASS_MOTION_PROGRESS_MAX;
             progress += 64) {
            liquid_glass_frame_t frame = liquid_glass_transition_frame(
                material_front, material_back, true, progress);
            assert(frame.surface_opacity == material_opacity[material]);
        }
    }

    assert_same_frame(
        liquid_glass_transition_frame(front, back, true, -1), front);
    assert_same_frame(
        liquid_glass_transition_frame(front, back, true,
                                      LIQUID_GLASS_MOTION_PROGRESS_MAX + 1),
        back);
    return 0;
}
