#pragma once

#include "u2f.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start BLE transport. Returns opaque pointer or NULL. */
void* u2f_transport_ble_start(U2fData* u2f_data);

/** Stop BLE transport. */
void u2f_transport_ble_stop(void* ble_impl);

#ifdef __cplusplus
}
#endif
