#pragma once

#include <furi_ble/profile_interface.h>
#include <services/u2f_service.h>

#ifdef __cplusplus
extern "C" {
#endif

/** U2F BLE profile descriptor */
extern const FuriHalBleProfileTemplate* const ble_profile_u2f;

/** Set U2F request callback (invoked when client sends a complete request frame) */
void ble_profile_u2f_set_request_callback(
    FuriHalBleProfileBase* profile,
    BleSvcU2fRequestCallback callback,
    void* context);

/** Send U2F response frame (STAT, HLEN, LLEN, DATA) */
bool ble_profile_u2f_send_response(
    FuriHalBleProfileBase* profile,
    const uint8_t* data,
    uint16_t len);

/** Send KEEPALIVE (e.g. PROCESSING or TUP_NEEDED) */
bool ble_profile_u2f_send_keepalive(FuriHalBleProfileBase* profile, uint8_t status);

#ifdef __cplusplus
}
#endif
