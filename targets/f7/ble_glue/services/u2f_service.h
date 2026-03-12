#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FIDO U2F GATT service (FIDO Alliance BLE spec).
 * Request callback is invoked when a complete request frame is received.
 * Response is sent via ble_svc_u2f_send_response(). */

#define BLE_SVC_U2F_CONTROL_POINT_LEN_MAX (64)
#define BLE_SVC_U2F_REQUEST_MAX_LEN       (512)

typedef struct BleServiceU2f BleServiceU2f;

typedef void (*BleSvcU2fRequestCallback)(const uint8_t* data, uint16_t len, void* context);

BleServiceU2f* ble_svc_u2f_start(void);

void ble_svc_u2f_stop(BleServiceU2f* service);

void ble_svc_u2f_set_request_callback(
    BleServiceU2f* service,
    BleSvcU2fRequestCallback callback,
    void* context);

/** Send response frame (STAT, HLEN, LLEN, DATA). Fragments and notifies on u2fStatus. */
bool ble_svc_u2f_send_response(BleServiceU2f* service, const uint8_t* data, uint16_t len);

/** Send KEEPALIVE (e.g. PROCESSING or TUP_NEEDED). */
bool ble_svc_u2f_send_keepalive(BleServiceU2f* service, uint8_t status);

#ifdef __cplusplus
}
#endif
