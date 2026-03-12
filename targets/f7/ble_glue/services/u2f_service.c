/**
 * FIDO U2F GATT service - FIDO Alliance BLE spec.
 * Service 0xFFFD, characteristics: Control Point (write), Status (notify), etc.
 */
#include "u2f_service.h"
#include "app_common.h"
#include <ble/ble.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>
#include <furi.h>
#include <string.h>

#include "u2f_service_uuid.inc"

#define TAG "BtU2fSvc"

/* FIDO BLE framing */
#define U2F_BLE_CMD_PING      0x81
#define U2F_BLE_CMD_KEEPALIVE 0x82
#define U2F_BLE_CMD_MSG       0x83
#define U2F_BLE_CMD_ERROR     0xbf
#define U2F_BLE_INIT_BIT      0x80

typedef enum {
    U2fSvcCharControlPoint = 0,
    U2fSvcCharStatus,
    U2fSvcCharControlPointLength,
    U2fSvcCharServiceRevision,
    U2fSvcCharServiceRevisionBitfield,
    U2fSvcCharCount,
} U2fSvcCharId;

static const uint8_t u2f_control_point_length_val[2] = {
    (BLE_SVC_U2F_CONTROL_POINT_LEN_MAX >> 8) & 0xff,
    BLE_SVC_U2F_CONTROL_POINT_LEN_MAX & 0xff,
};

static const uint8_t u2f_service_revision_str[] = U2F_SERVICE_REVISION_1_0_STR;
static const uint8_t u2f_service_revision_bitfield_val = U2F_SERVICE_REVISION_BITFIELD_1_1;

static const BleGattCharacteristicParams ble_svc_u2f_chars[U2fSvcCharCount] = {
    [U2fSvcCharControlPoint] =
        {.name = "U2F Control Point",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = BLE_SVC_U2F_CONTROL_POINT_LEN_MAX,
         .data.fixed.ptr = NULL,
         .uuid.Char_UUID_128 = BLE_U2F_CONTROL_POINT_UUID,
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_WRITE_WITHOUT_RESP | CHAR_PROP_WRITE,
         .security_permissions = ATTR_PERMISSION_AUTHEN_WRITE | ATTR_PERMISSION_ENCRY_WRITE,
         .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
         .is_variable = CHAR_VALUE_LEN_VARIABLE},
    [U2fSvcCharStatus] =
        {.name = "U2F Status",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = BLE_SVC_U2F_CONTROL_POINT_LEN_MAX,
         .data.fixed.ptr = NULL,
         .uuid.Char_UUID_128 = BLE_U2F_STATUS_UUID,
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_READ | CHAR_PROP_NOTIFY,
         .security_permissions = ATTR_PERMISSION_AUTHEN_READ | ATTR_PERMISSION_ENCRY_READ,
         .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
         .is_variable = CHAR_VALUE_LEN_VARIABLE},
    [U2fSvcCharControlPointLength] =
        {.name = "U2F Control Point Length",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = 2,
         .data.fixed.ptr = u2f_control_point_length_val,
         .uuid.Char_UUID_128 = BLE_U2F_CONTROL_POINT_LENGTH_UUID,
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_READ,
         .security_permissions = ATTR_PERMISSION_AUTHEN_READ | ATTR_PERMISSION_ENCRY_READ,
         .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
         .is_variable = CHAR_VALUE_LEN_CONSTANT},
    [U2fSvcCharServiceRevision] =
        {.name = "U2F Service Revision",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = sizeof(u2f_service_revision_str) - 1,
         .data.fixed.ptr = u2f_service_revision_str,
         .uuid.Char_UUID_16 = 0x2A28,
         .uuid_type = UUID_TYPE_16,
         .char_properties = CHAR_PROP_READ,
         .security_permissions = ATTR_PERMISSION_AUTHEN_READ | ATTR_PERMISSION_ENCRY_READ,
         .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
         .is_variable = CHAR_VALUE_LEN_CONSTANT},
    [U2fSvcCharServiceRevisionBitfield] =
        {.name = "U2F Service Revision Bitfield",
         .data_prop_type = FlipperGattCharacteristicDataFixed,
         .data.fixed.length = 1,
         .data.fixed.ptr = &u2f_service_revision_bitfield_val,
         .uuid.Char_UUID_128 = BLE_U2F_SERVICE_REVISION_BITFIELD_UUID,
         .uuid_type = UUID_TYPE_128,
         .char_properties = CHAR_PROP_READ | CHAR_PROP_WRITE,
         .security_permissions = ATTR_PERMISSION_AUTHEN_READ | ATTR_PERMISSION_AUTHEN_WRITE |
                                 ATTR_PERMISSION_ENCRY_READ | ATTR_PERMISSION_ENCRY_WRITE,
         .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
         .is_variable = CHAR_VALUE_LEN_CONSTANT},
};

struct BleServiceU2f {
    uint16_t svc_handle;
    BleGattCharacteristicInstance chars[U2fSvcCharCount];
    GapSvcEventHandler* event_handler;

    uint8_t req_buf[BLE_SVC_U2F_REQUEST_MAX_LEN];
    uint16_t req_len;
    uint16_t req_total;
    uint8_t req_seq;

    BleSvcU2fRequestCallback request_callback;
    void* request_context;
};

