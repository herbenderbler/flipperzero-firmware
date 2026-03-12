/**
 * U2F transport over BLE (FIDO Alliance BLE spec).
 * Full implementation only when built with firmware (bt_service available);
 * stubs when built as FAP so the app still compiles.
 */
#include "u2f_transport.h"
#include "u2f.h"
#include <furi.h>
#include <stdlib.h>
#include <string.h>

#if __has_include(<bt/bt_service/bt.h>) && __has_include(<profiles/u2f_profile.h>)
#include <bt/bt_service/bt.h>
#include <profiles/u2f_profile.h>

#define TAG "U2fTransportBle"

#define U2F_BLE_CMD_PING  0x81
#define U2F_BLE_CMD_MSG   0x83
#define U2F_BLE_CMD_ERROR 0xbf

#define U2F_BLE_ERR_INVALID_CMD 0x01

#define REQUEST_FLAG (1u << 0)
#define STOP_FLAG    (1u << 1)

typedef struct {
    Bt* bt;
    FuriHalBleProfileBase* profile;
    U2fData* u2f_data;
    FuriThread* thread;
    FuriMutex* mutex;
    uint8_t req_buf[BLE_SVC_U2F_REQUEST_MAX_LEN];
    uint16_t req_len;
    bool stop_requested;
} U2fTransportBle;

static void u2f_ble_request_callback(const uint8_t* data, uint16_t len, void* context) {
    U2fTransportBle* ble = context;
    if(len > BLE_SVC_U2F_REQUEST_MAX_LEN) return;
    if(furi_mutex_acquire(ble->mutex, 100) != FuriStatusOk) return;
    memcpy(ble->req_buf, data, len);
    ble->req_len = len;
    furi_mutex_release(ble->mutex);
    furi_thread_flags_set(furi_thread_get_id(ble->thread), REQUEST_FLAG);
}

static int32_t u2f_ble_worker(void* context) {
    U2fTransportBle* ble = context;
    uint8_t resp_buf[3 + BLE_SVC_U2F_REQUEST_MAX_LEN];
    uint16_t req_len;
    uint8_t req_buf[BLE_SVC_U2F_REQUEST_MAX_LEN];

    while(1) {
        uint32_t flags = furi_thread_flags_wait(STOP_FLAG | REQUEST_FLAG, FuriFlagWaitAny, FuriWaitForever);
        if(flags & FuriFlagError) break;
        if(flags & STOP_FLAG) break;

        if(!(flags & REQUEST_FLAG)) continue;

        if(furi_mutex_acquire(ble->mutex, FuriWaitForever) != FuriStatusOk) continue;
        req_len = ble->req_len;
        if(req_len > 0) memcpy(req_buf, ble->req_buf, req_len);
        furi_mutex_release(ble->mutex);

        if(req_len < 3) continue;

        uint8_t cmd = req_buf[0];
        uint16_t data_len = ((uint16_t)req_buf[1] << 8) | req_buf[2];

        if(cmd == U2F_BLE_CMD_PING) {
            resp_buf[0] = U2F_BLE_CMD_PING;
            resp_buf[1] = (req_len - 3) >> 8;
            resp_buf[2] = (req_len - 3) & 0xff;
            if(req_len > 3) memcpy(resp_buf + 3, req_buf + 3, req_len - 3);
            ble_profile_u2f_send_response(ble->profile, resp_buf, req_len);
            continue;
        }

        if(cmd != U2F_BLE_CMD_MSG) {
            resp_buf[0] = U2F_BLE_CMD_ERROR;
            resp_buf[1] = 0;
            resp_buf[2] = 1;
            resp_buf[3] = U2F_BLE_ERR_INVALID_CMD;
            ble_profile_u2f_send_response(ble->profile, resp_buf, 4);
            continue;
        }

        if(data_len > req_len - 3) data_len = req_len - 3;
        uint16_t resp_len = u2f_msg_parse(ble->u2f_data, req_buf + 3, data_len);

        if(resp_len == 0) {
            resp_buf[0] = U2F_BLE_CMD_ERROR;
            resp_buf[1] = 0;
            resp_buf[2] = 1;
            resp_buf[3] = U2F_BLE_ERR_INVALID_CMD;
            ble_profile_u2f_send_response(ble->profile, resp_buf, 4);
            continue;
        }

        /* u2f_msg_parse wrote the response in-place at req_buf+3 */
        resp_buf[0] = U2F_BLE_CMD_MSG;
        resp_buf[1] = (resp_len >> 8) & 0xff;
        resp_buf[2] = resp_len & 0xff;
        memcpy(resp_buf + 3, req_buf + 3, resp_len);
        ble_profile_u2f_send_response(ble->profile, resp_buf, 3 + resp_len);
    }
    return 0;
}

static U2fTransportBle* u2f_ble_start_impl(U2fData* u2f_data) {
    Bt* bt = furi_record_open(RECORD_BT);
    if(!bt) return NULL;

    FuriHalBleProfileBase* profile = bt_profile_start(bt, ble_profile_u2f, NULL);
    if(!profile) {
        furi_record_close(RECORD_BT);
        return NULL;
    }

    U2fTransportBle* ble = malloc(sizeof(U2fTransportBle));
    if(!ble) {
        bt_profile_restore_default(bt);
        furi_record_close(RECORD_BT);
        return NULL;
    }
    ble->bt = bt;
    ble->profile = profile;
    ble->u2f_data = u2f_data;
    ble->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    ble->req_len = 0;
    ble->stop_requested = false;

    ble->thread = furi_thread_alloc_ex("U2fBleWorker", 1024, u2f_ble_worker, ble);
    furi_thread_start(ble->thread);

    ble_profile_u2f_set_request_callback(profile, u2f_ble_request_callback, ble);

    u2f_set_state(u2f_data, 1);
    FURI_LOG_I(TAG, "BLE transport started");
    return ble;
}

static void u2f_ble_stop_impl(U2fTransportBle* ble) {
    if(!ble) return;
    ble->stop_requested = true;
    furi_thread_flags_set(furi_thread_get_id(ble->thread), STOP_FLAG);
    furi_thread_join(ble->thread);
    furi_thread_free(ble->thread);
    bt_profile_restore_default(ble->bt);
    u2f_set_state(ble->u2f_data, 0);
    furi_record_close(RECORD_BT);
    furi_mutex_free(ble->mutex);
    free(ble);
    FURI_LOG_I(TAG, "BLE transport stopped");
}

/* Exported for u2f_transport.c */
void* u2f_transport_ble_start(U2fData* u2f_data) {
    return u2f_ble_start_impl(u2f_data);
}
void u2f_transport_ble_stop(void* ble_impl) {
    u2f_ble_stop_impl((U2fTransportBle*)ble_impl);
}

#else
/* FAP build: no bt_service or u2f_profile, provide stubs */
void* u2f_transport_ble_start(U2fData* u2f_data) {
    (void)u2f_data;
    return NULL;
}
void u2f_transport_ble_stop(void* ble_impl) {
    (void)ble_impl;
}
#endif
