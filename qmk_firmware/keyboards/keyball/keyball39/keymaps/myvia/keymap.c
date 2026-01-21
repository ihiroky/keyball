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

// TODO User処理使用率のOLED表示

#include QMK_KEYBOARD_H

#include "quantum.h"
#include "split_common/transactions.h"
#include "raw_hid.h"
#include "usb_descriptor.h"
#include "os_detection.h"
#include "keymap_extras/keymap_japanese.h"

#define RAW_REPORT_VERSION 1
#define RAW_APP_ID_ZQ         0xFF
#define RAW_REPORT_TYPE_LAYER 0x00

#define DEFAULT_LAYER 0
#define MOUSE_LAYER 4
#define MOUSE_BTN1_RETURN_TERM TAPPING_TERM
#define TB_ACTIVATION_THRESHOLD 75
#define TB_GESTURE_INTERVAL 250

const uint16_t TB_ACTIVATION_THRESHOLD_MIN = 5;
const uint16_t TB_ACTIVATION_THRESHOLD_MAX = 155;
const uint16_t TB_ACTIVATION_THRESHOLD_QU = 5;
const uint16_t AUTO_SHIFT_TIMEOUT_MIN = 100;
const uint16_t AUTO_SHIFT_TIMEOUT_MAX = 250;
const uint16_t AUTO_SHIFT_TIMEOUT_QU  = 5;   // Quantization Unit

enum user_keycodes {
    OL_TGL = KEYBALL_SAFE_RANGE, // Toggle OLED on/off ,User 0
    OL_TGLINV,                   // Invert OLED display
    TAT_I5, // Increase trackball_activation_threshold by 5
    TAT_D5, // Decrease trackball_activation_threshold by 5
    AMLY_TGL, // Toggle auto mouse layer
    RT_LY_TGL, // Layer state report toggle
};

enum user_tapdance_keycodes {
    TD_PIPE_MO3,
};

typedef union {
    uint32_t raw;
    struct {
        uint32_t keyball_reserved : 18;
        uint8_t  report_layer_state : 1;
        uint8_t  oled_inversion : 1;
        uint8_t  auto_mouse_layer_enabled : 1;
        uint8_t  trackball_activation_threshold : 5;
        uint8_t  autoshift_enabled : 1;
        uint8_t  autoshift_timeout : 5;
    };
} user_config_t;

typedef enum {
    OLED_OFF = 0,
    OLED_ON_DEFAULT,
    OLED_ON_MYVIA_MISC
} oled_state_t;

typedef struct {
    bool auto_mouse_layer_enabled;
    uint8_t trackball_activation_threshold;
#ifdef OLED_ENABLE
    oled_state_t oled_status;
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
    uint16_t fire_timer;
    struct {
        uint16_t forward;
        uint16_t backward;
        uint16_t left;
        uint16_t right;
    } motion_codes;
} tb_gesture_t;

typedef enum {
    TB_GESTURE_NONE = -1,
    TB_GESTURE_WS_MOVE = 0,
    TB_GESTURE_ZOOM,
    TB_GESTURE_ZOOM_ALT,
    TB_GESTURE_COPY_PASTE,
} tb_gesture_id_t;

// Gestures may be overridden by housekeeping_task_user()
static tb_gesture_t tb_gestures[] = {
    [TB_GESTURE_WS_MOVE] = {
        .motion_codes = {
            .forward  = KC_NO,
            .backward = KC_NO,
            .left     = G(KC_PGDN),
            .right    = G(KC_PGUP),
        }
    },
    [TB_GESTURE_ZOOM] = {
        .motion_codes = {
            .forward  = C(JP_PLUS),
            .backward = C(JP_MINS),
            .left     = C(KC_0),
            .right    = C(KC_0),
        }
    },
    [TB_GESTURE_ZOOM_ALT] = {
        .motion_codes = {
            .forward  = C(JP_CIRC),
            .backward = C(JP_MINS),
            .left     = C(KC_0),
            .right    = C(KC_0),
        }
    },
    [TB_GESTURE_COPY_PASTE] = {
        .motion_codes = {
            .forward  = C(KC_C),
            .backward = C(KC_V),
            .left     = S(KC_HOME),
            .right    = S(KC_END),
        }
    },
};

