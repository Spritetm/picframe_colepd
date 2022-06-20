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
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#ifdef HSPI_HOST
//Waveshare ESP32 board
#define EPD_HOST	HSPI_HOST
#define PIN_NUM_BUSY 25
#define PIN_NUM_MOSI 14
#define PIN_NUM_CLK	 13
#define PIN_NUM_CS	 15
#define PIN_NUM_RST	 26
#define PIN_NUM_DC	 27
#else
//Actual picframe_epd board.
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

static const char *TAG="epd";

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
 */
static void epd_cmd(spi_device_handle_t spi, const uint8_t cmd) {
	esp_err_t ret;
	spi_transaction_t t;
	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length=8;						//Command is 8 bits
	t.tx_buffer=&cmd;				//The data is the cmd itself
	t.user=(void*)0;				//D/C needs to be set to 0
	ret=spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.
}

/* Send data to the EPD. Uses spi_device_polling_transmit, which waits until the
 * transfer is complete.
 */
static void epd_data(spi_device_handle_t spi, const uint8_t *data, int len) {
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
static void epd_spi_pre_transfer_callback(spi_transaction_t *t) {
	int dc=(int)t->user;
	gpio_set_level(PIN_NUM_DC, dc);
}

//Wait for the EPD to not be busy anymore
static void wait_busy(int val, int timeout_ms) {
	int64_t tout=esp_timer_get_time()+timeout_ms*1000;
	while(gpio_get_level(PIN_NUM_BUSY)!=val) {
		vTaskDelay(2);
		if (esp_timer_get_time()>tout) {
			ESP_LOGE(TAG, "Timeout on waiting for busy to go %s!", val?"high":"low");
			return;
		}
	}
}

//Initialize the display
static void epd_init(spi_device_handle_t spi) {
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
		epd_cmd(spi, epd_init_cmds[cmd].cmd);
		uint8_t data[16];
		memcpy(data, epd_init_cmds[cmd].data, 16);
		epd_data(spi, data, epd_init_cmds[cmd].databytes&0x1F);
		if (epd_init_cmds[cmd].databytes&0x80) {
			vTaskDelay(pdMS_TO_TICKS(100));
		}
		cmd++;
	}
}

extern const uint8_t icons_bmp_start[] asm("_binary_icons_bmp_start");

spi_device_handle_t spi;

void epd_send(const uint8_t *epddata, int icon) {
	gpio_hold_dis(PIN_NUM_CS);
	gpio_hold_dis(PIN_NUM_RST);

	/* initialize epd */
	esp_err_t ret;
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
	
	epd_cmd(spi, 0x61);
	uint8_t data[4]={0x02, 0x58, 0x01, 0xC0};
	epd_data(spi, data, 4);
	epd_cmd(spi, 0x10);
	int bmp_pix_start=icons_bmp_start[0xa]+(icons_bmp_start[0xb]<<8); //actually header is 32-bit... care.
	//ESP_LOGI(TAG, "bmp starts at 0x%X", bmp_pix_start);
	for (int y=0; y<448; y++) {
		uint8_t buf[300];
		memcpy(buf, &epddata[y*300], 300);
		if (icon!=0 && y<32) {
			//the bmp is a file with 4-bit info. Each icon is 32x32 pixels (aka 32x16 bytes)
			memcpy(buf, &icons_bmp_start[bmp_pix_start+y*16+(icon-1)*(16*32)], 16);
		}
		epd_data(spi, buf, 300);
	}
	epd_cmd(spi, 0x4);
	wait_busy(1, 30000);
	epd_cmd(spi, 0x12);
	wait_busy(1, 30000);
	epd_cmd(spi, 0x2);
	wait_busy(1, 30000);
	ESP_LOGI(TAG, "Displayed image.");
}

void epd_shutdown() {
	//deep sleep
	epd_cmd(spi, 0x7);
	uint8_t sdata=0xA5;
	epd_data(spi, &sdata, 1);
	const gpio_config_t cfg={
		.pin_bit_mask=(1<<PIN_NUM_DC)|(1<<PIN_NUM_RST)|(1<<PIN_NUM_MOSI)|(1<<PIN_NUM_CS)|(1<<PIN_NUM_CLK),
		.mode=GPIO_MODE_OUTPUT
	};
	//not sure if CS survives deep sleep, as it's GPIO15... RST surely does.
	gpio_set_level(PIN_NUM_CS, 0);
	gpio_set_level(PIN_NUM_RST, 1);
	gpio_config(&cfg);
	gpio_hold_en(PIN_NUM_CS);
	gpio_hold_en(PIN_NUM_RST);
}
