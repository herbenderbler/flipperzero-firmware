#include "../u2f_app_i.h"

static const char* transport_names[] = {"USB", "BLE"};
#define TRANSPORT_COUNT (sizeof(transport_names) / sizeof(transport_names[0]))

static void u2f_scene_config_transport_change(VariableItem* item) {
    U2fApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, transport_names[index]);
    app->transport_type = (U2fTransportType)index;
    u2f_save_settings(app);
}

void u2f_scene_config_on_enter(void* context) {
    U2fApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(
        list, "Transport", TRANSPORT_COUNT, u2f_scene_config_transport_change, app);
    uint8_t idx = (uint8_t)app->transport_type;
    if(idx >= TRANSPORT_COUNT) idx = 0;
    variable_item_set_current_value_index(item, idx);
    variable_item_set_current_value_text(item, transport_names[idx]);

    view_dispatcher_switch_to_view(app->view_dispatcher, U2fAppViewConfig);
}

bool u2f_scene_config_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void u2f_scene_config_on_exit(void* context) {
    U2fApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
