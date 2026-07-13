#include "pc_ai_ble.h"

#include <limits.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define PC_AI_BLE_DEVICE_NAME "ESP32-AI-Status"
#define PC_AI_BLE_VALID_FLAGS 0x0FU
#define PC_AI_BLE_DEVICE_CAPABILITIES 0x0FU
#define PC_AI_BLE_ADVERTISING_RETRY_US 1000000ULL

typedef struct {
    bool initialized;
    bool initializing;
    bool host_synced;
    bool advertising;
    bool connected;
    bool encrypted;
    bool bonded;
    bool pairing_open;
    bool passkey_pending;
    uint16_t connection_handle;
    uint8_t sequence;
    uint8_t codex_state;
    uint8_t claude_state;
    uint8_t flags;
    bool session_pending_active;
    uint8_t session_pending_sequence;
    uint8_t session_pending_count;
    uint8_t session_pending_total_count;
    uint8_t session_pending_mask;
    pc_ai_session_summary_t session_pending[PC_AI_BLE_MAX_SESSIONS];
    uint8_t session_count;
    uint8_t total_session_count;
    pc_ai_session_summary_t sessions[PC_AI_BLE_MAX_SESSIONS];
    uint32_t passkey;
    int64_t pairing_deadline_us;
    int64_t last_heartbeat_us;
    int64_t sessions_committed_us;
    esp_err_t last_error;
} pc_ai_ble_state_t;

static const char *TAG = "PC_AI_BLE";
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static pc_ai_ble_state_t s_state = {
    .connection_handle = BLE_HS_CONN_HANDLE_NONE,
    .codex_state = PC_AI_PROCESS_UNKNOWN,
    .claude_state = PC_AI_PROCESS_UNKNOWN,
    .last_error = ESP_OK,
};
static esp_timer_handle_t s_pairing_timer;
static esp_timer_handle_t s_advertising_retry_timer;
static TaskHandle_t s_host_task;
static uint8_t s_own_address_type;
static uint16_t s_status_value_handle;
static uint16_t s_device_info_value_handle;

static const ble_uuid128_t s_service_uuid =
    BLE_UUID128_INIT(0x00, 0x10, 0x7c, 0x5a, 0x2e, 0x6d, 0x2d, 0x9f,
                     0x9b, 0x4c, 0x15, 0x1b, 0x01, 0x00, 0x51, 0x7f);
static const ble_uuid128_t s_status_uuid =
    BLE_UUID128_INIT(0x00, 0x10, 0x7c, 0x5a, 0x2e, 0x6d, 0x2d, 0x9f,
                     0x9b, 0x4c, 0x15, 0x1b, 0x02, 0x00, 0x51, 0x7f);
static const ble_uuid128_t s_device_info_uuid =
    BLE_UUID128_INIT(0x00, 0x10, 0x7c, 0x5a, 0x2e, 0x6d, 0x2d, 0x9f,
                     0x9b, 0x4c, 0x15, 0x1b, 0x03, 0x00, 0x51, 0x7f);

static const uint8_t s_device_info[PC_AI_BLE_STATUS_FRAME_SIZE] = {
    PC_AI_BLE_PROTOCOL_MAGIC,
    PC_AI_BLE_PROTOCOL_VERSION,
    PC_AI_BLE_STATUS_FRAME_SIZE,
    PC_AI_PROCESS_UNKNOWN,
    PC_AI_BLE_DEVICE_CAPABILITIES,
    PC_AI_BLE_HEARTBEAT_INTERVAL_SECONDS,
    PC_AI_BLE_HEARTBEAT_STALE_MS / 1000U,
    0xA5,
};

void ble_store_config_init(void);

static int pc_ai_ble_gap_event(struct ble_gap_event *event, void *arg);
static esp_err_t pc_ai_ble_start_advertising(void);
static void pc_ai_ble_advertising_retry(void *arg);
static int pc_ai_ble_gatt_access(uint16_t connection_handle,
                                 uint16_t attribute_handle,
                                 struct ble_gatt_access_ctxt *context,
                                 void *arg);

static const struct ble_gatt_svc_def s_gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_status_uuid.u,
                .access_cb = pc_ai_ble_gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_ENC |
                         BLE_GATT_CHR_F_WRITE_AUTHEN,
                .val_handle = &s_status_value_handle,
            },
            {
                .uuid = &s_device_info_uuid.u,
                .access_cb = pc_ai_ble_gatt_access,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_device_info_value_handle,
            },
            {0},
        },
    },
    {0},
};

static uint8_t pc_ai_ble_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;

    for (size_t index = 0; index < length; index++)
    {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x07U)
                                : (uint8_t)(crc << 1);
        }
    }

    return crc;
}

static void pc_ai_ble_set_error(esp_err_t error)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.last_error = error;
    portEXIT_CRITICAL(&s_state_lock);
}

static void pc_ai_ble_clear_session_pending_locked(void)
{
    s_state.session_pending_active = false;
    s_state.session_pending_sequence = 0;
    s_state.session_pending_count = 0;
    s_state.session_pending_total_count = 0;
    s_state.session_pending_mask = 0;
    memset(s_state.session_pending, 0, sizeof(s_state.session_pending));
}

