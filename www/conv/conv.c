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
#include <stdint.h>
#include <time.h>
#include "gd.h"
#include <math.h>
#include <assert.h>
#include <string.h>

#define EPD_W 600
#define EPD_H 448

#define EPD_UPSIDE_DOWN 1

//This loads a png/jpg file and if needed converts it to truecolor, crops the center to the
//EPD aspect ratio and scales to EPD_W/EPD_H.
gdImagePtr load_scaled(char *filename) {
	FILE *f;
	f=fopen(filename, "r");
	if (f==NULL) {
		perror(filename);
		return NULL;
	}
	//We check the first 8 bytes of the file to check if it's PNG.
	char pnghdr[8]={0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
	char buf[8];
	fread(buf, 8, 1, f);
	rewind(f);
	//If match, we load it as PNG, if not we load it as JPEG.
	gdImagePtr oim;
	if (memcmp(pnghdr, buf, 8)==0) {
		oim=gdImageCreateFromPng(f);
	} else {
		oim=gdImageCreateFromJpegEx(f, 1);
	}
	if (oim==NULL) {
		return NULL;
	}

	gdImagePtr nim=NULL;
	if (gdImageSY(oim)==EPD_H && gdImageSX(oim)==EPD_W && gdImageTrueColor(oim)) {
		//no scaling needed
		nim=oim;
	} else {
		//Need scaling and/or converting to truecolor
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

//The two typedefs define what the epd binary image looks like.
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

//Convert SRGB to linear.
//note: takes srgb [0..1], outputs linear rgb [0..1]
float gamma_linear(float in) {
	return (in > 0.04045) ? pow((in + 0.055) / (1.0 + 0.055), 2.4) : (in / 12.92);
}

//Converts a RGB float to a CIE-LAB float
//note: takes linearized RGB [0..1], outputs LAB [0..1]
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

//Returns the input value clamped between min and max
float clamp(float val, float min, float max) {
	if (val<min) val=min;
	if (val>max) val=max;
	return val;
}

//Given an array of [rgb] floats, adds the difference diff to the
//pixel at [x,y], with i indicating the color (0=r, 1=g, 2=b)
void dist_diff(float *pixels, int x, int y, int i, float dif) {
	if (x<0 || x>=EPD_W) return;
	if (y<0 || y>=EPD_H) return;
	float new_val=pixels[(x+y*EPD_W)*3+i]+dif;
	new_val=clamp(new_val, 0, 1);
	pixels[(x+y*EPD_W)*3+i]=new_val;
}

static inline float deg2rad(float deg) {
	return (2 * M_PI * deg) / 360.0;
}

static inline float rad2deg(float rad) {
	return 360.0 * rad / (2 * M_PI);
}

//This, when fed two RGB values in range [0..1], returns the deltaE00 difference between the two.
//The higher the return value, the more perceptually different the two RGB values are.
float col_diff(float *rgb1, float *rgb2) {
	float lab1[3], lab2[3];
	rgb_to_lab(rgb1, lab1);
	rgb_to_lab(rgb2, lab2);

	//deltaE00 code stolen from https://github.com/hamada147/IsThisColourSimilar/blob/master/Colour.js
	// Start Equation
	// Equation exist on the following URL http://www.brucelindbloom.com/index.html?Eqn_DeltaE_CIE2000.html
	float avgL=(lab1[0]+lab2[0])/2;
	float c1 = sqrtf(powf(lab1[1], 2) + powf(lab1[2], 2));
	float c2 = sqrtf(powf(lab2[1], 2) + powf(lab2[2], 2));
	float avgC = (c1 + c2) / 2;
	float g = (1 - sqrtf(powf(avgC, 7) / (powf(avgC, 7) + powf(25, 7)))) / 2;

	float a1p = lab1[1] * (1 + g);
	float a2p = lab2[1] * (1 + g);

	float c1p = sqrtf(powf(a1p, 2) + powf(lab1[2], 2));
	float c2p = sqrtf(powf(a2p, 2) + powf(lab2[2], 2));

	float avgCp = (c1p + c2p) / 2;

	float h1p = rad2deg(atan2f(lab1[2], a1p));
	if (h1p < 0) h1p = h1p + 360;
	float h2p = rad2deg(atan2f(lab2[2], a2p));
	if (h2p < 0) h2p = h2p + 360;

	float avghp = fabsf(h1p - h2p) > 180 ? (h1p + h2p + 360) / 2 : (h1p + h2p) / 2;
	float t = 1 - 0.17 * cosf(deg2rad(avghp - 30)) + 0.24 * cosf(deg2rad(2 * avghp)) + 0.32 * cosf(deg2rad(3 * avghp + 6)) - 0.2 * cosf(deg2rad(4 * avghp - 63));
	float deltahp = h2p - h1p;
	if (fabsf(deltahp) > 180) {
		if (h2p <= h1p) {
			deltahp += 360;
		} else {
			deltahp -= 360;
		}
	}

	float deltalp = lab2[0] - lab1[0];
	float deltacp = c2p - c1p;

	deltahp = 2 * sqrtf(c1p * c2p) * sinf(deg2rad(deltahp) / 2);

	float sl = 1 + ((0.015 * powf(avgL - 50, 2)) / sqrtf(20 + powf(avgL - 50, 2)));
	float sc = 1 + 0.045 * avgCp;
	float sh = 1 + 0.015 * avgCp * t;

	float deltaro = 30 * expf(-(powf((avghp - 275) / 25, 2)));
	float rc = 2 * sqrtf(powf(avgCp, 7) / (powf(avgCp, 7) + powf(25, 7)));
	float rt = -rc * sinf(2 * deg2rad(deltaro));

	float kl = 1;
	float kc = 1;
	float kh = 1;

	float deltaE = sqrtf(powf(deltalp / (kl * sl), 2) + powf(deltacp / (kc * sc), 2) + powf(deltahp / (kh * sh), 2) + rt * (deltacp / (kc * sc)) * (deltahp / (kh * sh)));
	return deltaE;
}

int main(int argc, char **argv) {
	char *im_in="";
	char *im_out="";
	char *bin_out="";
	int error=0;
	//Find & parse command line arguments
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
		printf("infile can be png or jpeg; if no outfile is given, output will go to stdout\n");
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
	bin->hdr.id=0xfafa1a1a; //magic header
	bin->hdr.timestamp=time(NULL);
	
	//RGB colors as displayed on the screen
	//These are calculated by grabbing a test pattern which shows all colors, taking a picture,
	//then using an image editor to max out the levels for max contrast & saturation, then taking
	//the linear (not SRGB) RGB values and putting them here.
	float epd_colors[7][3]={ //rgb
		{0,0,0},
		{1,1,1},
		{0.059, 0.329, 0.119},
		{0.061, 0.147, 0.336},
		{0.574, 0.066, 0.010},
		{0.982, 0.756, 0.004},
		{0.795, 0.255, 0.018},
	};

	//Convert float RGB colors to int colors for putting on the preview png
	int epd_colors_int[7];
	for (int i=0; i<7; i++) {
		int c=0;
		for (int j=0; j<3; j++) {
			int pc=epd_colors[i][j]*255;
			c=(c<<8)|pc;
		}
		epd_colors_int[i]=c;
	}

	//Convert image to an array of floats so we can Floyd-Steinberg without limiting ourselves
	//to the range of ints
	float *pixels=calloc(EPD_W*EPD_H*3, sizeof(float));
	assert(pixels);
	float *pixelp=pixels;
	for (int y=0; y<EPD_H; y++) {
		for (int x=0; x<EPD_W; x++) {
			int c=gdImageGetPixel(im,x,y);
			*pixelp++=gamma_linear(((c>>16)&0xff)/255.0);
			*pixelp++=gamma_linear(((c>>8)&0xff)/255.0);
			*pixelp++=gamma_linear(((c>>0)&0xff)/255.0);
		}
	}

	//Create preview image
	gdImagePtr tim=gdImageCreateTrueColor(EPD_W, EPD_H);
	assert(tim);
	for (int y=0; y<EPD_H; y++) {
		int ob=0;
		for (int x=0; x<EPD_W; x++) {
			//Find closest color for this pixel from the palette the epd can display
			int best=0;
			float best_dif=999999999;
			for (int i=0; i<7; i++) {
				float d=col_diff(&pixels[(x+y*EPD_W)*3], epd_colors[i]);
				if (d<best_dif) {
					best_dif=d;
					best=i;
				}
			}
			
			//Distribute difference between chosen and ideal color using Floyd-Steinberg
			for (int i=0; i<3; i++) {
				float dif=pixels[(x+y*EPD_W)*3+i] - epd_colors[best][i];
				dist_diff(pixels, x+1, y, i, (dif/16.0)*7.0);
				dist_diff(pixels, x-1, y+1, i, (dif/16.0)*3.0);
				dist_diff(pixels, x, y+1, i, (dif/16.0)*5.0);
				dist_diff(pixels, x+1, y+1, i, (dif/16.0)*1.0);
			}
//			best=((x*14)/448)%7; //uncomment for test image
			//Set byte in output EPD binary data
#if EPD_UPSIDE_DOWN
			if (x&1) {
				bin->data[((EPD_H-1-y)*EPD_W+(EPD_W-1-x))/2]=ob|(best<<4);
			} else {
				ob=best;
			}
#else
			if (x&1) {
				bin->data[(y*EPD_W+x)/2]=(ob<<4)|best;
			} else {
				ob=best;
			}
#endif
			//Also set pixel in output
			gdImageSetPixel(tim, x, y, epd_colors_int[best]);
		}
	}

	if (im_out[0]) {
		//Write preview image
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
		//Write binary output to file
		FILE *of=fopen(bin_out, "wb");
		if (!of) {
			perror(bin_out);
			exit(1);
		}
		fwrite(bin, sizeof(flash_image_t)-sizeof(bin->padding), 1, of);
		fclose(of);
	} else {
		//Write binary output to stdout
		fwrite(bin, sizeof(flash_image_t)-sizeof(bin->padding), 1, stdout);
	}
	exit(0);
}

