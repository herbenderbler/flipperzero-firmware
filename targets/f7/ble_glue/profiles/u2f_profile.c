#include "u2f_profile.h"
#include <gap.h>
#include <furi_ble/profile_interface.h>
#include <services/dev_info_service.h>
#include <services/battery_service.h>
#include <services/u2f_service.h>
#include <furi.h>
#include <ble/core/ble_defs.h>
#include <string.h>

#define TAG "BtU2fProfile"

typedef struct {
    FuriHalBleProfileBase base;
    BleServiceDevInfo* dev_info_svc;
    BleServiceBattery* battery_svc;
    BleServiceU2f* u2f_svc;
} BleProfileU2f;
_Static_assert(offsetof(BleProfileU2f, base) == 0, "Wrong layout");

static FuriHalBleProfileBase* ble_profile_u2f_start(FuriHalBleProfileParams profile_params) {
    UNUSED(profile_params);

    BleProfileU2f* profile = malloc(sizeof(BleProfileU2f));
    if(!profile) return NULL;

    profile->base.config = ble_profile_u2f;
    profile->dev_info_svc = ble_svc_dev_info_start();
    profile->battery_svc = ble_svc_battery_start(true);
    profile->u2f_svc = ble_svc_u2f_start();
    if(!profile->u2f_svc) {
        ble_svc_battery_stop(profile->battery_svc);
        ble_svc_dev_info_stop(profile->dev_info_svc);
        free(profile);
        return NULL;
    }

    FURI_LOG_I(TAG, "U2F profile started");
    return &profile->base;
}

static void ble_profile_u2f_stop(FuriHalBleProfileBase* profile) {
    furi_check(profile);
    furi_check(profile->config == ble_profile_u2f);

    BleProfileU2f* u2f_profile = (BleProfileU2f*)profile;
    ble_svc_u2f_stop(u2f_profile->u2f_svc);
    ble_svc_battery_stop(u2f_profile->battery_svc);
    ble_svc_dev_info_stop(u2f_profile->dev_info_svc);
    free(u2f_profile);
    FURI_LOG_I(TAG, "U2F profile stopped");
}

#define U2F_CONN_INTERVAL_MIN (0x06)
#define U2F_CONN_INTERVAL_MAX (0x24)

static const GapConfig u2f_template_config = {
    .adv_service =
        {
            .UUID_Type = UUID_TYPE_16,
            .Service_UUID_16 = 0xFFFD,
        },
    .appearance_char = 0x00C2, /* Security Key per BT SIG */
    .bonding_mode = true,
    .pairing_method = GapPairingPinCodeShow,
    .conn_param = {
        .conn_int_min = U2F_CONN_INTERVAL_MIN,
        .conn_int_max = U2F_CONN_INTERVAL_MAX,
        .slave_latency = 0,
        .supervisor_timeout = 0,
    }};

static void ble_profile_u2f_get_config(GapConfig* config, FuriHalBleProfileParams profile_params) {
    UNUSED(profile_params);
    furi_check(config);
    memcpy(config, &u2f_template_config, sizeof(GapConfig));
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));
    strlcpy(config->adv_name, "Flipper U2F", FURI_HAL_VERSION_DEVICE_NAME_LENGTH);
}

static const FuriHalBleProfileTemplate profile_callbacks = {
    .start = ble_profile_u2f_start,
    .stop = ble_profile_u2f_stop,
    .get_gap_config = ble_profile_u2f_get_config,
};

const FuriHalBleProfileTemplate* const ble_profile_u2f = &profile_callbacks;

void ble_profile_u2f_set_request_callback(
    FuriHalBleProfileBase* profile,
    BleSvcU2fRequestCallback callback,
    void* context) {
    furi_check(profile && (profile->config == ble_profile_u2f));
    BleProfileU2f* u2f_profile = (BleProfileU2f*)profile;
    ble_svc_u2f_set_request_callback(u2f_profile->u2f_svc, callback, context);
}

bool ble_profile_u2f_send_response(
    FuriHalBleProfileBase* profile,
    const uint8_t* data,
    uint16_t len) {
    furi_check(profile && (profile->config == ble_profile_u2f));
    BleProfileU2f* u2f_profile = (BleProfileU2f*)profile;
    return ble_svc_u2f_send_response(u2f_profile->u2f_svc, data, len);
}

bool ble_profile_u2f_send_keepalive(FuriHalBleProfileBase* profile, uint8_t status) {
    furi_check(profile && (profile->config == ble_profile_u2f));
    BleProfileU2f* u2f_profile = (BleProfileU2f*)profile;
    return ble_svc_u2f_send_keepalive(u2f_profile->u2f_svc, status);
}
