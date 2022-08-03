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
#include "sync.h"

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

void app_main(void) {
	_Static_assert((sizeof(flash_image_t) == IMG_SIZE_BYTES), "flash_image_t not right size");
	connect_sema=xSemaphoreCreateBinary();

	/* start the wifi manager */
	wifi_manager_start();
	wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);

	const flash_image_t *images=NULL;
	spi_flash_mmap_handle_t mmap_handle;
	const esp_partition_t *part=esp_partition_find_first(123, 0, NULL);
	esp_partition_mmap(part, 0, IMG_SIZE_BYTES*IMG_SLOT_COUNT, SPI_FLASH_MMAP_DATA, (const void**)&images, &mmap_handle);

	nvs_handle_t nvs;
	nvs_open("epd", NVS_READWRITE, &nvs);

	ESP_LOGI(TAG, "Waiting for connection...");
	//wait for connection
	int can_connect=xSemaphoreTake(connect_sema, pdMS_TO_TICKS(30*1000));

	if (can_connect) {
		//todo: timeout on this?
		picframe_sync(images, part);
	}
	wifi_manager_destroy();
	vTaskDelay(pdMS_TO_TICKS(200)); //needed?
	esp_wifi_stop();

	//Grab cur_img and img_shows data
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
	
	if (curr_img[img]!=-1) {
		img_shows[img]++;
		nvs_set_blob(nvs, "img_shows", img_shows, IMG_SLOT_COUNT*sizeof(uint16_t));
		
		ESP_LOGI(TAG, "Displaying img id %d from slot %d", curr_img[img], img);

		epd_send(images[img].data, 0);

		epd_shutdown();
	}
	ESP_LOGI(TAG, "Deep sleep.");
	//Set timezone
	char tz[32];
	len=sizeof(tz);
	if (nvs_get_str(nvs, "tz", tz, &len)==ESP_OK) {
		setenv("TZ", tz, 1);
		tzset();
	}
	int32_t updhour=0;
	nvs_get_i32(nvs, "upd_hour", &updhour);
	struct tm tm;
	time_t now=time(NULL);
	localtime_r(&now, &tm);
	ESP_LOGI(TAG, "Now is %d:%02d, sleeping till %d:00", tm.tm_hour, tm.tm_min, updhour);
	tm.tm_hour=updhour;
	tm.tm_min=0;
	time_t wake=mktime(&tm);
	int64_t sleep_sec=wake-time(NULL);
	while (sleep_sec<10) sleep_sec+=(24*60*60); //make sure this is in the future
	ESP_LOGI(TAG, "Sleeping %lld sec", sleep_sec);
	esp_sleep_enable_timer_wakeup(sleep_sec*1000ULL*1000ULL);
	esp_deep_sleep_start();
}
