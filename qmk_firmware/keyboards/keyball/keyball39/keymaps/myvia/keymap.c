/*
Copyright 2022 @Yowkees
Copyright 2022 MURAOKA Taro (aka KoRoN, @kaoriya)

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

#define DEFAULT_LAYER 0
#define MOUSE_LAYER 4
#define MOUSE_ACTIVATION_THRESHOLD 50 // TODO configurable
#define MOUSE_BTN1_RETURN_TERM 160

const uint16_t AUTO_SHIFT_TIMEOUT_MIN = 50;
const uint16_t AUTO_SHIFT_TIMEOUT_MAX = 1000;
const uint16_t AUTO_SHIFT_TIMEOUT_QU  = 50;   // Quantization Unit

enum user_keycodes {
    OLED_TOGGLE = KEYBALL_SAFE_RANGE, // Toggle OLED on/off
    OLED_TOGGLE_INVERT,               // Invert OLED display
};

typedef union {
    uint32_t raw;
    struct {
#ifdef OLED_ENABLE
        uint32_t keyball_reserved : 25;
        uint8_t  oled_inversion : 1; //  OLED inversion
#else
        uint32_t keyball_reserved : 26;
#endif
        uint8_t  autoshift_enabled : 1; // auto shift enabled
        uint16_t autoshift_timeout : 5; // auto shift timeout
    };
} user_config_t;

typedef struct {
#ifdef OLED_ENABLE
    bool oled_on;
    uint32_t oled_timer;
    bool oled_inversion;
    bool oled_inversion_changed;
#endif
} user_state_t;

static user_state_t user_state = {0};

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  // keymap for default (VIA)
  [0] = LAYOUT_universal(
    KC_Q     , KC_W     , KC_E     , KC_R     , KC_T     ,                            KC_Y     , KC_U     , KC_I     , KC_O     , KC_P     ,
    KC_A     , KC_S     , KC_D     , KC_F     , KC_G     ,                            KC_H     , KC_J     , KC_K     , KC_L     , KC_MINS  ,
    KC_Z     , KC_X     , KC_C     , KC_V     , KC_B     ,                            KC_N     , KC_M     , KC_COMM  , KC_DOT   , KC_SLSH  ,
    KC_LCTL  , KC_LGUI  , KC_LALT  ,LSFT_T(KC_LNG2),LT(1,KC_SPC),LT(3,KC_LNG1),KC_BSPC,LT(2,KC_ENT),LSFT_T(KC_LNG2),KC_RALT,KC_RGUI, KC_RSFT
  ),

  [1] = LAYOUT_universal(
    KC_F1    , KC_F2    , KC_F3    , KC_F4    , KC_RBRC  ,                            KC_F6    , KC_F7    , KC_F8    , KC_F9    , KC_F10   ,
    KC_F5    , KC_EXLM  , S(KC_6)  ,S(KC_INT3), S(KC_8)  ,                           S(KC_INT1), KC_BTN1  , KC_PGUP  , KC_BTN2  , KC_SCLN  ,
    S(KC_EQL),S(KC_LBRC),S(KC_7)   , S(KC_2)  ,S(KC_RBRC),                            KC_LBRC  , KC_DLR   , KC_PGDN  , KC_BTN3  , KC_F11   ,
    KC_INT1  , KC_EQL   , S(KC_3)  , _______  , _______  , _______  ,      TO(2)    , TO(0)    , _______  , KC_RALT  , KC_RGUI  , KC_F12
  ),

  [2] = LAYOUT_universal(
    KC_TAB   , KC_7     , KC_8     , KC_9     , KC_MINS  ,                            KC_NUHS  , _______  , KC_BTN3  , _______  , KC_BSPC  ,
   S(KC_QUOT), KC_4     , KC_5     , KC_6     ,S(KC_SCLN),                            S(KC_9)  , KC_BTN1  , KC_UP    , KC_BTN2  , KC_QUOT  ,
    KC_SLSH  , KC_1     , KC_2     , KC_3     ,S(KC_MINS),                           S(KC_NUHS), KC_LEFT  , KC_DOWN  , KC_RGHT  , _______  ,
    KC_ESC   , KC_0     , KC_DOT   , KC_DEL   , KC_ENT   , KC_BSPC  ,      _______  , _______  , _______  , _______  , _______  , _______
  ),

  [3] = LAYOUT_universal(
    RGB_TOG  , AML_TO   , AML_I50  , AML_D50  , _______  ,                            _______  , _______  , SSNP_HOR , SSNP_VRT , SSNP_FRE ,
    RGB_MOD  , RGB_HUI  , RGB_SAI  , RGB_VAI  , SCRL_DVI ,                            _______  , _______  , _______  , _______  , _______  ,
    RGB_RMOD , RGB_HUD  , RGB_SAD  , RGB_VAD  , SCRL_DVD ,                            CPI_D1K  , CPI_D100 , CPI_I100 , CPI_I1K  , KBC_SAVE ,
    QK_BOOT  , KBC_RST  , _______  , _______  , _______  , _______  ,      _______  , _______  , _______  , _______  , KBC_RST  , QK_BOOT
  ),

  [4] = LAYOUT_universal(
    _______  , _______  , _______  , _______  , _______  ,                            _______  , KC_F3    , KC_F5    , KC_F12   , _______  ,
    _______  , KC_LSFT  , KC_LCTL  , KC_LALT  , _______  ,                            KC_LEFT  , KC_BTN1  , KC_BTN3  , KC_BTN2  , KC_RGHT  ,
    _______  , _______  , _______  , _______  , _______  ,                            _______  , _______  , _______  , _______  , _______  ,
    _______  , _______  , _______  , _______  , _______  , _______  ,      _______  , TO(0)    , _______  , _______  , _______  , _______
  ),
};
// clang-format on

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
    if (mouse_layer_state.motion_accum >= MOUSE_ACTIVATION_THRESHOLD) {
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

static void mouse_layer_return_to_default(void) {
    mouse_layer_state.active          = false;
    mouse_layer_state.btn1_pressed    = false;
    mouse_layer_state.btn1_timer      = 0;
    mouse_layer_state.btn1_waiting_return = false;
    mouse_layer_state.motion_accum    = 0;

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

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    mouse_layer_maybe_return();

    if (mouse_layer_state.active && !layer_state_is(MOUSE_LAYER)) {
        mouse_layer_state.active = false;
    }

    if (!mouse_layer_state.active && mouse_motion_exceeds_threshold(&mouse_report)) {
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
    // Enable scroll mode whenever layer 3 is active
    keyball_set_scroll_mode(layer_state_cmp(state, 3));
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

static char to_1x(uint8_t x) {
    x &= 0x0f;
    return x < 10 ? x + '0' : x + 'a' - 10;
}

static void oled_write_timeout(uint16_t timeout) {
    oled_write(format_4d(timeout / 10) + 1, false);
    oled_write_char((char) ('0' + (timeout % 10)), false);
}

#define BL ((char)0xB0)
static const char LFSTR_ON[] PROGMEM = "\xB2\xB3";
static const char LFSTR_OFF[] PROGMEM = "\xB4\xB5";

void oled_render_layer_aml_as_info(void) {
    // Format: `Layer:{layer state}`
    //
    // Output example:
    //
    //     Layer:-23------------
    //
    oled_write_P(PSTR("L\xB6\xB7r\xB1"), false);
    for (uint8_t i = 1; i < 8; i++) {
        oled_write_char((layer_state_is(i) ? to_1x(i) : BL), false);
    }
    oled_write_char(' ', false);

#    ifdef POINTING_DEVICE_AUTO_MOUSE_ENABLE
    oled_write_P(PSTR("\xC2\xC3"), false);
    if (get_auto_mouse_enable()) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }

    oled_write_timeout(get_auto_mouse_timeout());
#    else
    oled_write_P(PSTR("\xC4\xC5"), false);
    if (get_autoshift_state()) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }
    oled_write_timeout(get_generic_autoshift_timeout());
#    endif
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
    keyball_oled_render_ballinfo();
    oled_render_layer_aml_as_info();
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


void keyboard_post_init_user() {
#ifdef OLED_ENABLE
    transaction_register_rpc(KEYBALL_SET_OLED_INVERSION, rpc_set_oled_invert_handler);

    // turn on OLED on startup.
    oled_on_user(true);
#endif
}

void keyball_keyboard_post_init_kb_eeconfig(uint32_t raw) {
    user_config_t c = { .raw = raw };

    // TODO
#ifdef OLED_ENABLE
        user_state.oled_inversion = c.oled_inversion ? true : false;
        oled_invert(user_state.oled_inversion);
#endif
        if (c.autoshift_enabled) {
            autoshift_enable();
        } else {
            autoshift_disable();
        }
        set_autoshift_timeout(c.autoshift_timeout == 0 ? AUTO_SHIFT_TIMEOUT : (c.autoshift_timeout + 1) * AUTO_SHIFT_TIMEOUT_QU);
}

void housekeeping_task_user() {
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

uint32_t keyball_process_record_kb_eeconfig(uint32_t raw) {
    user_config_t c = { .raw = raw };

#ifdef OLED_ENABLE
    c.oled_inversion = user_state.oled_inversion ? 1 : 0;
#endif

    uint16_t autoshift_timeout = get_generic_autoshift_timeout() <= AUTO_SHIFT_TIMEOUT_MIN
        ? AUTO_SHIFT_TIMEOUT_MIN
        : MIN(get_generic_autoshift_timeout(), AUTO_SHIFT_TIMEOUT_MAX);
    c.autoshift_enabled   = get_autoshift_state() ? 1 : 0;
    c.autoshift_timeout  = (autoshift_timeout / AUTO_SHIFT_TIMEOUT_QU) - 1;

    return c.raw;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        switch (keycode) {
#ifdef OLED_ENABLE
            case OLED_TOGGLE:
                oled_on_user(!user_state.oled_on);
                break;
            case OLED_TOGGLE_INVERT:
                user_state.oled_inversion = !user_state.oled_inversion;
                user_state.oled_inversion_changed = true;
                oled_invert(user_state.oled_inversion);
                break;
#endif
            default:
                break;
        }
    }
    handle_mouse_layer(keycode, record);
    return true;
}

#ifdef COMBO_ENABLE

const uint16_t PROGMEM esc_combo[] = {KC_Q, KC_F, COMBO_END};
const uint16_t PROGMEM tab_combo[] = {KC_A, KC_F, COMBO_END};
const uint16_t PROGMEM del_combo[] = {KC_ENTER, KC_J, COMBO_END};
const uint16_t PROGMEM bsp_combo[] = {KC_L, KC_J, COMBO_END};
const uint16_t PROGMEM spc_combo[] = {KC_K, KC_J, COMBO_END};


combo_t key_combos[] = {
    COMBO(esc_combo, KC_ESC),
    COMBO(tab_combo, KC_TAB),
    COMBO(del_combo, KC_DEL),
    COMBO(bsp_combo, KC_BSPC),
    COMBO(spc_combo, KC_SPC),
};

#endif

#ifdef TAP_DANCE_ENABLE

// Japanese keyboard specific characters

enum {
    TD_QUOTS,
    TD_PIPE_TILD,
};

static void td_quot_handler(tap_dance_state_t *state, void *user_data) {
    // 1 tap: ' , 2 taps: " , 3+ taps: `
    const uint16_t c =
        (state->count == 1) ? S(KC_7) :
        (state->count == 2) ? S(KC_2) :
        S(KC_LEFT_BRACKET);
    tap_code16(c);
}

tap_dance_action_t tap_dance_actions[] = {
    [TD_QUOTS] = ACTION_TAP_DANCE_FN(td_quot_handler),
    [TD_PIPE_TILD] = ACTION_TAP_DANCE_DOUBLE(S(KC_INTERNATIONAL_3), S(KC_EQUAL)),
};

#endif
