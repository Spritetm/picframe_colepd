#pragma once

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

static inline int img_valid(const flash_image_hdr_t *img) {
	return (img->id==0xfafa1a1a);
}
