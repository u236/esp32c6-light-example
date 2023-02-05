#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nvs_flash.h"
#include "config.h"
#include "light.h"

static const char *tag = "zigbee";

static gptimer_handle_t timer;
static TaskHandle_t button_handle, timer_handle;
static light_data_t light_data;
static char manufacturer[32], model[32];
static uint8_t zigbee_channel = 11, zcl_version = ZCL_VERSION, app_version = APP_VERSION, power_source = POWER_SOURCE;
static uint16_t color_capabilities = 0x0009;

static void IRAM_ATTR button_handler(void *arg)
{
    (void) arg;
    xTaskResumeFromISR(button_handle);
}

static bool IRAM_ATTR timer_handler(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *arg)
{
    (void) event;
    (void) arg;

    gptimer_stop(timer);
    xTaskResumeFromISR(timer_handle);

    return true;
}

static void set_zcl_string(char *buffer, char *value)
{
    buffer[0] = (char) strlen(value);
    memcpy(buffer + 1, value, buffer[0]);
}

static void set_attr_value_cb(uint8_t status, uint8_t endpoint, uint16_t cluster_id, uint16_t attr_id, void *data)
{
    (void) status;
    (void) endpoint;

    switch (cluster_id)
    {
        case ESP_ZB_ZCL_CLUSTER_ID_ON_OFF:

            switch (attr_id)
            {
                case ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID:
                    light_data.status = *(uint8_t*) data;
                    light_update();
                    return;
            }

            break;

        case ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL:

            switch (attr_id)
            {
                case ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID:
                    light_data.level = *(uint8_t*) data;
                    light_update();
                    return;
            }

            break;

        case ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL:

            switch (attr_id)
            {
                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID:
                    light_data.color_h = *(uint8_t*) data;
                    light_update();
                    return;

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID:
                    light_data.color_s = *(uint8_t*) data;
                    light_update();
                    return;
          
                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID:
                    light_data.color_x = *(uint16_t*) data;
                    light_update();
                    return;

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID:
                    light_data.color_y = *(uint16_t*) data;
                    light_update();
                    return;

                case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_MODE_ID:
                    light_data.color_mode = *(uint8_t*) data;
                    light_update();
                    return;
            }

            break;
    }

    ESP_LOGI(tag, "cluster 0x%04X attribute 0x%04X value updated", cluster_id, attr_id);
}

