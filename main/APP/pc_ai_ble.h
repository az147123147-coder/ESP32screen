#ifndef PC_AI_BLE_H
#define PC_AI_BLE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define PC_AI_BLE_PROTOCOL_MAGIC 0xA1U
#define PC_AI_BLE_SESSION_PROTOCOL_MAGIC 0xA2U
#define PC_AI_BLE_PROTOCOL_VERSION 0x01U
#define PC_AI_BLE_STATUS_FRAME_SIZE 8U
#define PC_AI_BLE_SESSION_FRAME_SIZE 20U
#define PC_AI_BLE_MAX_SESSIONS 6U
#define PC_AI_BLE_SESSION_LABEL_MAX 10U
#define PC_AI_BLE_HEARTBEAT_INTERVAL_SECONDS 2U
#define PC_AI_BLE_HEARTBEAT_STALE_MS 10000U
#define PC_AI_BLE_SESSION_STALE_MS 10000U
#define PC_AI_BLE_SESSION_EXPIRE_MS 30000U
#define PC_AI_BLE_DEFAULT_PAIRING_TIMEOUT_SECONDS 60U
#define PC_AI_BLE_MAX_PAIRING_TIMEOUT_SECONDS 300U

#define PC_AI_BLE_FLAG_DETECTOR_OK (1U << 0)
#define PC_AI_BLE_FLAG_CODEX_EXE (1U << 1)
#define PC_AI_BLE_FLAG_CODEX_HOST_EXE (1U << 2)
#define PC_AI_BLE_FLAG_CLAUDE_EXE (1U << 3)

typedef enum {
    PC_AI_PROCESS_OFF = 0,
    PC_AI_PROCESS_RUNNING = 1,
    PC_AI_PROCESS_BUSY = 2,
    PC_AI_PROCESS_UNKNOWN = 3
} pc_ai_process_state_t;

typedef enum {
    PC_AI_PRODUCT_CODEX = 0,
    PC_AI_PRODUCT_CLAUDE = 1
} pc_ai_product_t;

typedef enum {
    PC_AI_SESSION_OFF = 0,
    PC_AI_SESSION_IDLE = 1,
    PC_AI_SESSION_BUSY = 2,
    PC_AI_SESSION_WAIT = 3,
    PC_AI_SESSION_DONE = 4,
    PC_AI_SESSION_FAILED = 5,
    PC_AI_SESSION_STALE = 6
} pc_ai_session_state_t;

typedef enum {
    PC_AI_BLE_LINK_DISABLED = 0,
    PC_AI_BLE_LINK_STARTING,
    PC_AI_BLE_LINK_ADVERTISING,
    PC_AI_BLE_LINK_CONNECTED,
    PC_AI_BLE_LINK_ENCRYPTED,
    PC_AI_BLE_LINK_ERROR
} pc_ai_ble_link_state_t;

typedef struct {
    pc_ai_product_t product;
    pc_ai_session_state_t state;
    uint8_t label_len;
    char label[PC_AI_BLE_SESSION_LABEL_MAX + 1U];
} pc_ai_session_summary_t;

typedef struct {
    pc_ai_ble_link_state_t link_state;
    pc_ai_process_state_t codex_state;
    pc_ai_process_state_t claude_state;
    bool initialized;
    bool host_synced;
    bool heartbeat_valid;
    bool bonded;
    bool pairing_open;
    bool passkey_pending;
    uint8_t sequence;
    uint8_t flags;
    uint32_t pairing_passkey;
    uint32_t pairing_remaining_seconds;
    uint32_t heartbeat_age_ms;
    esp_err_t last_error;
    uint8_t session_count;
    uint8_t total_session_count;
    pc_ai_session_summary_t sessions[PC_AI_BLE_MAX_SESSIONS];
} pc_ai_ble_snapshot_t;

esp_err_t pc_ai_ble_init(void);
esp_err_t pc_ai_ble_get_snapshot(pc_ai_ble_snapshot_t *snapshot);
esp_err_t pc_ai_ble_start_pairing(uint32_t timeout_seconds);
esp_err_t pc_ai_ble_cancel_pairing(void);
esp_err_t pc_ai_ble_forget_bonds(void);

#endif
