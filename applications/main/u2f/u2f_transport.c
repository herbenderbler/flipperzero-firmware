/**
 * U2F transport layer: dispatches to USB HID or BLE.
 */
#include "u2f_transport.h"
#include "u2f_transport_ble.h"
#include "u2f_hid.h"
#include <furi.h>
#include <stdlib.h>

#define TAG "U2fTransport"

struct U2fTransport {
    U2fTransportType type;
    union {
        U2fHid* hid;
        void* ble; /* U2fBle* when BLE is implemented */
    } impl;
    U2fTransportEventCallback event_callback;
    void* event_context;
};

static const char* transport_name_usb(void) {
    return "USB";
}

static const char* transport_name_ble(void) {
    return "BLE";
}

U2fTransport* u2f_transport_start(U2fData* u2f_data, U2fTransportType type) {
    U2fTransport* transport = malloc(sizeof(U2fTransport));
    if(!transport) return NULL;

    transport->type = type;
    transport->event_callback = NULL;
    transport->event_context = NULL;

    switch(type) {
    case U2fTransportTypeUsb: {
        transport->impl.hid = u2f_hid_start(u2f_data);
        if(!transport->impl.hid) {
            free(transport);
            return NULL;
        }
        FURI_LOG_I(TAG, "Started USB HID transport");
        break;
    }
    case U2fTransportTypeBle: {
        void* ble = u2f_transport_ble_start(u2f_data);
        if(!ble) {
            free(transport);
            return NULL;
        }
        transport->impl.ble = ble;
        FURI_LOG_I(TAG, "Started BLE transport");
        break;
    }
    default:
        free(transport);
        return NULL;
    }

    return transport;
}

void u2f_transport_stop(U2fTransport* transport) {
    furi_assert(transport);

    switch(transport->type) {
    case U2fTransportTypeUsb:
        u2f_hid_stop(transport->impl.hid);
        FURI_LOG_I(TAG, "Stopped USB HID transport");
        break;
    case U2fTransportTypeBle:
        u2f_transport_ble_stop(transport->impl.ble);
        FURI_LOG_I(TAG, "Stopped BLE transport");
        break;
    default:
        break;
    }
    free(transport);
}

const char* u2f_transport_get_name(const U2fTransport* transport) {
    furi_assert(transport);
    switch(transport->type) {
    case U2fTransportTypeUsb:
        return transport_name_usb();
    case U2fTransportTypeBle:
        return transport_name_ble();
    default:
        return "?";
    }
}

void u2f_transport_set_event_callback(
    U2fTransport* transport,
    U2fTransportEventCallback callback,
    void* context) {
    furi_assert(transport);
    transport->event_callback = callback;
    transport->event_context = context;
}
