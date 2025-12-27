/*
Copyright 2022 @Yowkees
Copyright 2022 MURAOKA Taro (aka KoRoN, @kaoriya)
Copyright 2025 Hiroki Itoh / @ihiroky

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include QMK_KEYBOARD_H

#include "quantum.h"
#include "transactions.h"
#include "raw_hid.h"
#include "tmk_core/protocol/usb_descriptor.h"
#include "os_detection.h"

#define RAW_REPORT_VERSION 1
#define RAW_APP_ID_ZQ         0xFF
#define RAW_REPORT_TYPE_LAYER 0x00

#define DEFAULT_LAYER 0
#define MOUSE_LAYER 4
#define MOUSE_ACTIVATION_THRESHOLD 10
#define MOUSE_BTN1_RETURN_TERM TAPPING_TERM
#define BTN_THRESHOLD 100

const uint16_t MOUSE_ACTIVATION_THRESHOLD_MIN = 5;
const uint16_t MOUSE_ACTIVATION_THRESHOLD_MAX = 155;
const uint16_t MOUSE_ACTIVATION_THRESHOLD_QU = 5;
const uint16_t AUTO_SHIFT_TIMEOUT_MIN = 100;
const uint16_t AUTO_SHIFT_TIMEOUT_MAX = 250;
const uint16_t AUTO_SHIFT_TIMEOUT_QU  = 5;   // Quantization Unit

enum user_keycodes {
    OL_TGL = KEYBALL_SAFE_RANGE, // Toggle OLED on/off ,User 0
    OL_TGLINV,                   // Invert OLED display
    MAT_I5, // Increase mouse_activation_threshold by 5
    MAT_D5, // Decrease mouse_activation_threshold by 5
    AMLY_TGL, // Toggle auto mouse layer
    RT_LY_TGL, // Layer state report toggle
};

enum user_tapdance_keycodes {
    TDKC_QUOTS,
    TDKC_PIPE_TILD,
};

typedef union {
    uint32_t raw;
    struct {
        uint32_t keyball_reserved : 18;
        uint8_t  report_layer_state : 1;
        uint8_t  oled_inversion : 1;
        uint8_t  auto_mouse_layer_enabled : 1;
        uint16_t mouse_activation_threshold : 5;
        uint8_t  autoshift_enabled : 1;
        uint16_t autoshift_timeout : 5;
    };
} user_config_t;

typedef struct {
    bool auto_mouse_layer_enabled;
    uint16_t mouse_activation_threshold;
#ifdef OLED_ENABLE
    bool oled_on;
    uint32_t oled_timer;
    bool oled_inversion;
    bool oled_inversion_changed;
#endif
#if defined(RAW_ENABLE) && defined(HID_REPORT_ENABLE)
    uint8_t last_highest_layer;
    layer_state_t last_layer_state;
    bool raw_hid_layer_report_enabled;
#endif
} user_state_t;

static user_state_t user_state = {0};

typedef struct {
    bool pressed;
    bool held;
    uint16_t press_timer;
    int16_t x_accum;
    int16_t y_accum;
    struct {
        uint16_t forward;
        uint16_t backward;
        uint16_t left;
        uint16_t right;
    } motion_codes;
} tb_gesture_t;

static tb_gesture_t btn2_tb_gesture = {0};
static tb_gesture_t btn3_tb_gesture = {0};

// clang-format off
#define LA_TAB LALT_T(KC_TAB)
#define LS_SPC LSFT_T(KC_SPC)
#define TD_QUOTS  TD(TDKC_QUOTS)
#define TD_PP_TLD TD(TDKC_PIPE_TILD)

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  // keymap for default (VIA)
  [0] = LAYOUT_universal(
    KC_Q     , KC_W     , KC_E     , KC_R     , KC_T     ,                       KC_Y     , KC_U     , KC_I     , KC_O     , KC_P     ,
    KC_A     , KC_S     , KC_D     , KC_F     , KC_G     ,                       KC_H     , KC_J     , KC_K     , KC_L     , KC_ENTER ,
    KC_Z     , KC_X     , KC_C     , KC_V     , KC_B     ,                       KC_N     , KC_M     , KC_COMM  , KC_DOT   , KC_SLSH  ,
    KC_ESC   , LA_TAB   , KC_LGUI  , MO(3)    , LS_SPC   , MO(2)    , MO(1)    , KC_LCTL  , _______  , _______  , _______  , KC_RSFT
  ),

  [1] = LAYOUT_universal(
    KC_1     , KC_2     , KC_3     , KC_4     , KC_5     ,                       KC_F1    , KC_F2    , KC_F3    , KC_F4    , KC_F5    ,
    KC_6     , KC_7     , KC_8     , KC_9     , KC_0     ,                       KC_F6    , KC_F7    , KC_F8    , KC_F9    , KC_F10   ,
    KC_MINUS ,S(KC_MINS), KC_INT3  , KC_RBRC  , KC_BSLS  ,                       TD_QUOTS , KC_F11   , KC_F12   , KC_F13   , TD_PP_TLD,
    KC_SCLN  , S(KC_7)  ,S(KC_LBRC), KC_BSPC  , KC_TAB   , KC_DEL   , _______  , _______  , _______  , _______  , _______  , _______
  ),

  [2] = LAYOUT_universal(
    S(KC_1)  , KC_LBRC  , S(KC_3)  , S(KC_4)  , S(KC_5)  ,                       KC_PSCR  , KC_BRID  , KC_BRIU  , KC_MUTE  , KC_VOLU  ,
    KC_EQUAL , S(KC_6)  ,S(KC_QUOT), S(KC_8)  , S(KC_9)  ,                       KC_LEFT  , KC_DOWN  , KC_UP    , KC_RIGHT , KC_VOLD  ,
   S(KC_INT1),S(KC_SCLN),S(KC_INT3),S(KC_RBRC),S(KC_BSLS),                       KC_HOME  , KC_PGDN  , KC_PGUP  , KC_END   , KC_F20   ,
    KC_QUOT , S(KC_2)   ,S(KC_EQL) , _______  , _______  , _______  , KC_RALT  , _______  , _______  , _______  , _______  , _______
  ),

  [3] = LAYOUT_universal(
    AML_TO   , SCRL_TO  , CPI_I100 , SSNP_VRT , QK_BOOT  ,                       QK_BOOT  , AMLY_TGL , AS_TOGG  , _______  , QK_RBT   ,
    AML_I50  , SCRL_MO  , CPI_D100 , SSNP_HOR , KBC_RST  ,                       OL_TGL   , MAT_I5   , AS_UP    , _______  , _______  ,
    AML_D50  , SCRL_DVI , CPI_I1K  , SSNP_FRE , KBC_SAVE ,                       OL_TGLINV, MAT_D5   , AS_DOWN  , RT_LY_TGL, KC_GRAVE ,
    _______  , SCRL_DVD , CPI_D1K  , _______  , _______  , _______  , KC_INT4  , KC_INT5  , _______  , _______  , _______  , _______
  ),

  [4] = LAYOUT_universal(
    _______  , _______  , _______  , _______  , _______  ,                       _______  , KC_F3    , KC_F5    , KC_F12   , _______  ,
    _______  , KC_LSFT  , KC_LCTL  , KC_LALT  , _______  ,                      A(KC_LEFT), KC_BTN1  , KC_BTN3  , KC_BTN2  ,A(KC_RGHT),
    _______  , _______  , _______  , _______  , _______  ,                       KC_LEFT  , KC_DOWN  , KC_UP    , KC_RIGHT , _______  ,
    _______  , _______  , _______  , _______  , _______  , _______  , _______  , TO(0)    , _______  , _______  , _______  , _______
  ),
};
// clang-format on

// Send highest active layer and bitmask via RAW HID; optionally bypass dedupe.
// NOTE: intentionally limit reported layers to 0-7 to keep protocol/UI simple.
static void send_layer_report(layer_state_t state, bool force) {
#if defined(RAW_ENABLE) && defined(HID_REPORT_ENABLE)
    if (!is_keyboard_master()) {
        return;
    }
    if (!user_state.raw_hid_layer_report_enabled) {
        return;
    }

    uint8_t mask     = (uint8_t)(state & 0xFF);  // layers 0-7 only
    uint8_t highest  = get_highest_layer(state);

    if (!force && highest == user_state.last_highest_layer && state == user_state.last_layer_state) {
        return;
    }

    uint8_t report[RAW_EPSIZE] = {0};
    report[0] = RAW_APP_ID_ZQ;
    report[1] = RAW_REPORT_VERSION;
    report[2] = RAW_REPORT_TYPE_LAYER;
    report[3] = highest;
    report[4] = mask;
    raw_hid_send(report, RAW_EPSIZE);

    user_state.last_highest_layer = highest;
    user_state.last_layer_state   = state;
#endif
}

#if defined(RAW_ENABLE) && defined(HID_REPORT_ENABLE) && !defined(VIA_ENABLE)
void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (!is_keyboard_master()) {
        return;
    }
    if (!user_state.raw_hid_layer_report_enabled) {
        return;
    }
    if (length < 3) {
        return;
    }
    if (data[0] != RAW_APP_ID_ZQ || data[1] != RAW_REPORT_VERSION || data[2] != RAW_REPORT_TYPE_LAYER) {
        return;
    }

    send_layer_report(user_state.last_layer_state, true);
}
#endif

#ifndef POINTING_DEVICE_AUTO_MOUSE_ENABLE
typedef struct {
    bool          active;
    uint16_t      motion_accum;
    uint16_t      btn1_timer;
    bool          btn1_pressed;
    bool          btn1_waiting_return;
} mouse_layer_state_t;

static mouse_layer_state_t mouse_layer_state = {0};

static inline uint8_t abs8(int8_t v) {
    return v < 0 ? (uint8_t)(-v) : (uint8_t)v;
}

static bool mouse_motion_exceeds_threshold(const report_mouse_t *report) {
    uint16_t delta = (uint16_t)abs8(report->x) + (uint16_t)abs8(report->y) + (uint16_t)abs8(report->h) + (uint16_t)abs8(report->v);
    if (delta == 0) {
        return false;
    }
    mouse_layer_state.motion_accum += delta;
    if (mouse_layer_state.motion_accum >= user_state.mouse_activation_threshold) {
        mouse_layer_state.motion_accum = 0;
        return true;
    }
    return false;
}

static inline bool is_mouse_layer_key_allowed(keyrecord_t *record) {
    uint16_t mouse_layer_keycode = keymap_key_to_keycode(MOUSE_LAYER, record->event.key);
    switch (mouse_layer_keycode) {
        case KC_BTN1 ... KC_BTN8:
        case KC_WH_U ... KC_WH_R:
        case KC_MS_UP ... KC_MS_RIGHT:
        case KC_ACL0 ... KC_ACL2:
        case KC_LSFT:
        case KC_LCTL:
        case KC_LALT:
        case KC_WWW_SEARCH ... KC_WWW_FAVORITES:
        case KC_F1 ... KC_F12:
        case A(KC_RIGHT):
        case A(KC_LEFT):
            return true;
        default:
            return false;
    }
}

static inline bool is_mouse_layer_active(void) {
    return mouse_layer_state.active && layer_state_is(MOUSE_LAYER);
}

static void reset_mouse_layer_state(void) {
    mouse_layer_state.active          = false;
    mouse_layer_state.btn1_pressed    = false;
    mouse_layer_state.btn1_timer      = 0;
    mouse_layer_state.btn1_waiting_return = false;
    mouse_layer_state.motion_accum    = 0;
}

static void mouse_layer_return_to_default(void) {
    reset_mouse_layer_state();
    layer_move(DEFAULT_LAYER);
}

static void mouse_layer_maybe_return(void) {
    if (!mouse_layer_state.active) {
        return;
    }

    if (!mouse_layer_state.btn1_waiting_return || mouse_layer_state.btn1_pressed || mouse_layer_state.btn1_timer == 0) {
        return;
    }

    if (timer_elapsed(mouse_layer_state.btn1_timer) >= MOUSE_BTN1_RETURN_TERM) {
        mouse_layer_return_to_default();
    }
}

static void tb_gesture_update_hold_state(tb_gesture_t *state) {
    if (state->pressed && !state->held &&
        timer_elapsed(state->press_timer) >= TAPPING_TERM) {
        state->held = true;
        state->x_accum = 0;
        state->y_accum = 0;
    }
}

static void tb_gesture_handle_key(tb_gesture_t *state, uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        state->pressed = true;
        state->held = false;
        state->press_timer = timer_read();
        state->x_accum = 0;
        state->y_accum = 0;
    } else {
        tb_gesture_update_hold_state(state);
        state->pressed = false;
        if (!state->held) {
            tap_code16(keycode);
        }
        state->held = false;
        state->x_accum = 0;
        state->y_accum = 0;
    }
}

static void tb_gesture_handle_motion(tb_gesture_t *state, report_mouse_t *mouse_report) {
    if (!state->held) {
        return;
    }

    if (mouse_report->x != 0) {
        state->x_accum += mouse_report->x;
        while (state->x_accum >= BTN_THRESHOLD || state->x_accum <= -BTN_THRESHOLD) {
            if (state->x_accum > 0) {
                if (state->motion_codes.right != KC_NO) {
                    tap_code16(state->motion_codes.right);
                }
                state->x_accum -= BTN_THRESHOLD;
            } else {
                if (state->motion_codes.left != KC_NO) {
                    tap_code16(state->motion_codes.left);
                }
                state->x_accum += BTN_THRESHOLD;
            }
        }
    }

    if (mouse_report->y != 0) {
        state->y_accum += mouse_report->y;
        while (state->y_accum >= BTN_THRESHOLD || state->y_accum <= -BTN_THRESHOLD) {
            if (state->y_accum > 0) {
                if (state->motion_codes.backward != KC_NO) {
                    tap_code16(state->motion_codes.backward);
                }
                state->y_accum -= BTN_THRESHOLD;
            } else {
                if (state->motion_codes.forward != KC_NO) {
                    tap_code16(state->motion_codes.forward);
                }
                state->y_accum += BTN_THRESHOLD;
            }
        }
    }

    // Stop Oocursor.
    mouse_report->x = 0;
    mouse_report->y = 0;
    mouse_report->h = 0;
    mouse_report->v = 0;
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    mouse_layer_maybe_return();

    if (mouse_layer_state.active && !layer_state_is(MOUSE_LAYER)) {
        mouse_layer_state.active = false;
    }

    tb_gesture_update_hold_state(&btn2_tb_gesture);
    tb_gesture_handle_motion(&btn2_tb_gesture, &mouse_report);
    tb_gesture_update_hold_state(&btn3_tb_gesture);
    tb_gesture_handle_motion(&btn3_tb_gesture, &mouse_report);

    if (user_state.auto_mouse_layer_enabled && !mouse_layer_state.active && mouse_motion_exceeds_threshold(&mouse_report)) {
        layer_on(MOUSE_LAYER);
        mouse_layer_state.active = true;
    }
    return mouse_report;
}

void handle_mouse_layer(uint16_t keycode, keyrecord_t *record) {
    mouse_layer_maybe_return();

    if (!is_mouse_layer_active()) {
        return;
    }

    if (keycode == KC_BTN1) {
        if (record->event.pressed) {
            // Second tap within term cancels pending return and completes as double tap
            if (mouse_layer_state.btn1_waiting_return && mouse_layer_state.btn1_timer != 0 &&
                timer_elapsed(mouse_layer_state.btn1_timer) < MOUSE_BTN1_RETURN_TERM) {
                mouse_layer_state.btn1_waiting_return = false;
                mouse_layer_state.btn1_timer          = 0;
            }
            mouse_layer_state.btn1_pressed = true;
        } else if (mouse_layer_state.btn1_pressed) {
            mouse_layer_state.btn1_pressed = false;
            if (mouse_layer_state.btn1_waiting_return) {
                // Second release: return immediately after double click
                mouse_layer_return_to_default();
            } else {
                // First release: wait for possible second click
                mouse_layer_state.btn1_waiting_return = true;
                mouse_layer_state.btn1_timer          = timer_read();
            }
        }
    }

    if (record->event.pressed) {
        bool allowed = is_mouse_layer_key_allowed(record);
        if (!allowed) {
            mouse_layer_return_to_default();
        }
    }
}
#endif

layer_state_t layer_state_set_user(layer_state_t state) {
    keyball_set_scroll_mode(layer_state_cmp(state, 3));
    send_layer_report(state, false);
    return state;
}

#ifdef OLED_ENABLE

#include "lib/oledkit/oledkit.h"

static void rpc_set_oled_invert_handler(uint8_t in_buflen, const void *in_data, uint8_t out_buflen, void *out_data) {
    bool oled_inversion = *(bool *)in_data;
    user_state.oled_inversion = oled_inversion;
}

static void rpc_set_oled_invert_invoke(void) {
    if (!user_state.oled_inversion_changed) {
        return;
    }
    bool req = user_state.oled_inversion;
    if (!transaction_rpc_send(KEYBALL_SET_OLED_INVERSION, sizeof(req), &req)) {
        return;
    }
    user_state.oled_inversion_changed = false;
}

static const char *format_4d(int8_t d) {
    static char buf[5] = {0}; // max width (4) + NUL (1)
    char        lead   = ' ';
    if (d < 0) {
        d    = -d;
        lead = '-';
    }
    buf[3] = (d % 10) + '0';
    d /= 10;
    if (d == 0) {
        buf[2] = lead;
        lead   = ' ';
    } else {
        buf[2] = (d % 10) + '0';
        d /= 10;
    }
    if (d == 0) {
        buf[1] = lead;
        lead   = ' ';
    } else {
        buf[1] = (d % 10) + '0';
        d /= 10;
    }
    buf[0] = lead;
    return buf;
}

static const char *format_u3d(uint8_t d) {
    static char buf[4] = {0}; // max width (3) + NUL (1)
    buf[2] = (d % 10) + '0';
    d /= 10;
    if (d == 0) {
        buf[1] = ' ';
    } else {
        buf[1] = (d % 10) + '0';
        d /= 10;
    }
    if (d == 0) {
        buf[0] = ' ';
    } else {
        buf[0] = (d % 10) + '0';
    }
    return buf;
}

static char to_1x(uint8_t x) {
    x &= 0x0f;
    return x < 10 ? x + '0' : x + 'a' - 10;
}

#define BL ((char)0xB0)
static const char LFSTR_ON[] PROGMEM = "\xB2\xB3";
static const char LFSTR_OFF[] PROGMEM = "\xB4\xB5";

void oled_render_ball_misc_info(void) {
    // Format: `Ball:{mouse x}{mouse y}{mouse h}{mouse v}`
    //
    // Output example:
    //
    //     Ball: -12  34   0   0

    // 1st line, "Ball" label, mouse x, y, h, and v.
    oled_write_P(PSTR("Ball\xB1"), false);
    oled_write(format_4d(keyball.last_mouse.x), false);
    oled_write(format_4d(keyball.last_mouse.y), false);
    oled_write(format_4d(keyball.last_mouse.h), false);
    oled_write(format_4d(keyball.last_mouse.v), false);

    // 2nd line, empty label and CPI
    oled_write_P(PSTR("\xBC\xBD"), false);
    oled_write(format_4d(keyball_get_cpi()) + 1, false);
    oled_write_P(PSTR("00 "), false);

    // indicate scroll snap mode: "VT" (vertical), "HO" (horizontal), and "SCR" (free)
#if 1 && KEYBALL_SCROLLSNAP_ENABLE == 2
    switch (keyball_get_scrollsnap_mode()) {
        case KEYBALL_SCROLLSNAP_MODE_VERTICAL:
            oled_write_P(PSTR("VT"), false);
            break;
        case KEYBALL_SCROLLSNAP_MODE_HORIZONTAL:
            oled_write_P(PSTR("HO"), false);
            break;
        default:
            oled_write_P(PSTR("\xBE\xBF"), false);
            break;
    }
#else
    oled_write_P(PSTR("\xBE\xBF"), false);
#endif
    // indicate scroll mode: on/off
    if (keyball.scroll_mode) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }

    // indicate scroll divider:
    oled_write_P(PSTR(" \xC0\xC1"), false);
    oled_write_char('0' + keyball_get_scroll_div(), false);

    // layer report
    oled_write_P(PSTR(" \xC6\xC7"), false);
#if defined(RAW_ENABLE) && defined(HID_REPORT_ENABLE)
    if (user_state.raw_hid_layer_report_enabled) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }
#else
    oled_write_P(LFSTR_OFF, false);
#endif
}

void oled_render_layer_misc_info(void) {
    oled_write_char('L', false);
    for (uint8_t i = 1; i <= 4; i++) {
        oled_write_char((layer_state_is(i) ? to_1x(i) : BL), false);
    }
    oled_write_char(' ', false);

    oled_write_P(PSTR("\xC2\xC3"), false);
    if (user_state.auto_mouse_layer_enabled) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }
    oled_write(format_u3d(user_state.mouse_activation_threshold), false);
    oled_write_char(' ', false);

#ifdef AUTO_SHIFT_ENABLE
    oled_write_P(PSTR("\xC4\xC5"), false);
    if (get_autoshift_state()) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }
    oled_write(format_u3d(get_generic_autoshift_timeout()), false);
#else
    oled_write_P(PSTR("\xC4\xC5"), false);
    oled_write_P(LFSTR_OFF, false);
    oled_write("---", false);
#endif
}

void oled_on_user(bool on) {
    if (on && !user_state.oled_on) {
        oled_on();
        user_state.oled_on = true;
        user_state.oled_timer = timer_read32();
    } else {
        oled_off();
        user_state.oled_on = false;
        user_state.oled_timer = 0;
    }
}

bool is_oled_on_user(void) {
    if (is_keyboard_master()){
        return user_state.oled_on;
    }
    return is_oled_on();
}

void oled_sync_inversion(void) {
    if (is_keyboard_master()) {
        return;
    }

    oled_invert(user_state.oled_inversion);
}

void oledkit_render_info_user(void) {
    if (!is_oled_on_user()) {
        return;
    }
    keyball_oled_render_keyinfo();
#if !defined(RAW_ENABLE) || !defined(HID_REPORT_ENABLE)
    keyball_oled_render_ballinfo();
#else
    oled_render_ball_misc_info();
#endif
#ifdef POINTING_DEVICE_AUTO_MOUSE_ENABLE
    keyball_oled_render_layerinfo();
#else
    oled_render_layer_misc_info();
#endif

}

void oledkit_render_logo_user(void) {
    if (!is_oled_on_user()) {
        return;
    }
    oled_sync_inversion();

    // Require `OLED_FONT_H "keyboards/keyball/lib/logofont/logofont.c"`
    char ch = 0x80;
    for (int y = 0; y < 3; y++) {
        oled_write_P(PSTR("  "), false);
        for (int x = 0; x < 16; x++) {
            oled_write_char(ch++, false);
        }
        oled_advance_page(false);
    }
}

#endif

void keyboard_post_init_user(void) {
#ifdef OLED_ENABLE
    transaction_register_rpc(KEYBALL_SET_OLED_INVERSION, rpc_set_oled_invert_handler);

    // turn on OLED on startup.
    oled_on_user(true);
#endif

#ifdef OS_DETECTION_ENABLE
    if (detected_host_os() == OS_WINDOWS) {
        btn2_tb_gesture.motion_codes.forward = KC_NO;
        btn2_tb_gesture.motion_codes.backward = KC_NO;
        btn2_tb_gesture.motion_codes.left = C(G(KC_LEFT));
        btn2_tb_gesture.motion_codes.right = C(G(KC_RIGHT));
    } else {
#endif
        btn2_tb_gesture.motion_codes.forward = KC_NO;
        btn2_tb_gesture.motion_codes.backward = KC_NO;
        btn2_tb_gesture.motion_codes.left = LGUI(KC_PGDN);
        btn2_tb_gesture.motion_codes.right = LGUI(KC_PGUP);
#ifdef OS_DETECTION_ENABLE
    }
#endif

    btn3_tb_gesture.motion_codes.forward = C(KC_EQUAL);
    btn3_tb_gesture.motion_codes.backward = C(S(KC_MINS));
    btn3_tb_gesture.motion_codes.left = C(KC_0);
    btn3_tb_gesture.motion_codes.right = C(KC_0);
}

void keyball_keyboard_post_init_eeconfig_user(uint32_t raw) {
    user_config_t c = { .raw = raw };

    // c.mouse_activation_threshold * MOUSE_ACTIVATION_THRESHOLD_QU
    // is the same as
    // MOUSE_ACTIVATION_THRESHOLD_MIN + MOUSE_ACTIVATION_THRESHOLD_QU * (c.mouse_activation_threshold - 1)
    // if MOUSE_ACTIVATION_THRESHOLD_MIN === MOUSE_ACTIVATION_THRESHOLD_QU
    user_state.mouse_activation_threshold = (c.mouse_activation_threshold == 0)
        ? MOUSE_ACTIVATION_THRESHOLD
        : c.mouse_activation_threshold * MOUSE_ACTIVATION_THRESHOLD_QU;
    user_state.auto_mouse_layer_enabled = c.auto_mouse_layer_enabled ? true : false;

#ifdef OLED_ENABLE
    user_state.oled_inversion = c.oled_inversion ? true : false;
    oled_invert(user_state.oled_inversion);
#endif
#if defined(RAW_ENABLE) && defined(HID_REPORT_ENABLE)
    user_state.raw_hid_layer_report_enabled = c.report_layer_state ? true : false;
    if (user_state.raw_hid_layer_report_enabled) {
    }
#endif
#ifdef AUTO_SHIFT_ENABLE
    if (c.autoshift_enabled) {
        autoshift_enable();
    } else {
        autoshift_disable();
    }
    set_autoshift_timeout(
        c.autoshift_timeout == 0
            ? AUTO_SHIFT_TIMEOUT
            : AUTO_SHIFT_TIMEOUT_MIN + AUTO_SHIFT_TIMEOUT_QU * (c.autoshift_timeout - 1)
    );
#endif
}

void housekeeping_task_user() {
    tb_gesture_update_hold_state(&btn2_tb_gesture);
    tb_gesture_update_hold_state(&btn3_tb_gesture);
    if (is_keyboard_master()) {
#ifdef OLED_ENABLE
        rpc_set_oled_invert_invoke();
        if (user_state.oled_on) {
            bool should_oled_on = (timer_elapsed32(user_state.oled_timer) <= KEYBALL_OLED_TIMEOUT);
            if (!should_oled_on && is_oled_on()) {
                oled_on_user(false);
            }
        }
#endif
    }
}

uint32_t keyball_process_record_eeconfig_user(uint32_t raw) {
    user_config_t c = { .raw = raw };

    c.mouse_activation_threshold = user_state.mouse_activation_threshold / MOUSE_ACTIVATION_THRESHOLD_QU;
    c.auto_mouse_layer_enabled = user_state.auto_mouse_layer_enabled ? 1 : 0;

#ifdef OLED_ENABLE
    c.oled_inversion = user_state.oled_inversion ? 1 : 0;
#endif
#if defined(RAW_ENABLE) && defined(HID_REPORT_ENABLE)
    c.report_layer_state = user_state.raw_hid_layer_report_enabled ? 1 : 0;
#endif
#ifdef AUTO_SHIFT_ENABLE
    uint16_t autoshift_timeout = get_generic_autoshift_timeout() <= AUTO_SHIFT_TIMEOUT_MIN
        ? AUTO_SHIFT_TIMEOUT_MIN
        : MIN(get_generic_autoshift_timeout(), AUTO_SHIFT_TIMEOUT_MAX);
    set_autoshift_timeout(autoshift_timeout);
    c.autoshift_enabled  = get_autoshift_state() ? 1 : 0;
    c.autoshift_timeout  = (autoshift_timeout - AUTO_SHIFT_TIMEOUT_MIN) / AUTO_SHIFT_TIMEOUT_QU + 1;
#endif

    return c.raw;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode == KC_BTN2) {
        tb_gesture_handle_key(&btn2_tb_gesture, keycode, record);
        handle_mouse_layer(keycode, record);
        return false;
    }
    if (keycode == KC_BTN3) {
        tb_gesture_handle_key(&btn3_tb_gesture, keycode, record);
        handle_mouse_layer(keycode, record);
        return false;
    }

    if (record->event.pressed) {
        // TODO Update oled timer if oled is on.
        switch (keycode) {
#ifdef OLED_ENABLE
            case OL_TGL:
                oled_on_user(!user_state.oled_on);
                break;
            case OL_TGLINV:
                user_state.oled_inversion = !user_state.oled_inversion;
                user_state.oled_inversion_changed = true;
                oled_invert(user_state.oled_inversion);
                break;
#endif
            case MAT_I5:
                {
                    uint8_t v = user_state.mouse_activation_threshold + 5;
                    user_state.mouse_activation_threshold = MIN(v, MOUSE_ACTIVATION_THRESHOLD_MAX);
                }
                break;
            case MAT_D5:
                {
                    uint8_t v = user_state.mouse_activation_threshold - 5;
                    user_state.mouse_activation_threshold = MAX(v, MOUSE_ACTIVATION_THRESHOLD_MIN);
                }
                break;
            case AMLY_TGL:
                user_state.auto_mouse_layer_enabled = !user_state.auto_mouse_layer_enabled;
#ifndef POINTING_DEVICE_AUTO_MOUSE_ENABLE
                if (!user_state.auto_mouse_layer_enabled) {
                    if (layer_state_is(MOUSE_LAYER)) {
                        mouse_layer_return_to_default();
                    } else {
                        reset_mouse_layer_state();
                    }
                }
#endif
                break;
#if defined(RAW_ENABLE) && defined(HID_REPORT_ENABLE)
            case RT_LY_TGL:
                user_state.raw_hid_layer_report_enabled = !user_state.raw_hid_layer_report_enabled;
                return false;
#endif
            // The autoshift implementation is in `qmk/quantum/process_keycode/process_auto_shift.c`.
            default:
                break;
        }
    }
#ifndef POINTING_DEVICE_AUTO_MOUSE_ENABLE
    handle_mouse_layer(keycode, record);
#endif

    return true;
}

#ifdef COMBO_ENABLE

const uint16_t PROGMEM jk_lclick_combo[] = {KC_J, KC_K, COMBO_END};
const uint16_t PROGMEM kl_rclick_combo[] = {KC_K, KC_L, COMBO_END};
const uint16_t PROGMEM jl_mclick_combo[] = {KC_J, KC_L, COMBO_END};
const uint16_t PROGMEM mcomm_back_combo[] = {KC_M, KC_COMM, COMBO_END};
const uint16_t PROGMEM commdot_fwd_combo[] = {KC_COMM, KC_DOT, COMBO_END};


combo_t key_combos[] = {
    COMBO(jk_lclick_combo, KC_BTN1),
    COMBO(kl_rclick_combo, KC_BTN2),
    COMBO(jl_mclick_combo, KC_BTN3),
    COMBO(mcomm_back_combo, A(KC_LEFT)),
    COMBO(commdot_fwd_combo, A(KC_RIGHT)),
};

#endif

#ifdef TAP_DANCE_ENABLE

// Japanese keyboard specific characters

static void td_quot_handler(tap_dance_state_t *state, void *user_data) {
    // 1 tap: ' , 2 taps: " , 3+ taps: `
    const uint16_t c =
        (state->count == 1) ? S(KC_7) :
        (state->count == 2) ? S(KC_2) :
        S(KC_LEFT_BRACKET);
    tap_code16(c);
}

tap_dance_action_t tap_dance_actions[] = {
    [TDKC_QUOTS] = ACTION_TAP_DANCE_FN(td_quot_handler),
    [TDKC_PIPE_TILD] = ACTION_TAP_DANCE_DOUBLE(S(KC_INTERNATIONAL_3), S(KC_EQUAL)),
};

#endif