// clang-format off
#define TD_PPMO3 TD(TD_PIPE_MO3)
#define LC_ESC   LCTL_T(KC_ESC)
#define LS_SPC   LSFT_T(KC_SPC)
#define L4_ENTER LT(4, KC_ENTER)
#define L3_YEN   LT(3, JP_YEN)
#define L2_BSPC  LT(2, KC_BSPC)
#define L1_TAB   LT(1, KC_TAB)
#define RC_DEL   RCTL_T(KC_DEL)
#define RS_BSLS  RSFT_T(JP_BSLS)
#define LC_CIRC  LCTL_T(JP_CIRC)
#define LA_AT    LALT_T(JP_AT)

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  // keymap for default (VIA)
  [0] = LAYOUT_universal(
    JP_Q     , JP_W     , JP_E     , JP_R     , JP_T     ,                       JP_Y     , JP_U     , JP_I     , JP_O     , JP_P     ,
    JP_A     , JP_S     , JP_D     , JP_F     , JP_G     ,                       JP_H     , JP_J     , JP_K     , JP_L     , L4_ENTER ,
    JP_Z     , JP_X     , JP_C     , JP_V     , JP_B     ,                       JP_N     , JP_M     , JP_COMM  , JP_DOT   , JP_SLSH  ,
    LC_ESC   , KC_LALT  , KC_LGUI  , TD_PPMO3 , LS_SPC   , L2_BSPC  , RC_DEL  ,  L1_TAB   , _______  , _______  , _______  , RS_BSLS
  ),

  [1] = LAYOUT_universal(
    JP_PLUS  , JP_7     , JP_8     , JP_9     , JP_ASTR  ,                       JP_QUOT  , JP_LBRC  , JP_RBRC  , JP_DLR   , JP_HASH  ,
    JP_MINS  , JP_4     , JP_5     , JP_6     , JP_0     ,                       JP_DQUO  , JP_LPRN  , JP_RPRN  , JP_AMPR  , JP_TILD  ,
    JP_EQL   , JP_1     , JP_2     , JP_3     , JP_PERC  ,                       JP_GRV   , JP_LCBR  , JP_RCBR  , JP_EXLM  , JP_UNDS  ,
    JP_COLN  , JP_YEN   , _______  , LC_CIRC  , _______  , LA_AT    , _______  , _______  , _______  , _______  , _______  , JP_SCLN
  ),

  [2] = LAYOUT_universal(
    KC_F1    , KC_F2    , KC_F3    , KC_F4    , KC_F5    ,                       KC_PSCR  , KC_BRID  , KC_BRIU  , KC_F20   , KC_VOLU  ,
    KC_F6    , KC_F7    , KC_F8    , KC_F9    , KC_F10   ,                       KC_LEFT  , KC_DOWN  , KC_UP    , KC_RIGHT , KC_VOLD  ,
    KC_F11   , KC_F12   , KC_F13   , KC_F14   , KC_F15   ,                       KC_HOME  , KC_PGDN  , KC_PGUP  , KC_END   , KC_MUTE  ,
    KC_LGUI  , _______  , _______  , _______  , _______  , _______  , _______  , KC_RALT  , _______  , _______  , _______  , _______
  ),

  [3] = LAYOUT_universal(
    AML_TO   , SCRL_TO  , CPI_I100 , SSNP_VRT , DB_TOGG  ,                       QK_MAKE  , AMLY_TGL , _______  , _______  , QK_RBT   ,
    AML_I50  , SCRL_MO  , CPI_D100 , SSNP_HOR , KBC_RST  ,                       OL_TGL   , TAT_I5   , _______  , _______  , _______  ,
    AML_D50  , SCRL_DVI , CPI_I1K  , SSNP_FRE , KBC_SAVE ,                       OL_TGLINV, TAT_D5   , _______  , RT_LY_TGL, JP_ZKHK  ,
    _______  , SCRL_DVD , CPI_D1K  , _______  , QK_BOOT  , EE_CLR   , JP_HENK  , JP_MHEN  , _______  , _______  , _______  , _______
  ),

  [4] = LAYOUT_universal(
    _______  , _______  , KC_LSFT  , KC_RSFT  , _______  ,                       _______  , KC_F3    , KC_F5    , KC_F12   , _______  ,
    _______  , _______  , KC_LCTL  , KC_RCTL  , _______  ,                       _______  , KC_BTN1  , KC_BTN3  , KC_BTN2  , _______  ,
    _______  , _______  , KC_LALT  , KC_RALT  , _______  ,                       _______  , KC_BTN4  , _______  , KC_BTN5  , _______  ,
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

#ifdef TAP_DANCE_ENABLE

typedef struct {
    uint16_t tap;
    uint16_t hold;
    uint16_t held;
} tap_dance_tap_hold_t;

void tap_dance_tap_hold_finished(tap_dance_state_t *state, void *user_data) {
    tap_dance_tap_hold_t *tap_hold = (tap_dance_tap_hold_t *)user_data;

    // Block to call twice (track ball interruption and regular event)
    if (state->count == 0 || tap_hold->held) {
        return;
    }

    if (state->count > 1) {
        // Multi-tap: repeat tap; if still pressed, keep last tap held for key repeat.
        if (state->pressed) {
            for (uint8_t i = 0; i < (state->count - 1); i++) {
                tap_code16(tap_hold->tap);
            }
            register_code16(tap_hold->tap);
            tap_hold->held = tap_hold->tap;
        } else {
            for (uint8_t i = 0; i < state->count; i++) {
                tap_code16(tap_hold->tap);
            }
        }
        return;
    }

    if (state->pressed
#if !defined(PERMISSIVE_HOLD) && !defined(HOLD_ON_OTHER_KEY_PRESS)
        && !state->interrupted
#endif
    ) {
        if (QK_MOMENTARY <= tap_hold->hold && tap_hold->hold <= QK_MOMENTARY_MAX) {
            layer_on(QK_MOMENTARY_GET_LAYER(tap_hold->hold));
        } else {
            register_code16(tap_hold->hold);
        }
        tap_hold->held = tap_hold->hold;
    } else {
        register_code16(tap_hold->tap);
        tap_hold->held = tap_hold->tap;
    }
}

void tap_dance_tap_hold_reset(tap_dance_state_t *state, void *user_data) {
    tap_dance_tap_hold_t *tap_hold = (tap_dance_tap_hold_t *)user_data;

    if (tap_hold->held) {
        if (QK_MOMENTARY <= tap_hold->held && tap_hold->held <= QK_MOMENTARY_MAX) {
            layer_off(QK_MOMENTARY_GET_LAYER(tap_hold->held));
        } else {
            unregister_code16(tap_hold->held);
        }
        tap_hold->held = 0;
    }
}

#define ACTION_TAP_DANCE_TAP_HOLD(tap, hold)                                        \
    {                                                                               \
        .fn        = {NULL, tap_dance_tap_hold_finished, tap_dance_tap_hold_reset}, \
        .user_data = (void *)&((tap_dance_tap_hold_t){tap, hold, 0}),               \
    }

tap_dance_action_t tap_dance_actions[] = {
    [TD_PIPE_MO3] =  ACTION_TAP_DANCE_TAP_HOLD(JP_PIPE, MO(3)),
};

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

static inline uint16_t trackball_motion(const report_mouse_t *report) {
    return (uint16_t)abs8(report->x) + (uint16_t)abs8(report->y) + (uint16_t)abs8(report->h) + (uint16_t)abs8(report->v);
}

static bool mouse_motion_exceeds_threshold(const report_mouse_t *report) {
    uint16_t delta = trackball_motion(report);
    if (delta == 0) {
        return false;
    }
    mouse_layer_state.motion_accum += delta;
    if (mouse_layer_state.motion_accum >= user_state.trackball_activation_threshold) {
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

static void tb_gesture_update_state(tb_gesture_t *state) {
    if (state->pressed && !state->held &&
        timer_elapsed(state->press_timer) >= TAPPING_TERM) {
        state->held = true;
        state->x_accum = 0;
        state->y_accum = 0;
    }
    if (state->fire_timer &&
        timer_elapsed(state->fire_timer) >= TB_GESTURE_INTERVAL) {
        state->fire_timer = 0;
    }
}

static void tb_gesture_handle_key(tb_gesture_t *state, uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        state->pressed = true;
        state->held = false;
        state->press_timer = timer_read();
        state->x_accum = 0;
        state->y_accum = 0;
        state->fire_timer = 0;
    } else {
        tb_gesture_update_state(state);
        state->pressed = false;
        if (!state->held) {
            tap_code16(keycode);
        }
        state->held = false;
        state->x_accum = 0;
        state->y_accum = 0;
        state->fire_timer = 0;
    }
}

static void tb_gesture_handle_motion(tb_gesture_t *state, report_mouse_t *mouse_report) {
#if defined(PERMISSIVE_HOLD) || defined(HOLD_ON_OTHER_KEY_PRESS)
    if (!state->pressed) {
        return;
    }
#else
    if (!state->held) {
        return;
    }
#endif

    int16_t threshold = (int16_t)user_state.trackball_activation_threshold;

    bool x_fired = false;
    if (mouse_report->x != 0 && state->fire_timer == 0) {
        state->x_accum += mouse_report->x;
        bool over_x = (state->x_accum >= threshold);
        bool under_x = (state->x_accum <= -threshold);
        x_fired = over_x || under_x;
        if (x_fired) {
            if (over_x) {
                if (state->motion_codes.right != KC_NO) {
                    tap_code16(state->motion_codes.right);
                }
                state->x_accum = 0;
            } else {
                if (state->motion_codes.left != KC_NO) {
                    tap_code16(state->motion_codes.left);
                }
                state->x_accum = 0;
            }
            state->fire_timer = timer_read();
        }
    }

    bool y_fired = false;
    if (mouse_report->y != 0 && state->fire_timer == 0) {
        state->y_accum += mouse_report->y;
        bool over_y = (state->y_accum >= threshold);
        bool under_y = (state->y_accum <= -threshold);
        y_fired = over_y || under_y;
        if (y_fired) {
            if (over_y) {
                if (state->motion_codes.backward != KC_NO) {
                    tap_code16(state->motion_codes.backward);
                }
                state->y_accum = 0;
            } else {
                if (state->motion_codes.forward != KC_NO) {
                    tap_code16(state->motion_codes.forward);
                }
                state->y_accum = 0;
            }
            state->fire_timer = timer_read();
        }
    }
#if defined(PERMISSIVE_HOLD) || defined(HOLD_ON_OTHER_KEY_PRESS)
    state->held |= (x_fired || y_fired);
#endif

    // Stop cursor based on tap-hold behavior.
#if defined(PERMISSIVE_HOLD)
    if (state->held) {
        mouse_report->x = 0;
        mouse_report->y = 0;
        mouse_report->h = 0;
        mouse_report->v = 0;
    }
#else
    mouse_report->x = 0;
    mouse_report->y = 0;
    mouse_report->h = 0;
    mouse_report->v = 0;
#endif
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    mouse_layer_maybe_return();

    if (mouse_layer_state.active && !layer_state_is(MOUSE_LAYER)) {
        mouse_layer_state.active = false;
    }

    const size_t tb_gesture_count = sizeof(tb_gestures) / sizeof(tb_gestures[0]);
    for (size_t i = 0; i < tb_gesture_count; i++) {
        tb_gesture_t *tbg = &tb_gestures[i];
        tb_gesture_update_state(tbg);
        tb_gesture_handle_motion(tbg, &mouse_report);
    }

#if defined(TAP_DANCE_ENABLE)
#if defined(PERMISSIVE_HOLD) || defined(HOLD_ON_OTHER_KEY_PRESS)
    // Interrupt to tap dance for tap/hold
    tap_dance_action_t *tda = &tap_dance_actions[TD_PIPE_MO3];
    tap_dance_state_t *tds = &tda->state;
    if (tds->count == 1 && tds->pressed) {
        uint16_t td_delta = trackball_motion(&mouse_report);
        if (td_delta > 0) {
            tap_dance_tap_hold_finished(tds, tda->user_data);
        }
    }
#endif
#endif

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
    if (!transaction_rpc_send(MYVIA_SET_OLED_INVERSION, sizeof(req), &req)) {
        return;
    }
    user_state.oled_inversion_changed = false;
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

static const char *format_u4d(uint16_t d) {
    static char buf[5] = {0}; // max width (4) + NUL (1)
    buf[3] = (d % 10) + '0';
    d /= 10;
    if (d == 0) {
        buf[2] = ' ';
    } else {
        buf[2] = (d % 10) + '0';
        d /= 10;
    }
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

static const char LFSTR_ON[] PROGMEM = "\xB2\xB3";
static const char LFSTR_OFF[] PROGMEM = "\xB4\xB5";

#ifdef OS_DETECTION_ENABLE
static char os_variant_initial(os_variant_t os) {
    switch (os) {
        case OS_LINUX:
            return 'L';
        case OS_WINDOWS:
            return 'W';
        case OS_MACOS:
            return 'M';
        case OS_IOS:
            return 'I';
        case OS_UNSURE:
        default:
            return '?';
    }
}
#endif

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    return !is_keyboard_left() ? OLED_ROTATION_180 : rotation;
}

void oled_render_myvia_info(void) {
    // key-related info (auto shift)
    oled_write_P(PSTR("Key \xB1"), false);
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
#endif
    oled_write_P(PSTR(" \xCE\xCF"), false); // " MSR"
    uint16_t msr = MIN(get_matrix_scan_rate(), 9999);
    oled_write(format_u4d((uint16_t)msr), false);
    oled_advance_page(false);

    // trackball-related info
    oled_write_P(PSTR("Ball\xB1\xC8\xC9"), false);
    oled_write(format_u3d((uint8_t)TB_GESTURE_INTERVAL), false);
    oled_write_P(PSTR(" \xC6\xC7"), false); // " GI"
    oled_write(format_u3d(user_state.trackball_activation_threshold), false);
    oled_advance_page(false);

    // layer-related info (auto mouse + layer report)
    oled_write_P(PSTR("L\xB6\xB7r\xB1"), false);
    oled_write_P(PSTR("\xC2\xC3"), false); // "AML"
#ifndef POINTING_DEVICE_AUTO_MOUSE_ENABLE
    if (user_state.auto_mouse_layer_enabled) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }
#endif
    oled_write_char('/', false);
    oled_write_char(MOUSE_LAYER + '0', false);
    oled_write_P(PSTR(" \xCA\xCB"), false); // " LSR"
#if defined(RAW_ENABLE) && defined(HID_REPORT_ENABLE)
    if (user_state.raw_hid_layer_report_enabled) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }
#else
    oled_write_P(LFSTR_OFF, false);
#endif

    oled_advance_page(false);
    oled_write_P(PSTR("Misc\xB1\xCC\xCD:"), false); // "Misc|OS:"
#ifdef OS_DETECTION_ENABLE
    oled_write_char(os_variant_initial(detected_host_os()), false);
#else
    oled_write_char('?', false);
#endif
}

void oled_set_status(oled_state_t state) {
    if (state != OLED_OFF && user_state.oled_status == OLED_OFF) {
        oled_on();
        user_state.oled_timer = timer_read32();
    } else if (state == OLED_OFF && user_state.oled_status != OLED_OFF) {
        oled_off();
        user_state.oled_timer = 0;
    }
    if (state == OLED_ON_MYVIA_MISC && user_state.oled_status != OLED_ON_MYVIA_MISC) {
        oled_clear();
    }
    user_state.oled_status = state;
}

void oled_toggle_status(void) {
    oled_state_t s = user_state.oled_status;
    switch (s) {
        case OLED_OFF:
            oled_set_status(OLED_ON_DEFAULT);
            break;
        case OLED_ON_DEFAULT:
            oled_set_status(OLED_ON_MYVIA_MISC);
            break;
        case OLED_ON_MYVIA_MISC:
            oled_set_status(OLED_OFF);
            break;
        default:
            oled_set_status(OLED_OFF);
            break;
    }
}

void oled_reset_timeout(void) {
    if (user_state.oled_status == OLED_OFF) {
        return;
    }
    user_state.oled_timer = timer_read32();
}

void oled_sync_inversion(void) {
    if (is_keyboard_master()) {
        return;
    }

    oled_invert(user_state.oled_inversion);
}

void oledkit_render_info_user(void) {
    switch (user_state.oled_status) {
        case OLED_OFF:
            break;
        case OLED_ON_DEFAULT:
            keyball_oled_render_keyinfo();
            keyball_oled_render_ballinfo();
            keyball_oled_render_layerinfo();
            break;
        case OLED_ON_MYVIA_MISC:
            oled_render_myvia_info();
            break;
        default:
            break;
    }
}

void oledkit_render_logo_user(void) {
    if (!is_oled_on()) {
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
    transaction_register_rpc(MYVIA_SET_OLED_INVERSION, rpc_set_oled_invert_handler);

    // turn on OLED on startup.
    oled_set_status(OLED_ON_DEFAULT);
#endif
}

void keyball_keyboard_post_init_eeconfig_user(uint32_t raw) {
    user_config_t c = { .raw = raw };

    // c.trackball_activation_threshold * MOUSE_ACTIVATION_THRESHOLD_QU
    // is the same as
    // MOUSE_ACTIVATION_THRESHOLD_MIN + MOUSE_ACTIVATION_THRESHOLD_QU * (c.trackball_activation_threshold - 1)
    // if MOUSE_ACTIVATION_THRESHOLD_MIN === MOUSE_ACTIVATION_THRESHOLD_QU
    user_state.trackball_activation_threshold = (c.trackball_activation_threshold == 0)
        ? TB_ACTIVATION_THRESHOLD
        : c.trackball_activation_threshold * TB_ACTIVATION_THRESHOLD_QU;
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
    const size_t tb_gesture_count = sizeof(tb_gestures) / sizeof(tb_gestures[0]);
    for (size_t i = 0; i < tb_gesture_count; i++) {
        tb_gesture_update_state(&tb_gestures[i]);
    }

    if (is_keyboard_master()) {
#ifdef OS_DETECTION_ENABLE
        static os_variant_t last_detected_os = OS_UNSURE;
        os_variant_t cur_os = detected_host_os();
        if (cur_os != last_detected_os) {
            // Reassign gesture if detected os changed.
            last_detected_os = OS_UNSURE;
        }
        if (last_detected_os == OS_UNSURE) {
            switch (cur_os) {
                case OS_UNSURE:
                    break;
                case OS_LINUX:
                    tb_gestures[TB_GESTURE_WS_MOVE].motion_codes.left = G(KC_PGDN);
                    tb_gestures[TB_GESTURE_WS_MOVE].motion_codes.right = G(KC_PGUP);
                    last_detected_os = cur_os;
                    break;
                case OS_WINDOWS:
                    tb_gestures[TB_GESTURE_WS_MOVE].motion_codes.left = C(G(KC_LEFT));
                    tb_gestures[TB_GESTURE_WS_MOVE].motion_codes.right = C(G(KC_RIGHT));
                    last_detected_os = cur_os;
                    break;
                default:
                    last_detected_os = cur_os;
                    break;
            }
        }
#endif
#ifdef OLED_ENABLE
        rpc_set_oled_invert_invoke();
        if (user_state.oled_status != OLED_OFF) {
            bool should_oled_off = (timer_elapsed32(user_state.oled_timer) > MYVIA_OLED_TIMEOUT);
            if (should_oled_off && is_oled_on()) {
                oled_set_status(OLED_OFF);
            }
        }
#endif
    }
}

uint32_t keyball_process_record_eeconfig_user(uint32_t raw) {
    user_config_t c = { .raw = raw };

    c.trackball_activation_threshold = user_state.trackball_activation_threshold / TB_ACTIVATION_THRESHOLD_QU;
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
#ifdef OLED_ENABLE
    oled_reset_timeout();
#endif

    tb_gesture_id_t tbg_id = TB_GESTURE_NONE;
    switch (keycode) {
        case KC_BTN2:
            tbg_id = TB_GESTURE_WS_MOVE;
            break;
        case KC_BTN4:
            tbg_id = TB_GESTURE_ZOOM;
            break;
        case KC_BTN5:
            tbg_id = TB_GESTURE_ZOOM_ALT;
            break;
        case JP_COMM:
            tbg_id = TB_GESTURE_COPY_PASTE;
            break;
    }
    if (tbg_id != TB_GESTURE_NONE) {
        tb_gesture_handle_key(&tb_gestures[tbg_id], keycode, record);
#ifndef POINTING_DEVICE_AUTO_MOUSE_ENABLE
        handle_mouse_layer(keycode, record);
#endif
        return false;
    }

    if (record->event.pressed) {
        switch (keycode) {
#ifdef OLED_ENABLE
            case OL_TGL:
                oled_toggle_status();
                break;
            case OL_TGLINV:
                user_state.oled_inversion = !user_state.oled_inversion;
                user_state.oled_inversion_changed = true;
                oled_invert(user_state.oled_inversion);
                break;
#endif
            case TAT_I5:
                {
                    uint8_t v = user_state.trackball_activation_threshold + 5;
                    user_state.trackball_activation_threshold = MIN(v, TB_ACTIVATION_THRESHOLD_MAX);
                }
                break;
            case TAT_D5:
                {
                    uint8_t v = user_state.trackball_activation_threshold - 5;
                    user_state.trackball_activation_threshold = MAX(v, TB_ACTIVATION_THRESHOLD_MIN);
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

const uint16_t PROGMEM left_click_combo[] = {KC_J, KC_K, COMBO_END};
const uint16_t PROGMEM right_click_combo[] = {KC_K, KC_L, COMBO_END};
const uint16_t PROGMEM middle_click_combo[] = {KC_J, KC_L, COMBO_END};
const uint16_t PROGMEM backward_combo[] = {KC_M, KC_COMMA, COMBO_END};
const uint16_t PROGMEM forward_combo[] = {KC_COMMA, KC_DOT, COMBO_END};

combo_t key_combos[] = {
    COMBO(left_click_combo, KC_BTN1),
    COMBO(right_click_combo, KC_BTN2),
    COMBO(middle_click_combo, KC_BTN3),
    COMBO(backward_combo, KC_BTN4),
    COMBO(forward_combo, KC_BTN5),
};

#endif