static void pc_ai_ble_clear_committed_sessions_locked(void)
{
    s_state.session_count = 0;
    s_state.total_session_count = 0;
    memset(s_state.sessions, 0, sizeof(s_state.sessions));
    s_state.sessions_committed_us = 0;
}

static void pc_ai_ble_clear_sessions_locked(void)
{
    pc_ai_ble_clear_session_pending_locked();
    pc_ai_ble_clear_committed_sessions_locked();
}

static bool pc_ai_ble_heartbeat_valid_locked(int64_t now_us)
{
    return s_state.host_synced && s_state.connected && s_state.encrypted &&
           s_state.last_heartbeat_us > 0 && now_us >= s_state.last_heartbeat_us &&
           now_us - s_state.last_heartbeat_us <=
               (int64_t)PC_AI_BLE_HEARTBEAT_STALE_MS * 1000LL;
}

static void pc_ai_ble_invalidate_status_locked(void)
{
    s_state.codex_state = PC_AI_PROCESS_UNKNOWN;
    s_state.claude_state = PC_AI_PROCESS_UNKNOWN;
    s_state.flags = 0;
    s_state.last_heartbeat_us = 0;
    pc_ai_ble_clear_sessions_locked();
}

static bool pc_ai_ble_pairing_active(int64_t now_us)
{
    bool active;

    portENTER_CRITICAL(&s_state_lock);
    active = s_state.pairing_open && now_us < s_state.pairing_deadline_us;
    if (!active && s_state.pairing_open)
    {
        s_state.pairing_open = false;
        s_state.passkey_pending = false;
        s_state.passkey = 0;
        s_state.pairing_deadline_us = 0;
    }
    portEXIT_CRITICAL(&s_state_lock);

    return active;
}

