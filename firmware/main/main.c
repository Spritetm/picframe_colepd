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

const char *TAG="main";

typedef struct __attribute__((packed)) {
	uint32_t id;
	uint64_t timestamp;
	uint8_t unused[64-12];
} flash_image_hdr_t;

typedef struct __attribute__((packed)) {
	flash_image_hdr_t hdr;
	uint8_t data[600*448/2];
	uint8_t padding[768-sizeof(flash_image_hdr_t)];
} flash_image_t;

#define IMG_SLOT_COUNT 10
#define IMG_SIZE_BYTES 0x21000

int img_valid(const flash_image_hdr_t *img) {
	return (img->id==0xfafa1a1a);
}

int fetch_new_image(const flash_image_t *images, const esp_partition_t *part) {
	//If we have a connection, try to grab a new picture
	ESP_LOGI(TAG, "Fetching new image...");
	const esp_http_client_config_t config={
		.url="http://192.168.5.5/epd/epd-img.php",
	};
	esp_http_client_handle_t http=esp_http_client_init(&config);
	esp_http_client_open(http, 0);
	esp_http_client_fetch_headers(http);
	flash_image_hdr_t hdr;
	esp_http_client_read(http, (char*)&hdr, sizeof(hdr));
	for (int i=0; i<IMG_SLOT_COUNT; i++) {
		if (images[i].hdr.timestamp == hdr.timestamp) {
			//Already have that image.
			ESP_LOGI(TAG, "Already have image.");
			esp_http_client_close(http);
			esp_http_client_cleanup(http);
			return -1;
		}
	}

	//We do not have that yet. Find oldest slot and erase.
	int oldest=0;
	for (int i=0; i<IMG_SLOT_COUNT; i++) {
		if (images[i].hdr.timestamp < images[oldest].hdr.timestamp) oldest=i;
		if (!img_valid(&images[i].hdr)) {
			//erased, use immediately
			oldest=i;
			break;
		}
	}
	ESP_LOGI(TAG, "Saving image to slot %d", oldest);
	esp_partition_erase_range(part, oldest*IMG_SIZE_BYTES, IMG_SIZE_BYTES);
	char buf[1024];
	int p=oldest*IMG_SIZE_BYTES+sizeof(flash_image_hdr_t);
	int len;
	int recved=0;
	while ((len=esp_http_client_read(http, buf, sizeof(buf)))>0) {
		esp_partition_write(part, p, buf, len);
		p+=len;
		recved+=len;
	}
	//write header after all else is succesfully done.
	esp_partition_write(part, oldest*IMG_SIZE_BYTES, &hdr, sizeof(hdr));

	ESP_LOGI(TAG, "Saved %d bytes.", recved);
	esp_http_client_close(http);
	esp_http_client_cleanup(http);
	return oldest;
}


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
	nvs_open("img", NVS_READWRITE, &nvs);

	ESP_LOGI(TAG, "Waiting for connection...");
	//wait for connection
	int can_connect=xSemaphoreTake(connect_sema, pdMS_TO_TICKS(30*1000));

	int32_t to_display=-1;
	if (can_connect) {
		to_display=fetch_new_image(images, part);
		if (to_display>=0) nvs_set_i32(nvs, "l", to_display);
	}
	wifi_manager_destroy();
	vTaskDelay(pdMS_TO_TICKS(200)); //needed?
	esp_wifi_stop();

	if (to_display==-1) {
		nvs_get_i32(nvs, "l", &to_display);
		do {
			to_display++;
			if (to_display<0 || to_display>=IMG_SLOT_COUNT) to_display=0;
		} while (img_valid(&images[to_display].hdr));
	}
	ESP_LOGI(TAG, "Displaying img from slot %d", to_display);

	epd_send(images[to_display].data, 0);
}
