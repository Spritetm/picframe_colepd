//Handles the simple IO things, like the button and battery measuring
#include <driver/adc.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#define PIN_NUM_BTN 10

int min_bat=9999;

void adc_callback(void *arg) {
	int i=(adc1_get_raw(0)*2360)/3276; //battery is IO0/ADC1CH0
	if (min_bat>i) min_bat=i;
}


void io_init() {
	const gpio_config_t cfg={
		.pin_bit_mask=(1<<PIN_NUM_BTN),
		.mode=GPIO_MODE_INPUT,
		.pull_up_en=GPIO_PULLUP_ENABLE
	};
	gpio_config(&cfg);
	esp_timer_create_args_t config={
		.callback=adc_callback,
		.name="adc",
		.skip_unhandled_events=true
	};
	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten(0, ADC_ATTEN_DB_11);
	adc_callback(NULL); //first callback is manual
	esp_timer_handle_t handle;
	esp_timer_create(&config, &handle);
	esp_timer_start_periodic(handle, 50*1000);
}

int io_get_btn() {
	return !gpio_get_level(PIN_NUM_BTN);
}

int io_get_battery_mv() {
	return min_bat;
}