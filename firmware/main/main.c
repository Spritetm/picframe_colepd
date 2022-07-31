#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "lwip/ip_addr.h"
#include "wifi_manager.h"
#include <assert.h>
#include "esp_partition.h"
#include "esp_http_client.h"
#include "nvs.h"

#ifdef HSPI_HOST
#define EPD_HOST	HSPI_HOST
#define PIN_NUM_BUSY 25
#define PIN_NUM_MOSI 14
#define PIN_NUM_CLK	 13
#define PIN_NUM_CS	 15
#define PIN_NUM_RST	 26
#define PIN_NUM_DC	 27
#else
#define EPD_HOST	SPI2_HOST
#define PIN_NUM_BUSY 4
#define PIN_NUM_MOSI 9
#define PIN_NUM_CLK	 8
#define PIN_NUM_CS	 7
#define PIN_NUM_RST	 5
#define PIN_NUM_DC	 6
#endif


//To speed up transfers, every SPI transfer sends a bunch of lines. This define specifies how many. More means more memory use,
//but less overhead for setting up / finishing transfers. Make sure 240 is dividable by this.
#define PARALLEL_LINES 16


const char *TAG="epd";

/*
 The EPD needs a bunch of command/argument values to be initialized. They are stored in this struct.
*/
typedef struct {
	uint8_t cmd;
	uint8_t data[16];
	uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} epd_init_cmd_t;
#define INIT_DATA_WAIT 0x80

static const epd_init_cmd_t epd_init_cmds[]={
	{0x00, {0xef, 0x08}, 2},
	{0x01, {0x37, 0x00, 0x23, 0x23}, 4},
	{0x03, {0x00}, 1},
	{0x06, {0xC7, 0xC7, 0x1D}, 3},
	{0x30, {0x39}, 1},
	{0x41, {0x00}, 1},
	{0x50, {0x37}, 1},
	{0x60, {0x22}, 1},
	{0x61, {0x02, 0x58, 0x01, 0xC0}, 4},
	{0xE3, {0xAA}, 1 | INIT_DATA_WAIT},
	{0x50, {0x37}, 1},
	{0, {0}, 0xFF}
};