static void pc_ai_ble_terminate_if_unencrypted(uint16_t connection_handle)
{
    struct ble_gap_conn_desc descriptor;

    if (connection_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return;
    }

    if (ble_gap_conn_find(connection_handle, &descriptor) == 0 &&
        !descriptor.sec_state.encrypted)
    {
        ble_gap_terminate(connection_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static esp_err_t pc_ai_ble_refresh_bond_state(void)
{
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int count = 0;
    int rc = ble_store_util_bonded_peers(peers, &count,
                                         CONFIG_BT_NIMBLE_MAX_BONDS);

    if (rc != 0)
    {
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.bonded = count > 0;
    portEXIT_CRITICAL(&s_state_lock);

    return ESP_OK;
}

static esp_err_t pc_ai_ble_peer_is_bonded(const ble_addr_t *peer_identity,
                                          bool *bonded)
{
    ble_addr_t bonded_peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int peer_count = 0;
    int rc;

    if (peer_identity == NULL || bonded == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *bonded = false;
    rc = ble_store_util_bonded_peers(bonded_peers, &peer_count,
                                     CONFIG_BT_NIMBLE_MAX_BONDS);
    if (rc != 0)
    {
        return ESP_FAIL;
    }

    for (int index = 0; index < peer_count; index++)
    {
        if (ble_addr_cmp(peer_identity, &bonded_peers[index]) == 0)
        {
            *bonded = true;
            break;
        }
    }

    return ESP_OK;
}

static void pc_ai_ble_schedule_advertising_retry(void)
{
    esp_err_t result;
    bool retry;

    portENTER_CRITICAL(&s_state_lock);
    retry = s_state.initialized && s_state.host_synced && !s_state.connected;
    portEXIT_CRITICAL(&s_state_lock);
    if (!retry || s_advertising_retry_timer == NULL)
    {
        return;
    }

    result = esp_timer_start_periodic(s_advertising_retry_timer,
                                      PC_AI_BLE_ADVERTISING_RETRY_US);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
    {
        pc_ai_ble_set_error(result);
    }
}

static esp_err_t pc_ai_ble_advertising_failed(esp_err_t error)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state.advertising = false;
    s_state.last_error = error;
    portEXIT_CRITICAL(&s_state_lock);
    pc_ai_ble_schedule_advertising_retry();
    return error;
}

static esp_err_t pc_ai_ble_start_advertising(void)
{
    struct ble_hs_adv_fields advertising_fields;
    struct ble_hs_adv_fields response_fields;
    struct ble_gap_adv_params parameters;
    const char *name;
    bool can_advertise;
    int rc;

    portENTER_CRITICAL(&s_state_lock);
    can_advertise = s_state.initialized && s_state.host_synced && !s_state.connected;
    if (!can_advertise)
    {
        s_state.advertising = false;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (!can_advertise)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (ble_gap_adv_active())
    {
        portENTER_CRITICAL(&s_state_lock);
        s_state.advertising = true;
        s_state.last_error = ESP_OK;
        portEXIT_CRITICAL(&s_state_lock);
        if (s_advertising_retry_timer != NULL)
        {
            esp_timer_stop(s_advertising_retry_timer);
        }
        return ESP_OK;
    }

    memset(&advertising_fields, 0, sizeof(advertising_fields));
    advertising_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    advertising_fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    advertising_fields.num_uuids128 = 1;
    advertising_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&advertising_fields);
    if (rc != 0)
    {
        return pc_ai_ble_advertising_failed(ESP_FAIL);
    }

    name = ble_svc_gap_device_name();
    memset(&response_fields, 0, sizeof(response_fields));
    response_fields.name = (uint8_t *)name;
    response_fields.name_len = strlen(name);
    response_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&response_fields);
    if (rc != 0)
    {
        return pc_ai_ble_advertising_failed(ESP_FAIL);
    }

    memset(&parameters, 0, sizeof(parameters));
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_address_type, NULL, BLE_HS_FOREVER,
                           &parameters, pc_ai_ble_gap_event, NULL);
    if (rc != 0)
    {
        return pc_ai_ble_advertising_failed(ESP_FAIL);
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.advertising = true;
    s_state.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_state_lock);

    if (s_advertising_retry_timer != NULL)
    {
        esp_timer_stop(s_advertising_retry_timer);
    }

    return ESP_OK;
}

static void pc_ai_ble_advertising_retry(void *arg)
{
    bool retry;

    (void)arg;

    portENTER_CRITICAL(&s_state_lock);
    retry = s_state.initialized && s_state.host_synced && !s_state.connected &&
            !s_state.advertising;
    portEXIT_CRITICAL(&s_state_lock);

    if (retry)
    {
        pc_ai_ble_start_advertising();
    }
    else if (s_advertising_retry_timer != NULL)
    {
        esp_timer_stop(s_advertising_retry_timer);
    }
}

static int pc_ai_ble_get_secure_connection(uint16_t connection_handle,
                                           struct ble_gap_conn_desc *descriptor)
{
    int rc = ble_gap_conn_find(connection_handle, descriptor);

    if (rc != 0 || !descriptor->sec_state.encrypted)
    {
        return BLE_ATT_ERR_INSUFFICIENT_ENC;
    }
    if (!descriptor->sec_state.authenticated)
    {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    return 0;
}

static int pc_ai_ble_write_aggregate_status(uint16_t connection_handle,
                                            const uint8_t *frame)
{
    struct ble_gap_conn_desc descriptor;
    int64_t now_us;
    int rc;

    if (frame[0] != PC_AI_BLE_PROTOCOL_MAGIC ||
        frame[1] != PC_AI_BLE_PROTOCOL_VERSION ||
        frame[3] > PC_AI_PROCESS_UNKNOWN ||
        frame[4] > PC_AI_PROCESS_UNKNOWN ||
        (frame[5] & (uint8_t)~PC_AI_BLE_VALID_FLAGS) != 0 ||
        frame[6] != 0 ||
        frame[7] != pc_ai_ble_crc8(frame, PC_AI_BLE_STATUS_FRAME_SIZE - 1U))
    {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }

    rc = pc_ai_ble_get_secure_connection(connection_handle, &descriptor);
    if (rc != 0)
    {
        return rc;
    }

    now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_state_lock);
    if (s_state.session_pending_active &&
        s_state.session_pending_sequence != frame[2])
    {
        pc_ai_ble_clear_session_pending_locked();
    }
    s_state.sequence = frame[2];
    s_state.codex_state = frame[3];
    s_state.claude_state = frame[4];
    s_state.flags = frame[5];
    s_state.last_heartbeat_us = now_us;
    s_state.encrypted = true;
    s_state.bonded = s_state.bonded || descriptor.sec_state.bonded;
    s_state.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_state_lock);

    return 0;
}

static bool pc_ai_ble_session_frame_valid(const uint8_t *frame)
{
    uint8_t record_index = frame[3];
    uint8_t record_count = frame[4];
    uint8_t label_length = frame[8];

    if (frame[0] != PC_AI_BLE_SESSION_PROTOCOL_MAGIC ||
        frame[1] != PC_AI_BLE_PROTOCOL_VERSION ||
        frame[19] != pc_ai_ble_crc8(frame, PC_AI_BLE_SESSION_FRAME_SIZE - 1U))
    {
        return false;
    }

    if (record_index == UINT8_MAX)
    {
        for (size_t index = 4; index < PC_AI_BLE_SESSION_FRAME_SIZE - 1U; index++)
        {
            if (frame[index] != 0)
            {
                return false;
            }
        }
        return true;
    }

    if (record_count == 0 || record_count > PC_AI_BLE_MAX_SESSIONS ||
        record_index >= record_count || frame[5] < record_count ||
        frame[6] > PC_AI_PRODUCT_CLAUDE ||
        frame[7] > PC_AI_SESSION_STALE ||
        label_length > PC_AI_BLE_SESSION_LABEL_MAX)
    {
        return false;
    }

    for (size_t index = 0; index < PC_AI_BLE_SESSION_LABEL_MAX; index++)
    {
        uint8_t character = frame[9U + index];

        if (index < label_length)
        {
            if (character < 0x20U || character > 0x7EU)
            {
                return false;
            }
        }
        else if (character != 0)
        {
            return false;
        }
    }

    return true;
}

static int pc_ai_ble_write_session_status(uint16_t connection_handle,
                                          const uint8_t *frame)
{
    struct ble_gap_conn_desc descriptor;
    pc_ai_session_summary_t record;
    uint8_t record_index = frame[3];
    uint8_t expected_mask;
    int64_t now_us;
    int rc;

    if (!pc_ai_ble_session_frame_valid(frame))
    {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }

    rc = pc_ai_ble_get_secure_connection(connection_handle, &descriptor);
    if (rc != 0)
    {
        return rc;
    }
    now_us = esp_timer_get_time();

    if (record_index == UINT8_MAX)
    {
        portENTER_CRITICAL(&s_state_lock);
        if (!pc_ai_ble_heartbeat_valid_locked(now_us))
        {
            pc_ai_ble_clear_sessions_locked();
            portEXIT_CRITICAL(&s_state_lock);
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        if (s_state.sequence != frame[2])
        {
            portEXIT_CRITICAL(&s_state_lock);
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        pc_ai_ble_clear_sessions_locked();
        s_state.encrypted = true;
        s_state.bonded = s_state.bonded || descriptor.sec_state.bonded;
        s_state.last_error = ESP_OK;
        portEXIT_CRITICAL(&s_state_lock);
        return 0;
    }

    memset(&record, 0, sizeof(record));
    record.product = (pc_ai_product_t)frame[6];
    record.state = (pc_ai_session_state_t)frame[7];
    record.label_len = frame[8];
    memcpy(record.label, &frame[9], record.label_len);

    portENTER_CRITICAL(&s_state_lock);
    if (!pc_ai_ble_heartbeat_valid_locked(now_us))
    {
        pc_ai_ble_clear_sessions_locked();
        portEXIT_CRITICAL(&s_state_lock);
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    if (s_state.sequence != frame[2])
    {
        portEXIT_CRITICAL(&s_state_lock);
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    if (!s_state.session_pending_active ||
        s_state.session_pending_sequence != frame[2])
    {
        pc_ai_ble_clear_session_pending_locked();
        s_state.session_pending_active = true;
        s_state.session_pending_sequence = frame[2];
        s_state.session_pending_count = frame[4];
        s_state.session_pending_total_count = frame[5];
    }
    else if (s_state.session_pending_count != frame[4] ||
             s_state.session_pending_total_count != frame[5])
    {
        pc_ai_ble_clear_session_pending_locked();
        portEXIT_CRITICAL(&s_state_lock);
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }

    s_state.session_pending[record_index] = record;
    s_state.session_pending_mask |= (uint8_t)(1U << record_index);
    expected_mask = (uint8_t)((1U << s_state.session_pending_count) - 1U);
    if (s_state.session_pending_mask == expected_mask)
    {
        s_state.session_count = s_state.session_pending_count;
        s_state.total_session_count = s_state.session_pending_total_count;
        memcpy(s_state.sessions, s_state.session_pending, sizeof(s_state.sessions));
        s_state.sessions_committed_us = now_us;
        pc_ai_ble_clear_session_pending_locked();
    }
    s_state.encrypted = true;
    s_state.bonded = s_state.bonded || descriptor.sec_state.bonded;
    s_state.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_state_lock);

    return 0;
}

static int pc_ai_ble_write_status(uint16_t connection_handle,
                                  struct os_mbuf *buffer)
{
    uint8_t frame[PC_AI_BLE_SESSION_FRAME_SIZE];
    uint16_t packet_length = OS_MBUF_PKTLEN(buffer);
    uint16_t copied_length = 0;
    int rc;

    if (packet_length != PC_AI_BLE_STATUS_FRAME_SIZE &&
        packet_length != PC_AI_BLE_SESSION_FRAME_SIZE)
    {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(buffer, frame, packet_length, &copied_length);
    if (rc != 0 || copied_length != packet_length)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (packet_length == PC_AI_BLE_STATUS_FRAME_SIZE)
    {
        return pc_ai_ble_write_aggregate_status(connection_handle, frame);
    }

    return pc_ai_ble_write_session_status(connection_handle, frame);
}

static int pc_ai_ble_gatt_access(uint16_t connection_handle,
                                 uint16_t attribute_handle,
                                 struct ble_gatt_access_ctxt *context,
                                 void *arg)
{
    (void)arg;

    if (attribute_handle == s_device_info_value_handle &&
        context->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        return os_mbuf_append(context->om, s_device_info, sizeof(s_device_info)) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (attribute_handle == s_status_value_handle &&
        context->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        return pc_ai_ble_write_status(connection_handle, context->om);
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void pc_ai_ble_pairing_timeout(void *arg)
{
    int64_t now_us;
    uint16_t connection_handle;
    bool terminate;

    (void)arg;

    now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_state_lock);
    if (!s_state.pairing_open || now_us < s_state.pairing_deadline_us)
    {
        portEXIT_CRITICAL(&s_state_lock);
        return;
    }
    s_state.pairing_open = false;
    s_state.passkey_pending = false;
    s_state.passkey = 0;
    s_state.pairing_deadline_us = 0;
    connection_handle = s_state.connection_handle;
    terminate = s_state.connected && !s_state.encrypted;
    portEXIT_CRITICAL(&s_state_lock);

    if (terminate)
    {
        pc_ai_ble_terminate_if_unencrypted(connection_handle);
    }
}

static void pc_ai_ble_on_reset(int reason)
{
    if (s_advertising_retry_timer != NULL)
    {
        esp_timer_stop(s_advertising_retry_timer);
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.host_synced = false;
    s_state.advertising = false;
    s_state.connected = false;
    s_state.encrypted = false;
    s_state.connection_handle = BLE_HS_CONN_HANDLE_NONE;
    s_state.pairing_open = false;
    s_state.passkey_pending = false;
    s_state.passkey = 0;
    s_state.pairing_deadline_us = 0;
    pc_ai_ble_invalidate_status_locked();
    s_state.last_error = ESP_FAIL;
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGE(TAG, "NimBLE host reset: %d", reason);
}

static void pc_ai_ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);

    if (rc == 0)
    {
        rc = ble_hs_id_infer_auto(0, &s_own_address_type);
    }
    if (rc != 0)
    {
        pc_ai_ble_set_error(ESP_FAIL);
        return;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.host_synced = true;
    s_state.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_state_lock);

    if (pc_ai_ble_refresh_bond_state() != ESP_OK)
    {
        pc_ai_ble_set_error(ESP_FAIL);
    }

    pc_ai_ble_start_advertising();
}

static int pc_ai_ble_handle_link_established(struct ble_gap_event *event)
{
    struct ble_gap_conn_desc descriptor;
    bool pairing_allowed;
    bool peer_bonded = false;
    uint16_t connection_handle;
    esp_err_t bond_result;
    int rc;

    if (event->link_estab.status != 0)
    {
        pc_ai_ble_start_advertising();
        return 0;
    }

    connection_handle = event->link_estab.conn_handle;
    if (s_advertising_retry_timer != NULL)
    {
        esp_timer_stop(s_advertising_retry_timer);
    }
    pairing_allowed = pc_ai_ble_pairing_active(esp_timer_get_time());
    rc = ble_gap_conn_find(connection_handle, &descriptor);
    if (rc != 0)
    {
        pc_ai_ble_set_error(ESP_FAIL);
        ble_gap_terminate(connection_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    bond_result = pc_ai_ble_peer_is_bonded(&descriptor.peer_id_addr,
                                           &peer_bonded);
    if (bond_result != ESP_OK)
    {
        pc_ai_ble_set_error(bond_result);
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.advertising = false;
    s_state.connected = true;
    s_state.encrypted = false;
    s_state.connection_handle = connection_handle;
    s_state.passkey_pending = false;
    s_state.passkey = 0;
    pc_ai_ble_invalidate_status_locked();
    portEXIT_CRITICAL(&s_state_lock);

    if (!peer_bonded && !pairing_allowed)
    {
        ble_gap_terminate(connection_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    rc = ble_gap_security_initiate(connection_handle);
    if (rc != 0 && rc != BLE_HS_EALREADY)
    {
        pc_ai_ble_set_error(ESP_FAIL);
        ble_gap_terminate(connection_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int pc_ai_ble_handle_encryption_change(struct ble_gap_event *event)
{
    struct ble_gap_conn_desc descriptor;
    bool secure = false;

    if (event->enc_change.status == 0 &&
        ble_gap_conn_find(event->enc_change.conn_handle, &descriptor) == 0)
    {
        secure = descriptor.sec_state.encrypted && descriptor.sec_state.authenticated;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.encrypted = secure;
    s_state.passkey_pending = false;
    s_state.passkey = 0;
    if (secure)
    {
        s_state.bonded = s_state.bonded || descriptor.sec_state.bonded;
        s_state.last_error = ESP_OK;
    }
    portEXIT_CRITICAL(&s_state_lock);

    if (secure)
    {
        pc_ai_ble_cancel_pairing();
    }
    else
    {
        ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int pc_ai_ble_handle_passkey(struct ble_gap_event *event)
{
    struct ble_sm_io passkey_io;
    uint32_t passkey;
    int rc;

    if (event->passkey.params.action != BLE_SM_IOACT_DISP ||
        !pc_ai_ble_pairing_active(esp_timer_get_time()))
    {
        ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    passkey = 100000U + (esp_random() % 900000U);
    portENTER_CRITICAL(&s_state_lock);
    s_state.passkey = passkey;
    s_state.passkey_pending = true;
    portEXIT_CRITICAL(&s_state_lock);

    memset(&passkey_io, 0, sizeof(passkey_io));
    passkey_io.action = BLE_SM_IOACT_DISP;
    passkey_io.passkey = passkey;
    rc = ble_sm_inject_io(event->passkey.conn_handle, &passkey_io);
    if (rc != 0)
    {
        portENTER_CRITICAL(&s_state_lock);
        s_state.passkey = 0;
        s_state.passkey_pending = false;
        portEXIT_CRITICAL(&s_state_lock);
        pc_ai_ble_set_error(ESP_FAIL);
        ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int pc_ai_ble_handle_repeat_pairing(struct ble_gap_event *event)
{
    struct ble_gap_conn_desc descriptor;
    int rc;

    if (!pc_ai_ble_pairing_active(esp_timer_get_time()))
    {
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }

    rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &descriptor);
    if (rc != 0)
    {
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }

    rc = ble_store_util_delete_peer(&descriptor.peer_id_addr);
    if (rc != 0)
    {
        pc_ai_ble_set_error(ESP_FAIL);
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }

    if (pc_ai_ble_refresh_bond_state() != ESP_OK)
    {
        pc_ai_ble_set_error(ESP_FAIL);
    }

    return BLE_GAP_REPEAT_PAIRING_RETRY;
}

static int pc_ai_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    bool restart;

    (void)arg;

    switch (event->type)
    {
        case BLE_GAP_EVENT_LINK_ESTAB:
            return pc_ai_ble_handle_link_established(event);

        case BLE_GAP_EVENT_DISCONNECT:
            portENTER_CRITICAL(&s_state_lock);
            s_state.connected = false;
            s_state.encrypted = false;
            s_state.connection_handle = BLE_HS_CONN_HANDLE_NONE;
            s_state.passkey_pending = false;
            s_state.passkey = 0;
            pc_ai_ble_invalidate_status_locked();
            portEXIT_CRITICAL(&s_state_lock);
            pc_ai_ble_start_advertising();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            portENTER_CRITICAL(&s_state_lock);
            s_state.advertising = false;
            restart = s_state.initialized && s_state.host_synced && !s_state.connected;
            portEXIT_CRITICAL(&s_state_lock);
            if (restart)
            {
                pc_ai_ble_start_advertising();
            }
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            return pc_ai_ble_handle_encryption_change(event);

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            return pc_ai_ble_handle_passkey(event);

        case BLE_GAP_EVENT_REPEAT_PAIRING:
            return pc_ai_ble_handle_repeat_pairing(event);

        default:
            return 0;
    }
}

static void pc_ai_ble_host_task(void *arg)
{
    esp_timer_handle_t pairing_timer;
    esp_timer_handle_t advertising_retry_timer;
    esp_err_t deinit_result;

    (void)arg;

    nimble_port_run();

    portENTER_CRITICAL(&s_state_lock);
    pairing_timer = s_pairing_timer;
    advertising_retry_timer = s_advertising_retry_timer;
    s_state.initialized = false;
    s_state.initializing = true;
    s_state.host_synced = false;
    s_state.advertising = false;
    s_state.connected = false;
    s_state.encrypted = false;
    s_state.connection_handle = BLE_HS_CONN_HANDLE_NONE;
    s_state.pairing_open = false;
    s_state.passkey_pending = false;
    s_state.passkey = 0;
    s_state.pairing_deadline_us = 0;
    pc_ai_ble_invalidate_status_locked();
    s_state.last_error = ESP_ERR_INVALID_STATE;
    s_host_task = NULL;
    portEXIT_CRITICAL(&s_state_lock);

    if (pairing_timer != NULL)
    {
        esp_timer_stop(pairing_timer);
    }
    if (advertising_retry_timer != NULL)
    {
        esp_timer_stop(advertising_retry_timer);
    }

    deinit_result = nimble_port_deinit();

    portENTER_CRITICAL(&s_state_lock);
    s_state.initializing = false;
    if (deinit_result != ESP_OK)
    {
        s_state.last_error = deinit_result;
    }
    portEXIT_CRITICAL(&s_state_lock);

    vTaskDelete(NULL);
}

esp_err_t pc_ai_ble_init(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = pc_ai_ble_pairing_timeout,
        .name = "ai_pairing",
    };
    const esp_timer_create_args_t advertising_retry_timer_args = {
        .callback = pc_ai_ble_advertising_retry,
        .name = "ai_adv_retry",
    };
    esp_err_t result;
    int rc;

    portENTER_CRITICAL(&s_state_lock);
    if (s_state.initializing)
    {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state.initialized)
    {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_OK;
    }
    s_state.initializing = true;
    s_state.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_state_lock);

    result = nimble_port_init();
    if (result != ESP_OK)
    {
        goto fail;
    }

    ble_hs_cfg.reset_cb = pc_ai_ble_on_reset;
    ble_hs_cfg.sync_cb = pc_ai_ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(s_gatt_services);
    if (rc != 0)
    {
        result = ESP_FAIL;
        goto deinit;
    }
    rc = ble_gatts_add_svcs(s_gatt_services);
    if (rc != 0)
    {
        result = ESP_FAIL;
        goto deinit;
    }
    rc = ble_svc_gap_device_name_set(PC_AI_BLE_DEVICE_NAME);
    if (rc != 0)
    {
        result = ESP_FAIL;
        goto deinit;
    }

    ble_store_config_init();

    if (s_pairing_timer == NULL)
    {
        result = esp_timer_create(&timer_args, &s_pairing_timer);
        if (result != ESP_OK)
        {
            goto deinit;
        }
    }

    if (s_advertising_retry_timer == NULL)
    {
        result = esp_timer_create(&advertising_retry_timer_args,
                                  &s_advertising_retry_timer);
        if (result != ESP_OK)
        {
            goto deinit;
        }
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.initialized = true;
    portEXIT_CRITICAL(&s_state_lock);

    if (xTaskCreatePinnedToCore(pc_ai_ble_host_task,
                                "nimble_host",
                                CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,
                                NULL,
                                configMAX_PRIORITIES - 4,
                                &s_host_task,
                                CONFIG_BT_NIMBLE_PINNED_TO_CORE) != pdPASS)
    {
        result = ESP_ERR_NO_MEM;
        portENTER_CRITICAL(&s_state_lock);
        s_state.initialized = false;
        s_state.last_error = result;
        portEXIT_CRITICAL(&s_state_lock);
        goto deinit;
    }

    portENTER_CRITICAL(&s_state_lock);
    if (s_state.initialized && s_host_task != NULL)
    {
        s_state.initializing = false;
    }
    portEXIT_CRITICAL(&s_state_lock);

    return ESP_OK;

deinit:
    nimble_port_deinit();
fail:
    portENTER_CRITICAL(&s_state_lock);
    s_state.initialized = false;
    s_state.initializing = false;
    s_state.last_error = result;
    portEXIT_CRITICAL(&s_state_lock);
    return result;
}

esp_err_t pc_ai_ble_get_snapshot(pc_ai_ble_snapshot_t *snapshot)
{
    pc_ai_ble_state_t state;
    int64_t now_us;
    uint64_t age_ms;
    uint64_t session_age_ms;
    bool heartbeat_recent;
    bool clear_sessions;
    bool clear_committed_sessions = false;

    if (snapshot == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_state_lock);
    state = s_state;
    portEXIT_CRITICAL(&s_state_lock);

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->initialized = state.initialized;
    snapshot->host_synced = state.host_synced;
    snapshot->bonded = state.bonded;
    snapshot->sequence = state.sequence;
    snapshot->flags = state.flags;
    snapshot->last_error = state.last_error;

    if (!state.initialized)
    {
        snapshot->link_state = state.last_error == ESP_OK
                                   ? PC_AI_BLE_LINK_DISABLED
                                   : PC_AI_BLE_LINK_ERROR;
    }
    else if (state.encrypted)
    {
        snapshot->link_state = PC_AI_BLE_LINK_ENCRYPTED;
    }
    else if (state.connected)
    {
        snapshot->link_state = PC_AI_BLE_LINK_CONNECTED;
    }
    else if (state.advertising)
    {
        snapshot->link_state = PC_AI_BLE_LINK_ADVERTISING;
    }
    else if (state.last_error != ESP_OK)
    {
        snapshot->link_state = PC_AI_BLE_LINK_ERROR;
    }
    else
    {
        snapshot->link_state = PC_AI_BLE_LINK_STARTING;
    }

    if (state.last_heartbeat_us <= 0 || now_us < state.last_heartbeat_us)
    {
        snapshot->heartbeat_age_ms = UINT32_MAX;
        heartbeat_recent = false;
    }
    else
    {
        age_ms = (uint64_t)(now_us - state.last_heartbeat_us) / 1000U;
        snapshot->heartbeat_age_ms = age_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)age_ms;
        heartbeat_recent = age_ms <= PC_AI_BLE_HEARTBEAT_STALE_MS;
    }

    snapshot->heartbeat_valid = state.host_synced && state.connected &&
                                state.encrypted && heartbeat_recent;
    clear_sessions = !snapshot->heartbeat_valid;
    if (snapshot->heartbeat_valid && (state.flags & PC_AI_BLE_FLAG_DETECTOR_OK))
    {
        snapshot->codex_state = (pc_ai_process_state_t)state.codex_state;
        snapshot->claude_state = (pc_ai_process_state_t)state.claude_state;
    }
    else
    {
        snapshot->codex_state = PC_AI_PROCESS_UNKNOWN;
        snapshot->claude_state = PC_AI_PROCESS_UNKNOWN;
    }

    if (snapshot->heartbeat_valid && state.session_count > 0 &&
        state.session_count <= PC_AI_BLE_MAX_SESSIONS)
    {
        if (state.sessions_committed_us <= 0 || now_us < state.sessions_committed_us)
        {
            clear_committed_sessions = true;
        }
        else
        {
            session_age_ms = (uint64_t)(now_us - state.sessions_committed_us) / 1000U;
            if (session_age_ms <= PC_AI_BLE_SESSION_EXPIRE_MS)
            {
                snapshot->session_count = state.session_count;
                snapshot->total_session_count = state.total_session_count;
                memcpy(snapshot->sessions, state.sessions, sizeof(snapshot->sessions));
                if (session_age_ms > PC_AI_BLE_SESSION_STALE_MS)
                {
                    for (size_t index = 0; index < snapshot->session_count; index++)
                    {
                        snapshot->sessions[index].state = PC_AI_SESSION_STALE;
                    }
                }
            }
            else
            {
                clear_committed_sessions = true;
            }
        }
    }

    snapshot->pairing_open = state.pairing_open && now_us < state.pairing_deadline_us;
    if (snapshot->pairing_open)
    {
        uint64_t remaining_us = (uint64_t)(state.pairing_deadline_us - now_us);
        snapshot->pairing_remaining_seconds = (uint32_t)((remaining_us + 999999U) / 1000000U);
        snapshot->passkey_pending = state.passkey_pending;
        snapshot->pairing_passkey = state.passkey_pending ? state.passkey : 0;
    }

    if (clear_sessions)
    {
        portENTER_CRITICAL(&s_state_lock);
        if (!pc_ai_ble_heartbeat_valid_locked(now_us))
        {
            pc_ai_ble_clear_sessions_locked();
        }
        portEXIT_CRITICAL(&s_state_lock);
    }
    else if (clear_committed_sessions)
    {
        portENTER_CRITICAL(&s_state_lock);
        if (s_state.sessions_committed_us == state.sessions_committed_us)
        {
            pc_ai_ble_clear_committed_sessions_locked();
        }
        portEXIT_CRITICAL(&s_state_lock);
    }

    return ESP_OK;
}

esp_err_t pc_ai_ble_start_pairing(uint32_t timeout_seconds)
{
    uint16_t connection_handle;
    bool initiate_security;
    esp_err_t result;
    int rc;

    if (timeout_seconds == 0 || timeout_seconds > PC_AI_BLE_MAX_PAIRING_TIMEOUT_SECONDS)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_state_lock);
    if (!s_state.initialized || !s_state.host_synced || s_pairing_timer == NULL)
    {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_state_lock);

    result = esp_timer_stop(s_pairing_timer);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
    {
        return result;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.pairing_open = true;
    s_state.passkey_pending = false;
    s_state.passkey = 0;
    s_state.pairing_deadline_us = esp_timer_get_time() +
                                  (int64_t)timeout_seconds * 1000000LL;
    s_state.last_error = ESP_OK;
    connection_handle = s_state.connection_handle;
    initiate_security = s_state.connected && !s_state.encrypted;
    portEXIT_CRITICAL(&s_state_lock);

    result = esp_timer_start_once(s_pairing_timer,
                                  (uint64_t)timeout_seconds * 1000000ULL);
    if (result != ESP_OK)
    {
        pc_ai_ble_cancel_pairing();
        pc_ai_ble_set_error(result);
        return result;
    }

    if (initiate_security)
    {
        rc = ble_gap_security_initiate(connection_handle);
        if (rc != 0 && rc != BLE_HS_EALREADY)
        {
            pc_ai_ble_cancel_pairing();
            pc_ai_ble_set_error(ESP_FAIL);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t pc_ai_ble_cancel_pairing(void)
{
    uint16_t connection_handle;
    bool terminate;
    esp_err_t result = ESP_OK;

    if (s_pairing_timer != NULL)
    {
        result = esp_timer_stop(s_pairing_timer);
        if (result == ESP_ERR_INVALID_STATE)
        {
            result = ESP_OK;
        }
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.pairing_open = false;
    s_state.passkey_pending = false;
    s_state.passkey = 0;
    s_state.pairing_deadline_us = 0;
    connection_handle = s_state.connection_handle;
    terminate = s_state.connected && !s_state.encrypted;
    portEXIT_CRITICAL(&s_state_lock);

    if (terminate)
    {
        pc_ai_ble_terminate_if_unencrypted(connection_handle);
    }

    return result;
}

esp_err_t pc_ai_ble_forget_bonds(void)
{
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    uint16_t connection_handle;
    bool connected;
    int count = 0;
    int rc;

    portENTER_CRITICAL(&s_state_lock);
    if (!s_state.initialized || !s_state.host_synced)
    {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    connection_handle = s_state.connection_handle;
    connected = s_state.connected;
    portEXIT_CRITICAL(&s_state_lock);

    pc_ai_ble_cancel_pairing();

    rc = ble_store_util_bonded_peers(peers, &count,
                                     CONFIG_BT_NIMBLE_MAX_BONDS);
    if (rc != 0)
    {
        pc_ai_ble_set_error(ESP_FAIL);
        return ESP_FAIL;
    }

    for (int index = 0; index < count; index++)
    {
        rc = ble_store_util_delete_peer(&peers[index]);
        if (rc != 0)
        {
            pc_ai_ble_set_error(ESP_FAIL);
            return ESP_FAIL;
        }
    }

    if (pc_ai_ble_refresh_bond_state() != ESP_OK)
    {
        pc_ai_ble_set_error(ESP_FAIL);
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&s_state_lock);
    pc_ai_ble_invalidate_status_locked();
    s_state.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_state_lock);

    if (connected)
    {
        rc = ble_gap_terminate(connection_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_ENOTCONN && rc != BLE_HS_EALREADY)
        {
            pc_ai_ble_set_error(ESP_FAIL);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}
