// Tests of the touch controls.

#import "ARTest.h"
#include "ar_pads.h"
#include "ar_run.h"

AR_TEST(test_pads_put_the_game_at_853_by_640_on_a_4s) {
    ARPads p;
    ar_pads_layout(&p, 960, 640);
    AR_EQUAL_INT(p.game_w, 853);
    AR_EQUAL_INT(p.game_h, 640);
    AR_EQUAL_INT(p.game_x, 53);
    AR_EQUAL_INT(p.game_y, 0);
}

AR_TEST(test_pads_send_eight_directions) {
    ARPads p;
    ar_pads_layout(&p, 960, 640);
    uint8_t keys[256] = {0};
    float r = p.dpad_radius * 0.8f;
    float xs[1] = {p.dpad_x + r}, ys[1] = {p.dpad_y};
    ar_pads_keys(&p, xs, ys, 1, keys);
    AR_EXPECT(keys[AR_VK_RIGHT] && !keys[AR_VK_UP] && !keys[AR_VK_DOWN]);
    // Up and to the left, at 135 degrees.
    xs[0] = p.dpad_x - r * 0.7f;
    ys[0] = p.dpad_y - r * 0.7f;
    ar_pads_keys(&p, xs, ys, 1, keys);
    AR_EXPECT(keys[AR_VK_UP] && keys[AR_VK_LEFT] && !keys[AR_VK_RIGHT]);
    // The center is a dead zone.
    xs[0] = p.dpad_x + 1;
    ys[0] = p.dpad_y;
    ar_pads_keys(&p, xs, ys, 1, keys);
    AR_EXPECT(!keys[AR_VK_UP] && !keys[AR_VK_LEFT] && !keys[AR_VK_RIGHT] && !keys[AR_VK_DOWN]);
}

AR_TEST(test_pads_hold_two_buttons_with_two_touches) {
    ARPads p;
    ar_pads_layout(&p, 960, 640);
    uint8_t keys[256] = {0};
    float xs[2] = {p.buttons[0].x, p.buttons[1].x + 5}, ys[2] = {p.buttons[0].y, p.buttons[1].y};
    ar_pads_keys(&p, xs, ys, 2, keys);
    AR_EXPECT(keys['Z'] && keys['X'] && !keys['C']);
    // A touch that goes away lets the key up.
    ar_pads_keys(&p, xs, ys, 1, keys);
    AR_EXPECT(keys['Z'] && !keys['X']);
}

AR_TEST(test_pads_ignore_a_touch_in_the_middle_of_the_game) {
    ARPads p;
    ar_pads_layout(&p, 960, 640);
    uint8_t keys[256] = {0};
    float xs[1] = {480}, ys[1] = {200};
    ar_pads_keys(&p, xs, ys, 1, keys);
    int any = 0;
    for (int k = 0; k < 256; k++) any |= keys[k];
    AR_EQUAL_INT(any, 0);
}

AR_TEST(test_pads_draw_inside_the_screen) {
    ARPads p;
    ar_pads_layout(&p, 960, 640);
    uint8_t keys[256] = {0};
    static ARVertex v[4096];
    int n = ar_pads_draw(&p, keys, v);
    AR_EXPECT(n > 0 && n % 3 == 0);
    int outside = 0;
    for (int i = 0; i < n; i++) outside += v[i].x < 0 || v[i].x > 960 || v[i].y < 0 || v[i].y > 640;
    AR_EQUAL_INT(outside, 0);
}
