/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "lwip/ip_addr.h"
#include "wifi_manager.h"
#include <assert.h>
#include "esp_partition.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "epd.h"
#include "epd_flash_image.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "sync.h"
#include "io.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"

static const char *TAG="main";

SemaphoreHandle_t connect_sema;

void cb_connection_ok(void *pvParameter){
	ip_event_got_ip_t* param = (ip_event_got_ip_t*)pvParameter;

	/* transform IP to human readable string */
	char str_ip[16];
	esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

	ESP_LOGI(TAG, "I have a connection and my IP is %s!", str_ip);
	xSemaphoreGive(connect_sema);
}

void shutdown_callback(void *arg) {
	ESP_LOGW(TAG, "Alive for too long! Sleeping.");
	esp_sleep_enable_timer_wakeup(60*60*1000ULL*1000ULL);
	esp_deep_sleep_start();
}


void app_main(void) {
	_Static_assert((sizeof(flash_image_t) == IMG_SIZE_BYTES), "flash_image_t not right size");
	connect_sema=xSemaphoreCreateBinary();
	io_init();
	int icon=ICON_NONE;

	//make sure we shut down after 10 mins guaranteed
	esp_timer_create_args_t config={
		.callback=shutdown_callback,
		.name="shutdown",
		.skip_unhandled_events=true
	};
	esp_timer_handle_t handle;
	esp_timer_create(&config, &handle);
	esp_timer_start_periodic(handle, 10*60*1000ULL*1000ULL);

	//Initialize NVS
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		//NVS partition was truncated and needs to be erased. 
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init(); //Retry nvs_flash_init
	}
	ESP_ERROR_CHECK(err);

	//get nvs handle
	nvs_handle_t nvs;
	nvs_open("epd", NVS_READWRITE, &nvs);

	//map image partition, we'll need it later
	const flash_image_t *images=NULL;
	spi_flash_mmap_handle_t mmap_handle;
	const esp_partition_t *part=esp_partition_find_first(123, 0, NULL);
	esp_partition_mmap(part, 0, IMG_SIZE_BYTES*IMG_SLOT_COUNT, SPI_FLASH_MMAP_DATA, (const void**)&images, &mmap_handle);

	//see if we still have enough juice to go online
	int bat=io_get_battery_mv();
	int32_t bat_empty_thr=2250;
	ESP_LOGI(TAG, "Battery at %d mV before going online...", bat);

	if (bat<bat_empty_thr) {
		icon=ICON_BAT_EMPTY;
	} else {
		//start the wifi manager
		wifi_manager_start();
		wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);

		if (io_get_btn()) {
			vTaskDelay(pdMS_TO_TICKS(200));
			wifi_manager_send_message(WM_ORDER_DISCONNECT_STA, NULL);
			vTaskDelay(pdMS_TO_TICKS(200));
			wifi_manager_send_message(WM_ORDER_START_AP, NULL);
			vTaskDelay(pdMS_TO_TICKS(200));
			wifi_manager_send_message(WM_ORDER_START_HTTP_SERVER, NULL);
			vTaskDelay(pdMS_TO_TICKS(200));
			wifi_manager_send_message(WM_ORDER_START_DNS_SERVICE, NULL);
		}
		//wait for connection
		ESP_LOGI(TAG, "Waiting for connection...x");
		int can_connect=xSemaphoreTake(connect_sema, pdMS_TO_TICKS(30*1000));
		int bat=io_get_battery_mv();
		ESP_LOGI(TAG, "Battery at %d mV after WiFi init", bat);
		if (bat<bat_empty_thr) {
			icon=ICON_BAT_EMPTY;
		} else {
			if (can_connect) {
				//We have a WiFi connection.
				err=picframe_sync(images, part);
				if (err==ESP_OK) esp_ota_mark_app_valid_cancel_rollback();
				if (err!=ESP_OK) icon=ICON_SERVER;
			} else {
				icon=ICON_WIFI;
				//No connection, mayhaps we're in config mode?
				//If so, wait for a few minutes for the user to do its thing in the
				//wifi manager UI. We break out either when we have a sta connection or
				//if the mode is not apsta/ap anymore.
				int wifi_timeout_sec=3*60;
				do {
					wifi_mode_t mode;
					esp_wifi_get_mode(&mode);
					if (mode==WIFI_MODE_STA) break;

					can_connect=xSemaphoreTake(connect_sema, pdMS_TO_TICKS(1*1000));
					if (can_connect) break;

					wifi_timeout_sec--;
					if (wifi_timeout_sec==0) break;
					ESP_LOGI(TAG, "Delaying shutdown as we're in AP/APSTA mode...");
				} while(1);

				if (can_connect) {
					err=picframe_sync(images, part);
					if (err==ESP_OK) esp_ota_mark_app_valid_cancel_rollback();
					if (err!=ESP_OK) icon=ICON_SERVER; else icon=ICON_NONE;
				}
			}
		}

		//Assume we're entirely done with WiFi.
		wifi_manager_destroy();
		vTaskDelay(pdMS_TO_TICKS(200)); //needed?
		esp_wifi_stop();
	}

	//Grab cur_img and img_shows data from NVS
	int16_t curr_img[IMG_SLOT_COUNT];
	int16_t img_shows[IMG_SLOT_COUNT]={0};
	for (int i=0; i<IMG_SLOT_COUNT; i++) curr_img[i]=-1; //default to invalid
	size_t len=IMG_SLOT_COUNT*sizeof(uint16_t);
	nvs_get_blob(nvs, "curr_img", curr_img, &len);
	len=IMG_SLOT_COUNT*sizeof(uint16_t);
	nvs_get_blob(nvs, "img_shows", img_shows, &len);

	//Find the image that is valid with the highest ID and lowest count
	int img=0;
	for (int i=1; i<IMG_SLOT_COUNT; i++) {
		if (curr_img[i]==-1) continue; //invalid
		if (img_shows[i]<img_shows[img]) {
			img=i;
		} else if (img_shows[i]==img_shows[img]) {
			if (curr_img[i]>curr_img[img]) img=i;
		}
	}
	
	//Show image we found, if any
	if (curr_img[img]!=-1) {
		img_shows[img]++;
		nvs_set_blob(nvs, "img_shows", img_shows, IMG_SLOT_COUNT*sizeof(uint16_t));
		
		ESP_LOGI(TAG, "Displaying img id %d from slot %d", curr_img[img], img);
		epd_send(images[img].data, icon);
		epd_shutdown();
	}

	ESP_LOGI(TAG, "Going to sleep.");
	//Set timezone
	char tz[32];
	len=sizeof(tz);
	if (nvs_get_str(nvs, "tz", tz, &len)==ESP_OK) {
		setenv("TZ", tz, 1);
		tzset();
	}
	//Figure out when to wake.
	int32_t updhour=0;
	nvs_get_i32(nvs, "upd_hour", &updhour);
	struct tm tm;
	time_t now=time(NULL);
	localtime_r(&now, &tm);
	ESP_LOGI(TAG, "Now is %d:%02d, sleeping till %ld:00", tm.tm_hour, tm.tm_min, updhour);
	tm.tm_hour=updhour;
	tm.tm_min=0;
	time_t wake=mktime(&tm);
	int64_t sleep_sec=wake-time(NULL);
	//Make sure this is in the future. We can wake up a bit before the wake time, and without
	//this, it would sleep only a short while until the actual wake time.
	while (sleep_sec<(10*60)) sleep_sec+=(24*60*60);

	if (icon==ICON_BAT_EMPTY) {
		//Sleep forever.
		ESP_LOGI(TAG, "Low battery, so sleeping forever.");
		esp_deep_sleep_start();
	}
	//Zzzzz....
	ESP_LOGI(TAG, "Sleeping %lld sec", sleep_sec);
	esp_sleep_enable_timer_wakeup(sleep_sec*1000ULL*1000ULL);
	vTaskDelay(pdMS_TO_TICKS(100));
	esp_deep_sleep_start();
}