static void u2f_svc_assemble_request(BleServiceU2f* svc, const uint8_t* data, uint16_t len) {
    if(len < 1) return;
    uint8_t b0 = data[0];
    if(b0 & U2F_BLE_INIT_BIT) {
        if(len < 3) return;
        svc->req_total = ((uint16_t)data[1] << 8) | data[2];
        if(svc->req_total > BLE_SVC_U2F_REQUEST_MAX_LEN) {
            svc->req_total = 0;
            return;
        }
        svc->req_len = MIN((uint16_t)(len - 3), svc->req_total);
        memcpy(svc->req_buf, data + 3, svc->req_len);
        svc->req_seq = 0;
    } else {
        if(svc->req_total == 0) return;
        uint8_t seq = b0 & 0x7f;
        if(seq != svc->req_seq) return;
        uint16_t copy = MIN((uint16_t)(len - 1), svc->req_total - svc->req_len);
        memcpy(svc->req_buf + svc->req_len, data + 1, copy);
        svc->req_len += copy;
        svc->req_seq++;
    }

    if(svc->req_len >= svc->req_total && svc->req_total > 0) {
        if(svc->request_callback) {
            svc->request_callback(svc->req_buf, svc->req_total, svc->request_context);
        }
        svc->req_total = 0;
        svc->req_len = 0;
    }
}

static BleEventAckStatus ble_svc_u2f_event_handler(void* event, void* context) {
    BleServiceU2f* svc = (BleServiceU2f*)context;
    BleEventAckStatus ret = BleEventNotAck;
    hci_event_pckt* event_pckt = (hci_event_pckt*)(((hci_uart_pckt*)event)->data);
    evt_blecore_aci* blecore_evt = (evt_blecore_aci*)event_pckt->data;

    if(event_pckt->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return ret;
    if(blecore_evt->ecode != ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) return ret;

    aci_gatt_attribute_modified_event_rp0* evt =
        (aci_gatt_attribute_modified_event_rp0*)blecore_evt->data;
    if(evt->Attr_Handle != svc->chars[U2fSvcCharControlPoint].handle + 1) return ret;

    u2f_svc_assemble_request(svc, evt->Attr_Data, evt->Attr_Data_Length);
    ret = BleEventAckFlowEnable;
    return ret;
}

static bool u2f_svc_send_fragmented(
    BleServiceU2f* svc,
    const uint8_t* frame,
    uint16_t frame_len,
    uint8_t max_chunk) {
    for(uint16_t offset = 0; offset < frame_len;) {
        uint8_t chunk = (uint8_t)MIN((uint16_t)max_chunk, frame_len - offset);
        uint8_t update_type = (offset + chunk >= frame_len) ? 0x02u : 0x00u;
        tBleStatus result = aci_gatt_update_char_value_ext(
            0,
            svc->svc_handle,
            svc->chars[U2fSvcCharStatus].handle,
            update_type,
            frame_len,
            offset,
            chunk,
            frame + offset);
        if(result != BLE_STATUS_SUCCESS) {
            FURI_LOG_E(TAG, "Status notify failed: %d", result);
            return false;
        }
        offset += chunk;
    }
    return true;
}

BleServiceU2f* ble_svc_u2f_start(void) {
    BleServiceU2f* svc = malloc(sizeof(BleServiceU2f));
    if(!svc) return NULL;
    memset(svc, 0, sizeof(BleServiceU2f));

    svc->event_handler = ble_event_dispatcher_register_svc_handler(ble_svc_u2f_event_handler, svc);

    static const uint16_t service_uuid = 0xFFFD;
    if(!ble_gatt_service_add(
           UUID_TYPE_16,
           (const Service_UUID_t*)&service_uuid,
           PRIMARY_SERVICE,
           12,
           &svc->svc_handle)) {
        ble_event_dispatcher_unregister_svc_handler(svc->event_handler);
        free(svc);
        return NULL;
    }

    for(uint8_t i = 0; i < U2fSvcCharCount; i++) {
        ble_gatt_characteristic_init(svc->svc_handle, &ble_svc_u2f_chars[i], &svc->chars[i]);
    }

    FURI_LOG_I(TAG, "U2F service started");
    return svc;
}

void ble_svc_u2f_stop(BleServiceU2f* svc) {
    furi_check(svc);
    ble_event_dispatcher_unregister_svc_handler(svc->event_handler);
    for(uint8_t i = 0; i < U2fSvcCharCount; i++) {
        ble_gatt_characteristic_delete(svc->svc_handle, &svc->chars[i]);
    }
    ble_gatt_service_delete(svc->svc_handle);
    free(svc);
    FURI_LOG_I(TAG, "U2F service stopped");
}

void ble_svc_u2f_set_request_callback(
    BleServiceU2f* svc,
    BleSvcU2fRequestCallback callback,
    void* context) {
    furi_check(svc);
    svc->request_callback = callback;
    svc->request_context = context;
}

bool ble_svc_u2f_send_response(BleServiceU2f* svc, const uint8_t* data, uint16_t len) {
    furi_check(svc && data);
    if(len > BLE_SVC_U2F_REQUEST_MAX_LEN) return false;
    uint8_t max_chunk = BLE_SVC_U2F_CONTROL_POINT_LEN_MAX;
    return u2f_svc_send_fragmented(svc, data, len, max_chunk);
}

bool ble_svc_u2f_send_keepalive(BleServiceU2f* svc, uint8_t status) {
    furi_check(svc);
    uint8_t frame[2] = {U2F_BLE_CMD_KEEPALIVE, status};
    return u2f_svc_send_fragmented(svc, frame, 2, BLE_SVC_U2F_CONTROL_POINT_LEN_MAX);
}
