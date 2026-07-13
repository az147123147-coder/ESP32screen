#include "app_usb.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "lvgl.h"
#include "lvgl_demo.h"
#include "lv_main_ui.h"
#include "pc_ai_ble.h"

#define AI_STATUS_BONDED_NOTICE_MS 3000U

typedef struct {
    lv_obj_t *main_ui;
    lv_obj_t *link_dot;
    lv_obj_t *link_value;
    lv_obj_t *codex_dot;
    lv_obj_t *codex_value;
    lv_obj_t *claude_dot;
    lv_obj_t *claude_value;
    lv_obj_t *session_dot[PC_AI_BLE_MAX_SESSIONS];
    lv_obj_t *session_source[PC_AI_BLE_MAX_SESSIONS];
    lv_obj_t *session_label[PC_AI_BLE_MAX_SESSIONS];
    lv_obj_t *session_value[PC_AI_BLE_MAX_SESSIONS];
    lv_obj_t *session_divider[PC_AI_BLE_MAX_SESSIONS];
    lv_obj_t *sessions_title;
    lv_obj_t *pair_info;
    lv_obj_t *pair_btn;
    lv_obj_t *pair_btn_label;
    lv_obj_t *forget_btn;
    lv_obj_t *forget_btn_label;
    lv_timer_t *check_timer;
    uint32_t bonded_notice_started_at;
    pc_ai_ble_snapshot_t rendered;
    bool bonded_notice_active;
    bool bonded_state_known;
    bool last_bonded;
    bool rendered_valid;
} ai_status_ui_t;

static ai_status_ui_t ai_status_ui;
static uint8_t ai_status_previous_dir;
static bool ai_status_dir_changed;
extern lv_obj_t *back_btn;

static lv_color_t color_text(void)
{
    return lv_color_hex(0xF5F7FA);
}

static lv_color_t color_muted(void)
{
    return lv_color_hex(0x8A9199);
}

static lv_color_t color_off(void)
{
    return lv_color_hex(0x667085);
}

static lv_color_t color_running(void)
{
    return lv_color_hex(0x32D583);
}

static lv_color_t color_busy(void)
{
    return lv_color_hex(0x56B4FF);
}

static lv_color_t color_waiting(void)
{
    return lv_color_hex(0xF5B942);
}

static lv_color_t color_done(void)
{
    return lv_color_hex(0x2DD4BF);
}

static lv_color_t color_error(void)
{
    return lv_color_hex(0xF97066);
}

static lv_color_t color_stale(void)
{
    return lv_color_hex(0xA0A7B0);
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, color, LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(label, 0, LV_STATE_DEFAULT);
    return label;
}

static lv_obj_t *create_dot(lv_obj_t *parent, int32_t x, int32_t y)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dot, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(dot, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dot, color_muted(), LV_STATE_DEFAULT);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return dot;
}

static lv_obj_t *create_divider(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t height)
{
    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_set_size(divider, width, height);
    lv_obj_set_pos(divider, x, y);
    lv_obj_set_style_radius(divider, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(divider, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(divider, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x2A2E35), LV_STATE_DEFAULT);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return divider;
}

static void create_status_column(lv_obj_t *parent, int32_t x, const char *name, lv_obj_t **dot, lv_obj_t **value)
{
    *dot = create_dot(parent, x, 64);

    lv_obj_t *name_label = create_label(parent, name, &lv_font_montserrat_18, color_text());
    lv_obj_set_pos(name_label, x + 18, 56);

    *value = create_label(parent, "UNKNOWN", &lv_font_montserrat_16, color_muted());
    lv_obj_set_size(*value, 130, 22);
    lv_obj_set_pos(*value, x, 80);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_label_set_long_mode(*value, LV_LABEL_LONG_CLIP);
}

static lv_obj_t *create_command_button(lv_obj_t *parent,
                                       int32_t x,
                                       int32_t y,
                                       int32_t width,
                                       int32_t height,
                                       const char *text,
                                       lv_obj_t **label)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x23272E), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x191C21), LV_STATE_DISABLED);

    *label = create_label(button, text, &lv_font_montserrat_16, color_text());
    lv_obj_center(*label);
    return button;
}

