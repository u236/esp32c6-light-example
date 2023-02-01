
#include "esp_log.h"
#include "led_strip.h"
#include "config.h"
#include "light.h"

static const char *tag = "light";
static led_strip_handle_t handle;

void light_init(uint8_t status)
{
    led_strip_config_t led_strip;
    led_strip_rmt_config_t rmt;

    led_strip.max_leds = 1;
    led_strip.strip_gpio_num = LED_PIN;
    rmt.resolution_hz = 10000000;

    led_strip_new_rmt_device(&led_strip, &rmt, &handle);
    light_set(status);
}

void light_set(uint8_t status)
{
    led_strip_set_pixel(handle, 0, 0, 0, status ? 32 : 0);
    led_strip_refresh(handle);
    ESP_LOGI(tag, "%s", status ? "enabled" : "disabled");
}
