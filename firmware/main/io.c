//Handles the simple IO things, like the button and battery measuring

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
*/


#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali_scheme.h>
#include <driver/gpio.h>
#include <esp_timer.h>


#define PIN_NUM_BTN 10

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t cal_handle;

//For the battery, we record the minimum voltage. Given that WiFi startup loads the battery,
//this gives a better indication of the state of the thing.
int min_bat=9999;

void adc_callback(void *arg) {
	int raw, mv;
	ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &raw));
	adc_cali_raw_to_voltage(cal_handle, raw, &mv);
	if (min_bat>mv) min_bat=mv;
}

void io_init() {
	const gpio_config_t gpio_cfg={
		.pin_bit_mask=(1<<PIN_NUM_BTN),
		.mode=GPIO_MODE_INPUT,
		.pull_up_en=GPIO_PULLUP_ENABLE
	};
	gpio_config(&gpio_cfg);
	esp_timer_create_args_t config={
		.callback=adc_callback,
		.name="adc",
		.skip_unhandled_events=true
	};
	
	adc_oneshot_unit_init_cfg_t adc1_cfg = {
		.unit_id = ADC_UNIT_1,
		.ulp_mode = false,
	};
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc1_cfg, &adc1_handle));
	adc_oneshot_chan_cfg_t chan_cfg = {
		.bitwidth = ADC_BITWIDTH_DEFAULT,
		.atten = ADC_ATTEN_DB_11,
	};
	ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &chan_cfg));
	adc_cali_curve_fitting_config_t cali_config = {
		.unit_id = ADC_UNIT_1,
		.atten = ADC_ATTEN_DB_11,
		.bitwidth = ADC_BITWIDTH_DEFAULT,
	};
	ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cal_handle));

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