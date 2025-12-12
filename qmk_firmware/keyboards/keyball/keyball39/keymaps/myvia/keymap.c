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

#define DEFAULT_LAYER 0
#define MOUSE_LAYER 4
#define MOUSE_ACTIVATE_THRESHOLD 50
#define MOUSE_BTN1_RETURN_TERM 160

typedef struct {
    bool          active;
    uint16_t      motion_accum;
    uint16_t      btn1_timer;
    bool          btn1_pressed;
    bool          btn1_waiting_return;
} mouse_layer_state_t;

static mouse_layer_state_t mouse_layer_state = {0};

static void mouse_layer_maybe_return(void);

static inline uint8_t abs8(int8_t v) {
    return v < 0 ? (uint8_t)(-v) : (uint8_t)v;
}

static bool mouse_motion_exceeds_threshold(const report_mouse_t *report) {
    uint16_t delta = (uint16_t)abs8(report->x) + (uint16_t)abs8(report->y) + (uint16_t)abs8(report->h) + (uint16_t)abs8(report->v);
    if (delta == 0) {
        return false;
    }
    mouse_layer_state.motion_accum += delta;
    if (mouse_layer_state.motion_accum >= MOUSE_ACTIVATE_THRESHOLD) {
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

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    mouse_layer_maybe_return();

    if (!is_mouse_layer_active()) {
        return true;
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

    return true;
}

layer_state_t layer_state_set_user(layer_state_t state) {
    // Enable scroll mode whenever layer 3 is active
    keyball_set_scroll_mode(layer_state_cmp(state, 3));
    return state;
}

#ifdef OLED_ENABLE

#    include "lib/oledkit/oledkit.h"

void oledkit_render_info_user(void) {
    if (!keyball_is_oled_on()) {
        return;
    }
    keyball_oled_render_keyinfo();
    keyball_oled_render_ballinfo();
    keyball_oled_render_layerinfo();
}

void oledkit_render_logo_user(void) {
    if (!keyball_is_oled_on()) {
        return;
    }
    keyball_oled_sync_inversion();

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

enum {
    TD_QUOT_DQUO,
    TD_PIPE_TILD,
};

tap_dance_action_t tap_dance_actions[] = {
    // Japanese keyboard specific characters
    [TD_QUOT_DQUO] = ACTION_TAP_DANCE_DOUBLE(S(KC_7), S(KC_2)),
    [TD_PIPE_TILD] = ACTION_TAP_DANCE_DOUBLE(S(KC_INTERNATIONAL_3), S(KC_EQUAL)),
};

#endif