/* Send a command to the EPD. Uses spi_device_polling_transmit, which waits
 * until the transfer is complete.
 *
 * Since command transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
void epd_cmd(spi_device_handle_t spi, const uint8_t cmd, bool keep_cs_active) {
	esp_err_t ret;
	spi_transaction_t t;
	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length=8;						//Command is 8 bits
	t.tx_buffer=&cmd;				//The data is the cmd itself
	t.user=(void*)0;				//D/C needs to be set to 0
	if (keep_cs_active) {
	  t.flags = SPI_TRANS_CS_KEEP_ACTIVE;	//Keep CS active after data transfer
	}
	ret=spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.
}

/* Send data to the EPD. Uses spi_device_polling_transmit, which waits until the
 * transfer is complete.
 *
 * Since data transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
void epd_data(spi_device_handle_t spi, const uint8_t *data, int len)
{
	esp_err_t ret;
	spi_transaction_t t;
	if (len==0) return;				//no need to send anything
	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length=len*8;					//Len is in bytes, transaction length is in bits.
	t.tx_buffer=data;				//Data
	t.user=(void*)1;				//D/C needs to be set to 1
	ret=spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.
}

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
void epd_spi_pre_transfer_callback(spi_transaction_t *t) {
	int dc=(int)t->user;
	gpio_set_level(PIN_NUM_DC, dc);
}

void wait_busy(int val, int timeout_ms) {
	int64_t tout=esp_timer_get_time()+timeout_ms*1000;
	while(gpio_get_level(PIN_NUM_BUSY)!=val) {
		vTaskDelay(2);
		if (esp_timer_get_time()>tout) {
			ESP_LOGE(TAG, "Timeout on waiting for busy to go %s!", val?"high":"low");
		}
	}
}

//Initialize the display
void epd_init(spi_device_handle_t spi) {
	int cmd=0;

	const gpio_config_t cfg[2]={
		{
			.pin_bit_mask=(1<<PIN_NUM_DC)|(1<<PIN_NUM_RST),
			.mode=GPIO_MODE_OUTPUT
		}, {
			.pin_bit_mask=(1<<PIN_NUM_BUSY),
			.mode=GPIO_MODE_INPUT,
			.pull_up_en=GPIO_PULLUP_ENABLE
		}
	};
	gpio_config(&cfg[0]);
	gpio_config(&cfg[1]);

	//Initialize non-SPI GPIOs
	gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
	gpio_set_direction(PIN_NUM_BUSY, GPIO_MODE_INPUT);

	//Reset the display
	gpio_set_level(PIN_NUM_RST, 0);
	vTaskDelay(pdMS_TO_TICKS(10));
	gpio_set_level(PIN_NUM_RST, 1);
	vTaskDelay(pdMS_TO_TICKS(100));

	//wait for not busy
	wait_busy(1, 1000);

	//Send all the commands
	while (epd_init_cmds[cmd].databytes!=0xff) {
		epd_cmd(spi, epd_init_cmds[cmd].cmd, false);
		uint8_t data[16];
		memcpy(data, epd_init_cmds[cmd].data, 16);
		epd_data(spi, data, epd_init_cmds[cmd].databytes&0x1F);
		if (epd_init_cmds[cmd].databytes&0x80) {
			vTaskDelay(pdMS_TO_TICKS(100));
		}
		cmd++;
	}
}

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

#define IMG_SLOT_COUNT 15
#define IMG_SIZE_BYTES 0x21000

int img_valid(const flash_image_hdr_t *img) {
	return (img->id==0xfafa1a1a);
}

int fetch_new_image(const flash_image_t *images, const esp_partition_t *part) {
	//If we have a connection, try to grab a new picture
	ESP_LOGI(TAG, "Fetching new image...");
	const esp_http_client_config_t config={
		.url="http://192.168.5.5/epd/epd-img.bin",
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

	/* initialize epd */
	esp_err_t ret;
	spi_device_handle_t spi;
	spi_bus_config_t buscfg={
		.miso_io_num=-1,
		.mosi_io_num=PIN_NUM_MOSI,
		.sclk_io_num=PIN_NUM_CLK,
		.quadwp_io_num=-1,
		.quadhd_io_num=-1,
		.max_transfer_sz=PARALLEL_LINES*600/2+8
	};
	spi_device_interface_config_t devcfg={
		.clock_speed_hz=1*1000*1000,			//Clock out at 10 MHz
		.mode=0,								//SPI mode 0
		.spics_io_num=PIN_NUM_CS,				//CS pin
		.queue_size=7,							//We want to be able to queue 7 transactions at a time
		.pre_cb=epd_spi_pre_transfer_callback,	//Specify pre-transfer callback to handle D/C line
	};
	//Initialize the SPI bus
	ret=spi_bus_initialize(EPD_HOST, &buscfg, SPI_DMA_CH_AUTO);
	ESP_ERROR_CHECK(ret);
	//Attach the EPD to the SPI bus
	ret=spi_bus_add_device(EPD_HOST, &devcfg, &spi);
	ESP_ERROR_CHECK(ret);
	//Initialize the EPD
	epd_init(spi);

	const flash_image_t *images=NULL;
	spi_flash_mmap_handle_t mmap_handle;
	const esp_partition_t *part=esp_partition_find_first(123, 0, NULL);
	esp_partition_mmap(part, 0, 2*1024*1024, SPI_FLASH_MMAP_DATA, (const void**)&images, &mmap_handle);

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
	if (to_display==-1) {
		nvs_get_i32(nvs, "l", &to_display);
		do {
			to_display++;
			if (to_display<0 || to_display>=IMG_SLOT_COUNT) to_display=0;
		} while (img_valid(&images[to_display].hdr));
	}
	ESP_LOGI(TAG, "Displaying img from slot %d", to_display);

	epd_cmd(spi, 0x61, false);
	uint8_t data[4]={0x02, 0x58, 0x01, 0xC0};
	epd_data(spi, data, 4);
	epd_cmd(spi, 0x10, false);
	for (int y=0; y<448; y++) {
		uint8_t buf[300];
		memcpy(buf, &images[to_display].data[y*300], 300);
		epd_data(spi, buf, 300);
	}
	epd_cmd(spi, 0x4, false);
	wait_busy(1, 30000);
	epd_cmd(spi, 0x12, false);
	wait_busy(1, 30000);
	epd_cmd(spi, 0x2, false);
	wait_busy(0, 30000);
	ESP_LOGI(TAG, "Displayed image.");
}
