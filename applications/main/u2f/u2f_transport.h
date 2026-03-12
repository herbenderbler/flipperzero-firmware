#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "u2f.h"

/**
 * U2F transport abstraction.
 * Allows the U2F app to use either USB HID or BLE without duplicating core logic.
 */

typedef struct U2fTransport U2fTransport;

typedef enum {
    U2fTransportTypeUsb,
    U2fTransportTypeBle,
} U2fTransportType;

typedef enum {
    U2fTransportEventConnected,
    U2fTransportEventDisconnected,
} U2fTransportEvent;

typedef void (*U2fTransportEventCallback)(U2fTransportEvent event, void* context);

/** Start the transport. Returns instance or NULL on failure. */
U2fTransport* u2f_transport_start(U2fData* u2f_data, U2fTransportType type);

/** Stop the transport and free the instance. */
void u2f_transport_stop(U2fTransport* transport);

/** Get transport type name for UI (e.g. "USB", "BLE"). */
const char* u2f_transport_get_name(const U2fTransport* transport);

/** Set callback for connect/disconnect. Optional. */
void u2f_transport_set_event_callback(
    U2fTransport* transport,
    U2fTransportEventCallback callback,
    void* context);

#ifdef __cplusplus
}
#endif