static void create_session_row(lv_obj_t *parent, uint8_t index, int32_t x, int32_t y)
{
    ai_status_ui.session_dot[index] = create_dot(parent, x, y + 7);

    ai_status_ui.session_source[index] = create_label(parent, "CDX", &lv_font_montserrat_14, color_muted());
    lv_obj_set_size(ai_status_ui.session_source[index], 32, 20);
    lv_obj_set_pos(ai_status_ui.session_source[index], x + 18, y + 2);
    lv_label_set_long_mode(ai_status_ui.session_source[index], LV_LABEL_LONG_CLIP);

    ai_status_ui.session_label[index] = create_label(parent, "-", &lv_font_montserrat_14, color_text());
    lv_obj_set_size(ai_status_ui.session_label[index], 86, 20);
    lv_obj_set_pos(ai_status_ui.session_label[index], x + 56, y + 2);
    lv_label_set_long_mode(ai_status_ui.session_label[index], LV_LABEL_LONG_CLIP);

    ai_status_ui.session_value[index] = create_label(parent, "STALE", &lv_font_montserrat_14, color_stale());
    lv_obj_set_size(ai_status_ui.session_value[index], 58, 20);
    lv_obj_set_pos(ai_status_ui.session_value[index], x + 148, y + 2);
    lv_obj_set_style_text_align(ai_status_ui.session_value[index], LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
    lv_label_set_long_mode(ai_status_ui.session_value[index], LV_LABEL_LONG_CLIP);

    ai_status_ui.session_divider[index] = create_divider(parent, x, y + 30, 206, 1);

    lv_obj_add_flag(ai_status_ui.session_dot[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ai_status_ui.session_source[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ai_status_ui.session_label[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ai_status_ui.session_value[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ai_status_ui.session_divider[index], LV_OBJ_FLAG_HIDDEN);
}

static const char *process_state_text(pc_ai_process_state_t state)
{
    switch (state)
    {
        case PC_AI_PROCESS_OFF:
            return "OFF";
        case PC_AI_PROCESS_RUNNING:
            return "IDLE";
        case PC_AI_PROCESS_BUSY:
            return "BUSY";
        default:
            return "UNKNOWN";
    }
}

static lv_color_t process_state_color(pc_ai_process_state_t state)
{
    switch (state)
    {
        case PC_AI_PROCESS_OFF:
            return color_off();
        case PC_AI_PROCESS_RUNNING:
            return color_running();
        case PC_AI_PROCESS_BUSY:
            return color_busy();
        default:
            return color_waiting();
    }
}

static void render_process(lv_obj_t *dot, lv_obj_t *label, pc_ai_process_state_t state)
{
    lv_color_t color = process_state_color(state);
    lv_obj_set_style_bg_color(dot, color, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, color, LV_STATE_DEFAULT);
    lv_label_set_text(label, process_state_text(state));
}

static const char *session_state_text(pc_ai_session_state_t state)
{
    switch (state)
    {
        case PC_AI_SESSION_OFF:
            return "OFF";
        case PC_AI_SESSION_IDLE:
            return "IDLE";
        case PC_AI_SESSION_BUSY:
            return "BUSY";
        case PC_AI_SESSION_WAIT:
            return "WAIT";
        case PC_AI_SESSION_DONE:
            return "DONE";
        case PC_AI_SESSION_FAILED:
            return "FAILED";
        case PC_AI_SESSION_STALE:
        default:
            return "STALE";
    }
}

static lv_color_t session_state_color(pc_ai_session_state_t state)
{
    switch (state)
    {
        case PC_AI_SESSION_OFF:
            return color_off();
        case PC_AI_SESSION_IDLE:
            return color_running();
        case PC_AI_SESSION_BUSY:
            return color_busy();
        case PC_AI_SESSION_WAIT:
            return color_waiting();
        case PC_AI_SESSION_DONE:
            return color_done();
        case PC_AI_SESSION_FAILED:
            return color_error();
        case PC_AI_SESSION_STALE:
        default:
            return color_stale();
    }
}

static bool session_summary_changed(const pc_ai_session_summary_t *a, const pc_ai_session_summary_t *b)
{
    if (a->product != b->product || a->state != b->state || a->label_len != b->label_len)
    {
        return true;
    }

    uint8_t label_length = a->label_len;
    if (label_length > PC_AI_BLE_SESSION_LABEL_MAX)
    {
        label_length = PC_AI_BLE_SESSION_LABEL_MAX;
    }

    return label_length > 0U && memcmp(a->label, b->label, label_length) != 0;
}

static bool sessions_changed(const pc_ai_ble_snapshot_t *a, const pc_ai_ble_snapshot_t *b)
{
    if (a->session_count != b->session_count ||
        a->total_session_count != b->total_session_count)
    {
        return true;
    }

    uint8_t count = a->session_count;
    if (count > PC_AI_BLE_MAX_SESSIONS)
    {
        count = PC_AI_BLE_MAX_SESSIONS;
    }

    for (uint8_t index = 0; index < count; index++)
    {
        if (session_summary_changed(&a->sessions[index], &b->sessions[index]))
        {
            return true;
        }
    }

    return false;
}

static void set_session_row_hidden(uint8_t index, bool hidden)
{
    if (hidden)
    {
        lv_obj_add_flag(ai_status_ui.session_dot[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ai_status_ui.session_source[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ai_status_ui.session_label[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ai_status_ui.session_value[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ai_status_ui.session_divider[index], LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(ai_status_ui.session_dot[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ai_status_ui.session_source[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ai_status_ui.session_label[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ai_status_ui.session_value[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ai_status_ui.session_divider[index], LV_OBJ_FLAG_HIDDEN);
    }
}

static void render_session(uint8_t index, const pc_ai_session_summary_t *session)
{
    char label[PC_AI_BLE_SESSION_LABEL_MAX + 1U];
    uint8_t label_length = session->label_len;
    lv_color_t color = session_state_color(session->state);

    if (label_length > PC_AI_BLE_SESSION_LABEL_MAX)
    {
        label_length = PC_AI_BLE_SESSION_LABEL_MAX;
    }

    memcpy(label, session->label, label_length);
    label[label_length] = '\0';

    lv_label_set_text(ai_status_ui.session_source[index],
                      session->product == PC_AI_PRODUCT_CLAUDE ? "CLD" : "CDX");
    lv_label_set_text(ai_status_ui.session_label[index], label_length > 0U ? label : "-");
    lv_label_set_text(ai_status_ui.session_value[index], session_state_text(session->state));
    lv_obj_set_style_bg_color(ai_status_ui.session_dot[index], color, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ai_status_ui.session_value[index], color, LV_STATE_DEFAULT);
    set_session_row_hidden(index, false);
}

static void render_sessions(const pc_ai_ble_snapshot_t *snapshot)
{
    uint8_t count = snapshot->session_count;
    if (count > PC_AI_BLE_MAX_SESSIONS)
    {
        count = PC_AI_BLE_MAX_SESSIONS;
    }

    for (uint8_t index = 0; index < PC_AI_BLE_MAX_SESSIONS; index++)
    {
        if (index < count)
        {
            render_session(index, &snapshot->sessions[index]);
        }
        else
        {
            set_session_row_hidden(index, true);
        }
    }
}

static bool snapshot_changed(const pc_ai_ble_snapshot_t *a, const pc_ai_ble_snapshot_t *b)
{
    return a->link_state != b->link_state ||
           a->codex_state != b->codex_state ||
           a->claude_state != b->claude_state ||
           a->initialized != b->initialized ||
           a->host_synced != b->host_synced ||
           a->heartbeat_valid != b->heartbeat_valid ||
           a->bonded != b->bonded ||
           a->pairing_open != b->pairing_open ||
           a->passkey_pending != b->passkey_pending ||
           a->pairing_passkey != b->pairing_passkey ||
           a->pairing_remaining_seconds != b->pairing_remaining_seconds ||
           (a->heartbeat_age_ms >= PC_AI_BLE_HEARTBEAT_STALE_MS) !=
               (b->heartbeat_age_ms >= PC_AI_BLE_HEARTBEAT_STALE_MS) ||
           a->last_error != b->last_error ||
           sessions_changed(a, b);
}

static void render_link(const pc_ai_ble_snapshot_t *snapshot)
{
    const char *text = "DISABLED";
    lv_color_t color = color_muted();

    switch (snapshot->link_state)
    {
        case PC_AI_BLE_LINK_STARTING:
            text = "STARTING";
            color = color_waiting();
            break;
        case PC_AI_BLE_LINK_ADVERTISING:
            text = snapshot->pairing_open ? "PAIRING" : "ADVERTISING";
            color = color_waiting();
            break;
        case PC_AI_BLE_LINK_CONNECTED:
            text = "CONNECTED";
            color = color_waiting();
            break;
        case PC_AI_BLE_LINK_ENCRYPTED:
            if (snapshot->heartbeat_valid)
            {
                text = "ONLINE";
                color = color_running();
            }
            else if (snapshot->heartbeat_age_ms == UINT32_MAX)
            {
                text = "WAITING";
                color = color_waiting();
            }
            else if (snapshot->heartbeat_age_ms >= PC_AI_BLE_HEARTBEAT_STALE_MS)
            {
                text = "STALE";
                color = color_stale();
            }
            else
            {
                text = "WAITING";
                color = color_waiting();
            }
            break;
        case PC_AI_BLE_LINK_ERROR:
            text = "ERROR";
            color = color_error();
            break;
        default:
            break;
    }

    lv_obj_set_style_bg_color(ai_status_ui.link_dot, color, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ai_status_ui.link_value, color, LV_STATE_DEFAULT);
    lv_label_set_text(ai_status_ui.link_value, text);
}

static void render_pairing(const pc_ai_ble_snapshot_t *snapshot)
{
    bool show_pair_info = true;

    if (snapshot->passkey_pending)
    {
        lv_label_set_text_fmt(ai_status_ui.pair_info, "PAIR CODE %06lu", (unsigned long)snapshot->pairing_passkey);
        lv_obj_set_style_text_color(ai_status_ui.pair_info, color_waiting(), LV_STATE_DEFAULT);
    }
    else if (snapshot->pairing_open)
    {
        lv_label_set_text_fmt(ai_status_ui.pair_info, "PAIRING %lus", (unsigned long)snapshot->pairing_remaining_seconds);
        lv_obj_set_style_text_color(ai_status_ui.pair_info, color_waiting(), LV_STATE_DEFAULT);
    }
    else if (snapshot->bonded)
    {
        if (ai_status_ui.bonded_notice_active)
        {
            lv_label_set_text(ai_status_ui.pair_info, "BONDED");
            lv_obj_set_style_text_color(ai_status_ui.pair_info, color_running(), LV_STATE_DEFAULT);
        }
        else
        {
            show_pair_info = false;
        }
    }
    else
    {
        lv_label_set_text(ai_status_ui.pair_info, "NOT PAIRED");
        lv_obj_set_style_text_color(ai_status_ui.pair_info, color_muted(), LV_STATE_DEFAULT);
    }

    if (show_pair_info)
    {
        lv_obj_clear_flag(ai_status_ui.pair_info, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(ai_status_ui.pair_info, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(ai_status_ui.pair_btn_label, snapshot->pairing_open ? "CANCEL" : "PAIR");
    lv_obj_center(ai_status_ui.pair_btn_label);

    if (snapshot->host_synced && !snapshot->bonded)
    {
        lv_obj_clear_state(ai_status_ui.pair_btn, LV_STATE_DISABLED);
        lv_obj_set_style_text_color(ai_status_ui.pair_btn_label, color_text(), LV_STATE_DEFAULT);
    }
    else
    {
        lv_obj_add_state(ai_status_ui.pair_btn, LV_STATE_DISABLED);
        lv_obj_set_style_text_color(ai_status_ui.pair_btn_label, color_muted(), LV_STATE_DEFAULT);
    }

    if (snapshot->host_synced && snapshot->bonded && !snapshot->pairing_open)
    {
        lv_obj_clear_state(ai_status_ui.forget_btn, LV_STATE_DISABLED);
        lv_obj_set_style_text_color(ai_status_ui.forget_btn_label, color_text(), LV_STATE_DEFAULT);
    }
    else
    {
        lv_obj_add_state(ai_status_ui.forget_btn, LV_STATE_DISABLED);
        lv_obj_set_style_text_color(ai_status_ui.forget_btn_label, color_muted(), LV_STATE_DEFAULT);
    }
}

static bool update_bonded_notice(const pc_ai_ble_snapshot_t *snapshot)
{
    bool notice_changed = false;

    if (!ai_status_ui.bonded_state_known)
    {
        ai_status_ui.bonded_state_known = true;
        ai_status_ui.last_bonded = snapshot->bonded;
        if (snapshot->bonded)
        {
            ai_status_ui.bonded_notice_started_at = lv_tick_get();
            ai_status_ui.bonded_notice_active = true;
            notice_changed = true;
        }
    }
    else if (snapshot->bonded != ai_status_ui.last_bonded)
    {
        ai_status_ui.last_bonded = snapshot->bonded;
        ai_status_ui.bonded_notice_active = snapshot->bonded;
        if (snapshot->bonded)
        {
            ai_status_ui.bonded_notice_started_at = lv_tick_get();
        }
        notice_changed = true;
    }

    if (ai_status_ui.bonded_notice_active &&
        lv_tick_elaps(ai_status_ui.bonded_notice_started_at) >= AI_STATUS_BONDED_NOTICE_MS)
    {
        ai_status_ui.bonded_notice_active = false;
        notice_changed = true;
    }

    return notice_changed;
}

static void render_snapshot(const pc_ai_ble_snapshot_t *snapshot)
{
    render_link(snapshot);
    render_process(ai_status_ui.codex_dot, ai_status_ui.codex_value, snapshot->codex_state);
    render_process(ai_status_ui.claude_dot, ai_status_ui.claude_value, snapshot->claude_state);
    render_sessions(snapshot);
    render_pairing(snapshot);
    ai_status_ui.rendered = *snapshot;
    ai_status_ui.rendered_valid = true;
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (ai_status_ui.main_ui == NULL || !lv_obj_is_valid(ai_status_ui.main_ui))
    {
        return;
    }

    pc_ai_ble_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (pc_ai_ble_get_snapshot(&snapshot) != ESP_OK)
    {
        snapshot.link_state = PC_AI_BLE_LINK_ERROR;
        snapshot.codex_state = PC_AI_PROCESS_UNKNOWN;
        snapshot.claude_state = PC_AI_PROCESS_UNKNOWN;
        snapshot.last_error = ESP_ERR_INVALID_STATE;
    }

    bool bonded_notice_changed = update_bonded_notice(&snapshot);
    if (!ai_status_ui.rendered_valid || snapshot_changed(&snapshot, &ai_status_ui.rendered))
    {
        render_snapshot(&snapshot);
    }
    else if (bonded_notice_changed)
    {
        render_pairing(&snapshot);
    }
}

static void pair_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }

    pc_ai_ble_snapshot_t snapshot;
    if (pc_ai_ble_get_snapshot(&snapshot) != ESP_OK || snapshot.bonded)
    {
        return;
    }

    if (snapshot.pairing_open)
    {
        pc_ai_ble_cancel_pairing();
    }
    else
    {
        pc_ai_ble_start_pairing(PC_AI_BLE_DEFAULT_PAIRING_TIMEOUT_SECONDS);
    }

    ai_status_ui.rendered_valid = false;
}

static void forget_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }

    pc_ai_ble_forget_bonds();
    ai_status_ui.rendered_valid = false;
}

void app_usb_ui_init(void)
{
    if (ai_status_ui.main_ui != NULL)
    {
        lv_usb_del();
    }

    lv_hidden_box();
    ai_status_previous_dir = lcd_dev.dir;
    lvgl_set_display_dir(1U, true);
    ai_status_dir_changed = true;

    memset(&ai_status_ui, 0, sizeof(ai_status_ui));
    ai_status_ui.main_ui = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ai_status_ui.main_ui, lcd_dev.width, lcd_dev.height);
    lv_obj_set_pos(ai_status_ui.main_ui, 0, 0);
    lv_obj_set_style_radius(ai_status_ui.main_ui, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ai_status_ui.main_ui, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ai_status_ui.main_ui, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ai_status_ui.main_ui, lv_color_hex(0x0B0C0E), LV_STATE_DEFAULT);
    lv_obj_clear_flag(ai_status_ui.main_ui, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(ai_status_ui.main_ui, "AI STATUS", &lv_font_montserrat_24, color_text());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    create_status_column(ai_status_ui.main_ui, 20, "BLE", &ai_status_ui.link_dot, &ai_status_ui.link_value);
    create_status_column(ai_status_ui.main_ui, 170, "CODEX", &ai_status_ui.codex_dot, &ai_status_ui.codex_value);
    create_status_column(ai_status_ui.main_ui, 320, "CLAUDE", &ai_status_ui.claude_dot, &ai_status_ui.claude_value);
    create_divider(ai_status_ui.main_ui, 160, 56, 1, 48);
    create_divider(ai_status_ui.main_ui, 310, 56, 1, 48);
    create_divider(ai_status_ui.main_ui, 20, 108, 440, 1);

    ai_status_ui.sessions_title = create_label(ai_status_ui.main_ui, "SESSIONS", &lv_font_montserrat_14, color_muted());
    lv_obj_set_size(ai_status_ui.sessions_title, 440, 20);
    lv_obj_set_pos(ai_status_ui.sessions_title, 20, 114);

    create_session_row(ai_status_ui.main_ui, 0U, 20, 138);
    create_session_row(ai_status_ui.main_ui, 1U, 250, 138);
    create_session_row(ai_status_ui.main_ui, 2U, 20, 174);
    create_session_row(ai_status_ui.main_ui, 3U, 250, 174);
    create_session_row(ai_status_ui.main_ui, 4U, 20, 210);
    create_session_row(ai_status_ui.main_ui, 5U, 250, 210);
    create_divider(ai_status_ui.main_ui, 238, 134, 1, 107);

    ai_status_ui.pair_info = create_label(ai_status_ui.main_ui, "NOT PAIRED", &lv_font_montserrat_16, color_muted());
    lv_obj_set_size(ai_status_ui.pair_info, 440, 20);
    lv_obj_set_pos(ai_status_ui.pair_info, 20, 246);
    lv_obj_set_style_text_align(ai_status_ui.pair_info, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);

    ai_status_ui.pair_btn = create_command_button(ai_status_ui.main_ui, 108, 272, 120, 36, "PAIR", &ai_status_ui.pair_btn_label);
    ai_status_ui.forget_btn = create_command_button(ai_status_ui.main_ui, 252, 272, 120, 36, "FORGET", &ai_status_ui.forget_btn_label);
    lv_obj_add_event_cb(ai_status_ui.pair_btn, pair_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ai_status_ui.forget_btn, forget_button_cb, LV_EVENT_CLICKED, NULL);

    ai_status_ui.check_timer = lv_timer_create(status_timer_cb, 500, NULL);

    if (back_btn != NULL && lv_obj_is_valid(back_btn))
    {
        lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(back_btn, 0, 0);
        lv_obj_move_foreground(back_btn);
    }

    app_obj_general.del_parent = ai_status_ui.main_ui;
    app_obj_general.APP_Function = lv_usb_del;
    app_obj_general.app_state = NOT_DEL_STATE;
    app_obj_general.requires_sd = 0;
    status_timer_cb(NULL);
}

void lv_usb_del(void)
{
    pc_ai_ble_cancel_pairing();

    if (ai_status_ui.check_timer != NULL)
    {
        lv_timer_del(ai_status_ui.check_timer);
        ai_status_ui.check_timer = NULL;
    }

    if (ai_status_ui.main_ui != NULL && lv_obj_is_valid(ai_status_ui.main_ui))
    {
        lv_obj_del(ai_status_ui.main_ui);
    }

    memset(&ai_status_ui, 0, sizeof(ai_status_ui));
    app_obj_general.APP_Function = NULL;
    app_obj_general.del_parent = NULL;
    app_obj_general.app_state = NOT_DEL_STATE;
    app_obj_general.requires_sd = 0;

    if (ai_status_dir_changed)
    {
        lvgl_set_display_dir(ai_status_previous_dir, false);
        ai_status_dir_changed = false;
    }

    if (back_btn != NULL && lv_obj_is_valid(back_btn))
    {
        lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(back_btn, 0, 0);
        lv_obj_move_foreground(back_btn);
    }

    lv_display_box();
}
