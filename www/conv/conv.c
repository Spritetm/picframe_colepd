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

#define USE_LAB 0
#define COL_DIST_CLIP_VAL 0
#define COL_DIST_CLIP_VAL_AB 0

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

//note: takes srgb [0..1], outputs linear rgb [0..1]
float gamma_linear(float in) {
	return (in > 0.04045) ? pow((in + 0.055) / (1.0 + 0.055), 2.4) : (in / 12.92);
}

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
	for (int i=0; i<3; i++) lab[i]=lab[i]/100.0; //[0..100] -> [0..1]
}

float clamp(float val, float min, float max) {
	if (val<min) val=min;
	if (val>max) val=max;
	return val;
}

void dist_diff(float *pixels, int x, int y, int i, float dif) {
	if (x<0 || x>=EPD_W) return;
	if (y<0 || y>=EPD_H) return;
	float new_val=pixels[(x+y*EPD_W)*3+i]+dif;
#if USE_LAB
	if (i==0) { //L
		new_val=clamp(new_val, -COL_DIST_CLIP_VAL, 1+COL_DIST_CLIP_VAL);
	} else { //A, B
		new_val=clamp(new_val, -1-COL_DIST_CLIP_VAL, 1+COL_DIST_CLIP_VAL);
	}
#else
	new_val=clamp(new_val, -COL_DIST_CLIP_VAL, 1+COL_DIST_CLIP_VAL);
#endif
	pixels[(x+y*EPD_W)*3+i]=new_val;
}

float col_diff_lab(float *c1, float *c2) {
	float dif=0;
	const float dif_weight[3]={1, 1, 1};
	for (int i=0; i<3; i++) {
		float vdif=(c1[i]-c2[i]);
		vdif*=dif_weight[i];
		dif+=(vdif*vdif);
	}
	return dif;
}


float col_diff_rgb(float *c1, float *c2) {
	float lab1[3], lab2[3];
	rgb_to_lab(c1, lab1);
	rgb_to_lab(c2, lab2);

	//Color is less important when the luminosity is high or low (spherical
	//color space), so we compensate for that.
	float avg_l=(c1[0] + c2[0])/2;
	avg_l=clamp(avg_l, 0, 1);
	float col_comp=sin(avg_l*M_PI);


	float dif=0;
	for (int i=0; i<3; i++) {
		float vd=(lab1[i]-lab2[i]);
		if (i!=0) vd*=col_comp;
		dif+=vd*vd;
	}



//	int lum1=0.2126*c1[0] + 0.7152*c1[1] + 0.0722*c1[2];
//	int lum2=0.2126*c2[0] + 0.7152*c2[1] + 0.0722*c2[2];
//	int lumdif=lum1-lum2;
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
#if USE_LAB
	//Convert to LAB as well
	float epd_colors_lab[7][3];
	for (int i=0; i<7; i++) {
		rgb_to_lab(epd_colors[i], epd_colors_lab[i]);
	}
#endif

	float *pixels=calloc(EPD_W*EPD_H*3, sizeof(float));
	assert(pixels);
	float *pixelp=pixels;
	for (int y=0; y<EPD_H; y++) {
		for (int x=0; x<EPD_W; x++) {
			int c=gdImageGetPixel(im,x,y);
#if USE_LAB
			float rgbval[3], lab[3];
			rgbval[0]=gamma_linear(((c>>16)&0xff)/255.0);
			rgbval[1]=gamma_linear(((c>>8)&0xff)/255.0);
			rgbval[2]=gamma_linear(((c>>0)&0xff)/255.0);
			rgb_to_lab(rgbval, lab);
			for (int i=0; i<3; i++) *pixelp++=lab[i];
#else
			*pixelp++=gamma_linear(((c>>16)&0xff)/255.0);
			*pixelp++=gamma_linear(((c>>8)&0xff)/255.0);
			*pixelp++=gamma_linear(((c>>0)&0xff)/255.0);
#endif
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
#if USE_LAB
				float d=col_diff_lab(&pixels[(x+y*EPD_W)*3], epd_colors_lab[i]);
#else
				float d=col_diff_rgb(&pixels[(x+y*EPD_W)*3], epd_colors[i]);
#endif
				if (d<best_dif) {
					best_dif=d;
					best=i;
				}
			}
			
			//Distribute difference
			float dif[3];
			for (int i=0; i<3; i++) {
#if USE_LAB
				dif[i]=pixels[(x+y*EPD_W)*3+i] - epd_colors_lab[best][i];
#else
				dif[i]=pixels[(x+y*EPD_W)*3+i] - epd_colors[best][i];
#endif
			}
#if USE_LAB
			//Color is less important when the luminosity is high or low (spherical
			//color space), so we compensate for that.
			float avg_l=(pixels[(x+y*EPD_W)*3] + epd_colors[best][0])/2;
			avg_l=clamp(avg_l, 0, 1);
			float col_comp=sin(avg_l*M_PI);
			dif[1]=dif[1]*col_comp;
			dif[2]=dif[2]*col_comp;
#endif
			for (int i=0; i<3; i++) {
				dist_diff(pixels, x+1, y, i, (dif[i]/16.0)*7.0);
				dist_diff(pixels, x-1, y+1, i, (dif[i]/16.0)*3.0);
				dist_diff(pixels, x, y+1, i, (dif[i]/16.0)*5.0);
				dist_diff(pixels, x+1, y+1, i, (dif[i]/16.0)*1.0);
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

