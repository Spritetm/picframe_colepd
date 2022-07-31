#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "gd.h"
#include <math.h>
#include <assert.h>
#include <string.h>

#define EPD_W 600
#define EPD_H 448

gdImagePtr load_scaled(char *filename) {
	FILE *f;
	f=fopen(filename, "r");
	if (f==NULL) {
		perror(filename);
		return NULL;
	}
	gdImagePtr oim=gdImageCreateFromJpegEx(f, 1);
	if (oim==NULL) {
		//try as png?
		oim=gdImageCreateFromPng(f);
		if (oim==NULL) {
			return NULL;
		}
	}

	gdImagePtr nim=NULL;
	if (gdImageSY(oim)==EPD_H && gdImageSX(oim)==EPD_W && gdImageTrueColor(oim)) {
		//no scaling needed
		nim=oim;
	} else {
		//Need scaling or converting to truecolor
		nim=gdImageCreateTrueColor(EPD_W, EPD_H);
		if (nim==NULL) {
			gdImageDestroy(oim);
			return NULL;
		}
		int bgnd = gdImageColorAllocate(nim, 255,255,255);
		gdImageFilledRectangle(nim, 0, 0, EPD_W, EPD_H, bgnd);
		int nw=EPD_W;
		int nh=(gdImageSY(oim)*EPD_W)/gdImageSX(oim);
		if (nh>EPD_H) {
			nh=EPD_H;
			nw=(gdImageSX(oim)*EPD_H)/gdImageSY(oim);
		}
		gdImageCopyResampled(nim, oim, (EPD_W-nw)/2, (EPD_H-nh)/2, 
					0, 0, nw, nh, gdImageSX(oim), gdImageSY(oim));
		gdImageDestroy(oim);
	}
	return nim;
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

float gamma_linear(float in) {
	return (in > 0.04045) ? pow((in + 0.055) / (1.0 + 0.055), 2.4) : (in / 12.92);
}

//note: takes linearized RGB
void rgb_to_lab(float *rgb, float *lab) {
	float var_R = rgb[0] * 100;
	float var_G = rgb[1] * 100;
	float var_B = rgb[2] * 100;
	if (var_R>100) var_R=100;
	if (var_G>100) var_G=100;
	if (var_B>100) var_B=100;
	if (var_R<0) var_R=0;
	if (var_G<0) var_G=0;
	if (var_B<0) var_B=0;

	//Observer. = 2°, Illuminant = D65
	float inX = var_R * 0.4124f + var_G * 0.3576f + var_B * 0.1805f;
	float inY = var_R * 0.2126f + var_G * 0.7152f + var_B * 0.0722f;
	float inZ = var_R * 0.0193f + var_G * 0.1192f + var_B * 0.9505f;

	float var_X = (inX / 95.047);
	float var_Y = (inY / 100.0);
	float var_Z = (inZ / 108.883);

	if ( var_X > 0.008856 ) 
		var_X = powf(var_X , ( 1.0f/3 )); 
	else 
		var_X = ( 7.787 * var_X ) + ( 16.0f/116 );

	if ( var_Y > 0.008856 )
		var_Y = powf(var_Y , ( 1.0f/3 )); 
	else
		var_Y = ( 7.787 * var_Y ) + ( 16.0f/116 );

	if ( var_Z > 0.008856 )
		var_Z = powf(var_Z , ( 1.0f/3 )); 
	else 
		var_Z = ( 7.787 * var_Z ) + ( 16.0f/116 );

	lab[0] = ( 116 * var_Y ) - 16;
	lab[1] = 500 * ( var_X - var_Y );
	lab[2] = 200 * ( var_Y - var_Z );
}

void dist_diff(float *rgb, int x, int y, int i, float dif) {
	float edge=2;
	if (x<0 || x>=EPD_W) return;
	if (y<0 || y>=EPD_H) return;
	float new_val=rgb[(x+y*EPD_W)*3+i]+dif;
	if (new_val<-edge) new_val=-edge;
	if (new_val>1+edge) new_val=1+edge;
	rgb[(x+y*EPD_W)*3+i]=new_val;
}

float col_diff(float *c1, float *c2) {
	float lab1[3], lab2[3];
	rgb_to_lab(c1, lab1);
	rgb_to_lab(c2, lab2);
	float dif=0;
	for (int i=0; i<3; i++) {
//		dif+=(c1[i]-c2[i])*(c1[i]-c2[i]);
		dif+=(lab1[i]-lab2[i])*(lab1[i]-lab2[i]);
	}

	int lum1=0.2126*c1[0] + 0.7152*c1[1] + 0.0722*c1[2];
	int lum2=0.2126*c2[0] + 0.7152*c2[1] + 0.0722*c2[2];
	int lumdif=lum1-lum2;
//	dif+=(lumdif*lumdif);

	return dif;
}

int main(int argc, char **argv) {
	char *im_in="";
	char *im_out="";
	char *bin_out="";
	int error=0;
	for (int i=1; i<argc; i++) {
		if (strcmp(argv[i], "-p")==0 && i<argc-1) {
			if (im_out[0]!=0) error=1;
			i++;
			im_out=argv[i];
		} else if (strcmp(argv[i], "-o")==0 && i<argc-1) {
			if (bin_out[0]!=0) error=1;
			i++;
			bin_out=argv[i];
		} else {
			if (im_in[0]!=0) error=1;
			im_in=argv[i];
		}
	}
	if (im_in[0]==0 || error) {
		printf("Usage: %s [-o outfile.bin] [-p preview.png] infile.[jpg|png]\n", argv[0]);
		printf("infile can be png or jpeg, if no outfile is given, output will go to stdout\n");
		exit(error);
	}

	//Load image
	gdImagePtr im=load_scaled(im_in);
	if (!im) {
		fprintf(stderr, "Could not load image %s\n", im_in);
		exit(1);
	}

	flash_image_t *bin=calloc(sizeof(flash_image_t), 1);
	assert(bin);
	bin->hdr.id=0xfafa1a1a;
	bin->hdr.timestamp=time(NULL);
	
	//RGB colors as displayed on the screen
	float epd_colors[7][3]={ //rgb
		{0,0,0},
		{1,1,1},
		{0.07, 0.41, 0.09},
//		{0.07, 0.08, 0.34},
		{0.30, 0.25, 0.75},
		{0.63, 0.06, 0.07},
		{0.98, 0.92, 0.24},
		{0.89, 0.51, 0.14}
	};

	//Convert float RGB colors to int colors
	int epd_colors_int[7];
	for (int i=0; i<7; i++) {
		int c=0;
		for (int j=0; j<3; j++) {
			int pc=epd_colors[i][j]*255;
			c=(c<<8)|pc;
		}
		epd_colors_int[i]=c;
	}

	float *rgb=calloc(EPD_W*EPD_H*3, sizeof(float));
	assert(rgb);
	float *rgbp=rgb;
	for (int y=0; y<EPD_H; y++) {
		for (int x=0; x<EPD_W; x++) {
			int c=gdImageGetPixel(im,x,y);
			*rgbp++=gamma_linear(((c>>16)&0xff)/255.0);
			*rgbp++=gamma_linear(((c>>8)&0xff)/255.0);
			*rgbp++=gamma_linear(((c>>0)&0xff)/255.0);
		}
	}

	gdImagePtr tim=gdImageCreateTrueColor(EPD_W, EPD_H);
	assert(tim);
	for (int y=0; y<EPD_H; y++) {
		int ob=0;
		for (int x=0; x<EPD_W; x++) {
			//Find closest color
			int best=0;
			float best_dif=999999999;
			for (int i=0; i<7; i++) {
				float d=col_diff(&rgb[(x+y*EPD_W)*3], epd_colors[i]);
				if (d<best_dif) {
					best_dif=d;
					best=i;
				}
			}
			
			//Distribute difference
			for (int i=0; i<3; i++) {
				float dif=rgb[(x+y*EPD_W)*3+i] - epd_colors[best][i];
				dist_diff(rgb, x+1, y, i, (dif/16)*7);
				dist_diff(rgb, x-1, y+1, i, (dif/16)*3);
				dist_diff(rgb, x, y+1, i, (dif/16)*5);
				dist_diff(rgb, x+1, y+1, i, (dif/16)*1);
			}
			//Set col byte
			if (x&1) {
				bin->data[(y*EPD_W+x)/2]=(ob<<4)|best;
			} else {
				ob=best;
			}
			gdImageSetPixel(tim, x, y, epd_colors_int[best]);
		}
	}

	if (im_out[0]) {
		FILE *of=fopen(im_out, "w");
		if (!of) {
			perror(im_out);
			exit(1);
		}
		gdImagePng(tim, of);
		fclose(of);
	}
	gdImageDestroy(tim);
	if (bin_out[0]) {
		FILE *of=fopen(bin_out, "wb");
		if (!of) {
			perror(bin_out);
			exit(1);
		}
		fwrite(bin, sizeof(flash_image_t)-sizeof(bin->padding), 1, of);
		fclose(of);
	} else {
		fwrite(bin, sizeof(flash_image_t)-sizeof(bin->padding), 1, stdout);
	}
}