static void zigbee_task(void *arg)
{
    (void) arg;

    esp_zb_cfg_t zigbee_config;
    esp_zb_attribute_list_t *attr_list_basic = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC), *attr_list_on_off = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF), *attr_list_level = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL), *attr_list_color = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL);
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

    memset(&zigbee_config, 0, sizeof(zigbee_config));
    zigbee_config.esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER;
    zigbee_config.install_code_policy = false;
    zigbee_config.nwk_cfg.zczr_cfg.max_children = 16;

    set_zcl_string(manufacturer, MANUFACTURER_NAME);
    set_zcl_string(model, MODEL_NAME);

    esp_zb_basic_cluster_add_attr(attr_list_basic, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &zcl_version);
    esp_zb_basic_cluster_add_attr(attr_list_basic, ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, &app_version);
    esp_zb_basic_cluster_add_attr(attr_list_basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer);
    esp_zb_basic_cluster_add_attr(attr_list_basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model);
    esp_zb_basic_cluster_add_attr(attr_list_basic, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &power_source);

    esp_zb_on_off_cluster_add_attr(attr_list_on_off, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &light_data.status);
    esp_zb_level_cluster_add_attr(attr_list_level, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, &light_data.level);

    esp_zb_color_control_cluster_add_attr(attr_list_color, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID, &light_data.color_h);
    esp_zb_color_control_cluster_add_attr(attr_list_color, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID, &light_data.color_s);
    esp_zb_color_control_cluster_add_attr(attr_list_color, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID, &light_data.color_x);
    esp_zb_color_control_cluster_add_attr(attr_list_color, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID, &light_data.color_y);
    esp_zb_color_control_cluster_add_attr(attr_list_color, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_MODE_ID, &light_data.color_mode);
    esp_zb_color_control_cluster_add_attr(attr_list_color, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_ENHANCED_COLOR_MODE_ID, &light_data.color_mode);
    esp_zb_color_control_cluster_add_attr(attr_list_color, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_CAPABILITIES_ID, &color_capabilities);

    esp_zb_cluster_list_add_basic_cluster(cluster_list, attr_list_basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_on_off_cluster(cluster_list, attr_list_on_off, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_level_cluster(cluster_list, attr_list_level, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_color_control_cluster(cluster_list, attr_list_color, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_ep_list_add_ep(ep_list, cluster_list, LIGHT_ENDPOINT, LIGHT_PROFILE_ID, LIGHT_DEVICE_ID);

    esp_zb_init(&zigbee_config);
    esp_zb_set_network_channel(zigbee_channel);
    esp_zb_device_register(ep_list);
    esp_zb_device_add_set_attr_value_cb(set_attr_value_cb);
    esp_zb_start(false);

    esp_zb_main_loop_iteration();
}

static void button_task(void *arg)
{
    (void) arg;

    while (1)
    {
        vTaskSuspend(NULL);

        if (gpio_get_level(BUTTON_PIN))
        {
            gptimer_stop(timer);
            continue;
        }

        gptimer_set_raw_count(timer, 0);
        gptimer_start(timer);

        light_data.status ^= 1;
        light_update();

        // TODO: send report here
    }
}

static void timer_task(void *arg)
{
    (void) arg;
    vTaskSuspend(NULL);
    esp_zb_factory_reset();
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *data)
{
    esp_zb_app_signal_type_t signal = *data->p_app_signal;
    esp_err_t error = data->esp_err_status;

    switch (signal)
    {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(tag, "ZigBee stack initialized");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;

        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:

            if (error == ESP_OK)
            {
                ESP_LOGI(tag, "Network steering started");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
                break;
            }

            ESP_LOGE(tag, "Failed to initialize Zigbee stack (status: %d)", error);
            break;

        case ESP_ZB_BDB_SIGNAL_STEERING:

            if (error == ESP_OK)
            {
                esp_zb_ieee_addr_t pan_id;
                esp_zb_get_extended_pan_id(pan_id);
                ESP_LOGI(tag, "Successfully joined network on channel %d (PAN ID: 0x%04x, Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x)", zigbee_channel, esp_zb_get_pan_id(), pan_id[7], pan_id[6], pan_id[5], pan_id[4], pan_id[3], pan_id[2], pan_id[1], pan_id[0]);
                break;
            }

            ESP_LOGW(tag, "Network steering failed on channel %d (status: %d)", zigbee_channel, error);
            zigbee_channel = zigbee_channel < 26 ? zigbee_channel + 1 : 11;

            esp_zb_set_network_channel(zigbee_channel);
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            break;

        default:
            ESP_LOGW(tag, "ZDO signal %d received with error code %d", signal, error);
            break;
    }
}

void app_main(void)
{
    esp_zb_platform_config_t platform_config;
    gpio_config_t button_config;
    gptimer_config_t timer_config;
    gptimer_event_callbacks_t timer_callbacks;
    gptimer_alarm_config_t alarm_config;

    memset(&platform_config, 0, sizeof(platform_config));
    platform_config.radio_config.radio_mode = RADIO_MODE_NATIVE;
    platform_config.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;

    memset(&button_config, 0, sizeof(button_config));
    button_config.pin_bit_mask = 1ULL << BUTTON_PIN;
    button_config.mode = GPIO_MODE_INPUT;
    button_config.intr_type = GPIO_INTR_ANYEDGE;

    memset(&timer_config, 0, sizeof(timer_config));
    timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_config.direction = GPTIMER_COUNT_UP;
    timer_config.resolution_hz = 1000000;

    memset(&timer_callbacks, 0, sizeof(timer_callbacks));
    timer_callbacks.on_alarm = timer_handler;

    memset(&alarm_config, 0, sizeof(alarm_config));
    alarm_config.alarm_count = timer_config.resolution_hz * 10;

    nvs_flash_init();
    light_init(&light_data);
    esp_zb_platform_config(&platform_config);

    gpio_config(&button_config);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_handler, NULL);

    gptimer_new_timer(&timer_config, &timer);
    gptimer_register_event_callbacks(timer, &timer_callbacks, NULL);
    gptimer_set_alarm_action(timer, &alarm_config);
    gptimer_enable(timer);

    xTaskCreate(zigbee_task, "zigbee", 4096, NULL, 5, NULL);
    xTaskCreate(button_task, "button", 4096, NULL, 0, &button_handle);
    xTaskCreate(timer_task, "timer", 4096, NULL, 0, &timer_handle);
}
