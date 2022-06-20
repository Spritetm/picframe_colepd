
/*
This code syncs the state of the picture frame (firmware, images in memory, config) to
what the server thinks it should be.

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
#include "esp_wifi.h"
#include <assert.h>
#include "esp_partition.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_flash_partitions.h"
#include "esp_ota_ops.h"
#include "esp_tls_crypto.h"
#include "mbedtls/base64.h"
#include "sync.h"
#include "io.h"
#include "sdkconfig.h"

static const char *TAG="sync";

#define BASE_URL CONFIG_PHOTOFRAME_BASE_URL
#define INFO_PATH "epd-info.php"
#define IMG_PATH "epd-img.php"

#define CHECKFW_OK 0
#define CHECKFW_NEED_UPDATE 1
#define CHECKFW_ERR 2

static void get_app_sha(uint8_t *sha, int for_ota_part) {
	const esp_partition_t *runpart;
	if (!for_ota_part) {
		runpart=esp_ota_get_running_partition();
	} else {
		runpart=esp_ota_get_next_update_partition(NULL);
	}
	esp_app_desc_t runappinfo;
	esp_ota_get_partition_description(runpart, &runappinfo);
	memcpy(sha, runappinfo.app_elf_sha256, 32);
}

static int check_fw_update(cJSON *json) {
	char sha[32];
	cJSON *js_fwsha=cJSON_GetObjectItem(json, "fw_sha");
	if (!js_fwsha) return CHECKFW_ERR;
	const char *sha_txt=cJSON_GetStringValue(js_fwsha);
	if (!sha_txt) return CHECKFW_ERR;
	size_t olen;
	mbedtls_base64_decode((unsigned char*)sha, 32, &olen, (const unsigned char*)sha_txt, strlen(sha_txt));
	if (olen!=32) return CHECKFW_ERR;

	uint8_t cur_sha[32];
	get_app_sha(cur_sha, 0);

	if (memcmp(cur_sha, sha, 32)==0) {
		ESP_LOGI(TAG, "Firmware still up to date.");
		return CHECKFW_OK;
	} else {
		ESP_LOGI(TAG, "Firmware needs update.");
		get_app_sha(cur_sha, 1);
		if (memcmp(cur_sha, sha, 32)==0) {
			ESP_LOGI(TAG, "...but the update was previously attempted and failed");
			return CHECKFW_OK;
		}
		return CHECKFW_NEED_UPDATE;
	}
}

static void do_fw_update(esp_http_client_handle_t http) {
	esp_ota_handle_t ota;
	const esp_partition_t *runpart=esp_ota_get_running_partition();
	const esp_partition_t *updpart=esp_ota_get_next_update_partition(runpart);
	esp_err_t err=esp_ota_begin(updpart, OTA_SIZE_UNKNOWN, &ota);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Could not initialize ota: %s",  esp_err_to_name(err));
		goto err_ota;
	}
	//Data read/write loop
	char buf[1024];
	while(1) {
		int len=esp_http_client_read(http, buf, sizeof(buf));
		if (len==0) {
			if (esp_http_client_is_complete_data_received(http)) {
				err=esp_ota_end(ota);
				if (err != ESP_OK) {
					ESP_LOGE(TAG, "OTA finalize failed: %s",  esp_err_to_name(err));
					goto err;
				}
				break;
			} else {
				ESP_LOGE(TAG, "HTTP reading image failed");
				goto err_ota;
			}
		}
		err=esp_ota_write(ota, buf, len);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "OTA write of len %d failed: %s", len, esp_err_to_name(err));
			goto err_ota;
		}
	}
	err=esp_ota_set_boot_partition(updpart);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "OTA set boot part failed: %s",  esp_err_to_name(err));
		goto err;
	}
	ESP_LOGW(TAG, "Update succesful! Booting into new app version.");
	esp_restart();
	while(1); //never reached
err_ota:
	esp_ota_abort(ota);
err:
}

const char *json_get_string(cJSON *json, const char *name) {
	cJSON *jsnode=cJSON_GetObjectItem(json, name);
	if (!jsnode) return NULL;
	const char *ret=cJSON_GetStringValue(jsnode);
	return ret;
}

esp_err_t picframe_sync(const flash_image_t *images, const esp_partition_t *part) {
	esp_err_t ret=ESP_OK;
	const esp_http_client_config_t config={
		.url=BASE_URL,
		.timeout_ms=16000,
	};
	esp_http_client_handle_t http=esp_http_client_init(&config);
	ESP_GOTO_ON_FALSE(http, ESP_ERR_NO_MEM, err_client_alloc, TAG, "couldn't init http client");

	//Generate info retrieve URL
	unsigned char mac[6];
	char url[192];
	esp_base_mac_addr_get(mac);
	int bat_pwr=io_get_battery_mv();
	uint8_t cur_sha[32];
	get_app_sha(cur_sha, 0);
	char cur_sha_text[16];
	for (int i=0; i<8; i++) sprintf(&cur_sha_text[i*2], "%02X", cur_sha[i]);
	sprintf(url, "%s%s?mac=%02X%02X%02X%02X%02X%02X&bat=%d&fw=%s", BASE_URL, INFO_PATH, 
				mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], bat_pwr, cur_sha_text);

	//Fetch info
	esp_err_t err;
	err=esp_http_client_set_url(http, url);
	ESP_GOTO_ON_ERROR(err, err_http, TAG, "esp_http_client_url to info url failed");
	err=esp_http_client_open(http, 0);
	ESP_GOTO_ON_ERROR(err, err_http, TAG, "esp_http_client_open for info url failed");
	esp_http_client_fetch_headers(http); //note: error ignored
	char resp[512];
	size_t len=esp_http_client_read(http, resp, sizeof(resp));
	ESP_GOTO_ON_FALSE(len>0, ESP_FAIL, err_http, TAG, "couldn't read info url");
	err=esp_http_client_close(http);
	ESP_GOTO_ON_ERROR(err, err_http, TAG, "esp_http_client_open for info url failed");

	//Parse info
	cJSON *json=cJSON_Parse(resp);
	ESP_GOTO_ON_FALSE(json!=NULL, ESP_FAIL, err_http, TAG, "couldn't parse info");
	
	//First, see if there's a software update.
	int needs_update=check_fw_update(json);
	if (needs_update==CHECKFW_NEED_UPDATE) {
		const char *upd_path=json_get_string(json, "fw_upd");
		ESP_GOTO_ON_FALSE(upd_path!=NULL, ESP_FAIL, err_http, TAG, "couldn't parse json from info");
		sprintf(url, "%s%s", BASE_URL, upd_path);
		ESP_LOGI(TAG, "Doing OTA from %s", url);
		err=esp_http_client_set_url(http, url);
		ESP_GOTO_ON_ERROR(err, err_httpjs, TAG, "esp_http_client_url to update url failed");
		err=esp_http_client_open(http, 0);
		ESP_GOTO_ON_ERROR(err, err_httpjs, TAG, "esp_http_client_open for update url failed");
		esp_http_client_fetch_headers(http);
		do_fw_update(http);
		err=esp_http_client_close(http);
		ESP_GOTO_ON_ERROR(err, err_httpjs, TAG, "esp_http_client_close for update url failed");
	}

	//Next, save the things we received to flash
	nvs_handle_t nvs;
	nvs_open("epd", NVS_READWRITE, &nvs);
	const char *tz=json_get_string(json, "tz");
	if (tz) nvs_set_str(nvs, "tz", tz);
	cJSON *j_updhour=cJSON_GetObjectItem(json, "update_hour");
	if (j_updhour) {
		int32_t updhour=cJSON_GetNumberValue(j_updhour);
		nvs_set_i32(nvs, "upd_hour", updhour);
	}
	
	//Set time. We set timezone in the main app from the nvs value.
	cJSON *js_time=cJSON_GetObjectItem(json, "time");
	if (js_time && tz) {
		setenv("TZ", "GMT+0", 1);
		tzset();
		//set time
		struct timeval tv={0};
		tv.tv_sec=cJSON_GetNumberValue(js_time);
		settimeofday(&tv, NULL);
		ESP_LOGI(TAG, "Time set.");
	}

	int16_t curr_img[IMG_SLOT_COUNT];
	int16_t server_img[IMG_SLOT_COUNT];
	int16_t img_shows[IMG_SLOT_COUNT]={0};
	for (int i=0; i<IMG_SLOT_COUNT; i++) {
		curr_img[i]=-1; //default to invalid
		server_img[i]=-1;
	}
	len=IMG_SLOT_COUNT*sizeof(uint16_t);
	nvs_get_blob(nvs, "curr_img", curr_img, &len);
	len=IMG_SLOT_COUNT*sizeof(uint16_t);
	nvs_get_blob(nvs, "img_shows", img_shows, &len);

	//Parse info we got from server
	cJSON *js_ids=cJSON_GetObjectItem(json, "images");
	ESP_GOTO_ON_FALSE(js_ids, ESP_FAIL, err_httpjs, TAG, "no image array in info");
	for (int i=0; i<IMG_SLOT_COUNT; i++) {
		cJSON *js_id=cJSON_GetArrayItem(js_ids, i);
		if (!js_id) continue;
		server_img[i]=cJSON_GetNumberValue(js_id);
	}
	//See if there's anything we need to download
	for (int i=0; i<IMG_SLOT_COUNT; i++) {
		int found=0;
		for (int j=0; j<IMG_SLOT_COUNT; j++) {
			if (server_img[i]==curr_img[j]) found=1;
		}
		if (found) {
			ESP_LOGI(TAG, "Image ID %d: already have that", server_img[i]);
		} else {
			//Need to find a slot to download this to. We can use a slot that contains an
			//image that is stale, as in, not on the list the server gave us.
			int download_slot=-1;
			for (int j=0; j<IMG_SLOT_COUNT; j++) {
				int slot_available=1;
				for (int k=0; k<IMG_SLOT_COUNT; k++) {
					if (curr_img[j]==server_img[k]) slot_available=0;
				}
				if (slot_available) {
					download_slot=j;
					break;
				}
			}
			//Mark slot as invalid, in case the download fails
			curr_img[download_slot]=-1; //invalid
			nvs_set_blob(nvs, "curr_img", curr_img, IMG_SLOT_COUNT*sizeof(uint16_t));
			//Download image to flash partition.
			ESP_LOGI(TAG, "Image ID %d: need to download to slot %d, overwriting image id %d", server_img[i], download_slot, curr_img[download_slot]);
			esp_partition_erase_range(part, download_slot*IMG_SIZE_BYTES, IMG_SIZE_BYTES);
			sprintf(url, "%s%s?id=%d", BASE_URL, IMG_PATH, server_img[i]);
			err=esp_http_client_set_url(http, url);
			ESP_GOTO_ON_ERROR(err, err_httpjs, TAG, "esp_http_client_url to image url failed");
			err=esp_http_client_open(http, 0);
			ESP_GOTO_ON_ERROR(err, err_httpjs, TAG, "esp_http_client_open for image url failed");
			esp_http_client_fetch_headers(http);
			char buf[1024];
			int p=download_slot*IMG_SIZE_BYTES;
			int len;
			int recved=0;
			while ((len=esp_http_client_read(http, buf, sizeof(buf)))>0) {
				esp_partition_write(part, p, buf, len);
				p+=len;
				recved+=len;
			}
			ESP_GOTO_ON_FALSE(len>=0, ESP_FAIL, err_http, TAG, "couldn't read image");
			esp_http_client_close(http);
			if (recved<sizeof(flash_image_hdr_t)+(600*448/2)) {
				//Not sure what happened here... download succeeded but was too small. Server error?
				ESP_LOGW(TAG, "Image data too short. Not marking image as valid.");
			} else {
				//update curr_img to reflect download
				curr_img[download_slot]=server_img[i];
				img_shows[download_slot]=0;
				nvs_set_blob(nvs, "curr_img", curr_img, IMG_SLOT_COUNT*sizeof(uint16_t));
				nvs_set_blob(nvs, "img_shows", img_shows, IMG_SLOT_COUNT*sizeof(uint16_t));
			}
		}
	}
	ESP_LOGI(TAG, "Sync done.");
err_httpjs:
	cJSON_Delete(json);
err_http:
	esp_http_client_cleanup(http);
err_client_alloc:
	return ret;
}
