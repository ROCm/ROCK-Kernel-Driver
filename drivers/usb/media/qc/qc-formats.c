/* Start of file */

/* {{{ [fold] Comments */
/*
 * qc-usb, linux V4L driver for the Logitech QuickCam family
 *
 * qc-formats.c - converts color formats
 *
 * Copyright (c) 2001 Jean-Frederic Clere (RGB->YUV conversion)
 *               2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca> (YUV->RGB conversion, from mpeg2dec)
 *               2003 Tuukka Toivonen (Bayer->RGB conversion)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
/* }}} */

#ifdef NOKERNEL
#include "quickcam.h"
#else
#include <linux/quickcam.h>
#endif
#include <asm/byteorder.h>

#ifndef v4l2_fourcc
/*  Four-character-code (FOURCC) */
#define v4l2_fourcc(a,b,c,d)\
        (((__u32)(a)<<0)|((__u32)(b)<<8)|((__u32)(c)<<16)|((__u32)(d)<<24))
#endif

/* {{{ [fold] **** qc_yuv:  Start of RGB to YUV conversion functions  ******************** */

/* {{{ [fold] qc_yuv_interp[256][8] */
static signed short qc_yuv_interp[256][8] = {
{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,1,0,0,0,1,0,0},{0,1,0,0,0,1,-1,0},
{1,2,0,0,-1,2,-1,0},{1,2,0,0,-1,2,-2,0},{1,3,0,-1,-1,3,-2,0},{2,3,0,-1,-2,3,-2,0},
{2,4,0,-1,-2,4,-3,0},{2,5,1,-1,-2,4,-3,0},{2,5,1,-1,-3,5,-4,0},{3,6,1,-1,-3,5,-4,0},
{3,6,1,-2,-3,6,-5,0},{3,7,1,-2,-4,6,-5,-1},{4,7,1,-2,-4,7,-5,-1},{4,8,1,-2,-4,7,-6,-1},
{4,9,1,-2,-5,8,-6,-1},{5,9,1,-2,-5,8,-7,-1},{5,10,2,-3,-5,9,-7,-1},{5,10,2,-3,-6,9,-7,-1},
{5,11,2,-3,-6,10,-8,-1},{6,11,2,-3,-6,10,-8,-1},{6,12,2,-3,-7,11,-9,-1},{6,13,2,-3,-7,11,-9,-1},
{7,13,2,-4,-7,12,-10,-1},{7,14,2,-4,-8,12,-10,-2},{7,14,2,-4,-8,13,-10,-2},{8,15,3,-4,-8,13,-11,-2},
{8,15,3,-4,-9,14,-11,-2},{8,16,3,-4,-9,14,-12,-2},{8,17,3,-5,-9,15,-12,-2},{9,17,3,-5,-10,15,-12,-2},
{9,18,3,-5,-10,16,-13,-2},{9,18,3,-5,-10,16,-13,-2},{10,19,3,-5,-11,17,-14,-2},{10,19,3,-5,-11,17,-14,-2},
{10,20,4,-6,-11,18,-15,-2},{11,20,4,-6,-12,18,-15,-3},{11,21,4,-6,-12,19,-15,-3},{11,22,4,-6,-12,19,-16,-3},
{11,22,4,-6,-13,20,-16,-3},{12,23,4,-6,-13,20,-17,-3},{12,23,4,-7,-13,21,-17,-3},{12,24,4,-7,-14,21,-18,-3},
{13,24,5,-7,-14,22,-18,-3},{13,25,5,-7,-14,22,-18,-3},{13,26,5,-7,-15,23,-19,-3},{14,26,5,-7,-15,23,-19,-3},
{14,27,5,-8,-15,24,-20,-3},{14,27,5,-8,-16,24,-20,-3},{14,28,5,-8,-16,25,-20,-4},{15,28,5,-8,-16,25,-21,-4},
{15,29,5,-8,-17,26,-21,-4},{15,30,6,-8,-17,26,-22,-4},{16,30,6,-9,-17,27,-22,-4},{16,31,6,-9,-18,27,-23,-4},
{16,31,6,-9,-18,28,-23,-4},{17,32,6,-9,-18,28,-23,-4},{17,32,6,-9,-19,29,-24,-4},{17,33,6,-9,-19,29,-24,-4},
{17,34,6,-10,-19,30,-25,-4},{18,34,6,-10,-20,30,-25,-4},{18,35,7,-10,-20,31,-25,-5},{18,35,7,-10,-20,31,-26,-5},
{19,36,7,-10,-21,32,-26,-5},{19,36,7,-10,-21,32,-27,-5},{19,37,7,-11,-21,33,-27,-5},{20,37,7,-11,-22,33,-28,-5},
{20,38,7,-11,-22,34,-28,-5},{20,39,7,-11,-22,34,-28,-5},{20,39,7,-11,-23,35,-29,-5},{21,40,8,-11,-23,35,-29,-5},
{21,40,8,-12,-23,36,-30,-5},{21,41,8,-12,-24,36,-30,-5},{22,41,8,-12,-24,37,-30,-6},{22,42,8,-12,-24,37,-31,-6},
{22,43,8,-12,-25,38,-31,-6},{23,43,8,-12,-25,38,-32,-6},{23,44,8,-13,-25,39,-32,-6},{23,44,9,-13,-26,39,-33,-6},
{23,45,9,-13,-26,40,-33,-6},{24,45,9,-13,-26,40,-33,-6},{24,46,9,-13,-27,41,-34,-6},{24,47,9,-14,-27,41,-34,-6},
{25,47,9,-14,-27,42,-35,-6},{25,48,9,-14,-28,42,-35,-6},{25,48,9,-14,-28,43,-36,-6},{26,49,9,-14,-28,43,-36,-7},
{26,49,10,-14,-29,44,-36,-7},{26,50,10,-15,-29,44,-37,-7},{26,51,10,-15,-29,45,-37,-7},{27,51,10,-15,-30,45,-38,-7},
{27,52,10,-15,-30,46,-38,-7},{27,52,10,-15,-30,46,-38,-7},{28,53,10,-15,-31,47,-39,-7},{28,53,10,-16,-31,47,-39,-7},
{28,54,10,-16,-31,48,-40,-7},{29,54,11,-16,-32,48,-40,-7},{29,55,11,-16,-32,49,-41,-7},{29,56,11,-16,-32,49,-41,-8},
{29,56,11,-16,-33,50,-41,-8},{30,57,11,-17,-33,50,-42,-8},{30,57,11,-17,-33,51,-42,-8},{30,58,11,-17,-34,51,-43,-8},
{31,58,11,-17,-34,52,-43,-8},{31,59,11,-17,-34,52,-43,-8},{31,60,12,-17,-35,53,-44,-8},{31,60,12,-18,-35,53,-44,-8},
{32,61,12,-18,-35,54,-45,-8},{32,61,12,-18,-36,54,-45,-8},{32,62,12,-18,-36,55,-46,-8},{33,62,12,-18,-36,55,-46,-9},
{33,63,12,-18,-37,56,-46,-9},{33,64,12,-19,-37,56,-47,-9},{34,64,12,-19,-37,57,-47,-9},{34,65,13,-19,-38,57,-48,-9},
{34,65,13,-19,-38,58,-48,-9},{34,66,13,-19,-38,58,-48,-9},{35,66,13,-19,-39,59,-49,-9},{35,67,13,-20,-39,59,-49,-9},
{35,68,13,-20,-39,60,-50,-9},{36,68,13,-20,-40,60,-50,-9},{36,69,13,-20,-40,61,-51,-9},{36,69,14,-20,-40,61,-51,-9},
{37,70,14,-20,-41,62,-51,-10},{37,70,14,-21,-41,62,-52,-10},{37,71,14,-21,-41,63,-52,-10},{37,72,14,-21,-42,63,-53,-10},
{38,72,14,-21,-42,64,-53,-10},{38,73,14,-21,-42,64,-54,-10},{38,73,14,-21,-43,65,-54,-10},{39,74,14,-22,-43,65,-54,-10},
{39,74,15,-22,-43,66,-55,-10},{39,75,15,-22,-44,66,-55,-10},{40,75,15,-22,-44,67,-56,-10},{40,76,15,-22,-44,67,-56,-10},
{40,77,15,-22,-45,68,-56,-11},{40,77,15,-23,-45,68,-57,-11},{41,78,15,-23,-45,69,-57,-11},{41,78,15,-23,-46,69,-58,-11},
{41,79,15,-23,-46,70,-58,-11},{42,79,16,-23,-46,70,-59,-11},{42,80,16,-23,-47,71,-59,-11},{42,81,16,-24,-47,71,-59,-11},
{43,81,16,-24,-47,72,-60,-11},{43,82,16,-24,-48,72,-60,-11},{43,82,16,-24,-48,73,-61,-11},{43,83,16,-24,-48,73,-61,-11},
{44,83,16,-24,-49,74,-61,-12},{44,84,16,-25,-49,74,-62,-12},{44,85,17,-25,-49,75,-62,-12},{45,85,17,-25,-50,75,-63,-12},
{45,86,17,-25,-50,76,-63,-12},{45,86,17,-25,-50,76,-64,-12},{46,87,17,-25,-51,77,-64,-12},{46,87,17,-26,-51,77,-64,-12},
{46,88,17,-26,-51,78,-65,-12},{46,89,17,-26,-52,78,-65,-12},{47,89,18,-26,-52,79,-66,-12},{47,90,18,-26,-52,79,-66,-12},
{47,90,18,-26,-53,80,-66,-13},{48,91,18,-27,-53,80,-67,-13},{48,91,18,-27,-53,81,-67,-13},{48,92,18,-27,-54,81,-68,-13},
{49,92,18,-27,-54,82,-68,-13},{49,93,18,-27,-54,82,-69,-13},{49,94,18,-28,-54,83,-69,-13},{49,94,19,-28,-55,83,-69,-13},
{50,95,19,-28,-55,84,-70,-13},{50,95,19,-28,-55,84,-70,-13},{50,96,19,-28,-56,85,-71,-13},{51,96,19,-28,-56,85,-71,-13},
{51,97,19,-29,-56,86,-72,-13},{51,98,19,-29,-57,86,-72,-14},{52,98,19,-29,-57,87,-72,-14},{52,99,19,-29,-57,87,-73,-14},
{52,99,20,-29,-58,88,-73,-14},{52,100,20,-29,-58,88,-74,-14},{53,100,20,-30,-58,89,-74,-14},{53,101,20,-30,-59,89,-74,-14},
{53,102,20,-30,-59,90,-75,-14},{54,102,20,-30,-59,90,-75,-14},{54,103,20,-30,-60,91,-76,-14},{54,103,20,-30,-60,91,-76,-14},
{55,104,20,-31,-60,92,-77,-14},{55,104,21,-31,-61,92,-77,-15},{55,105,21,-31,-61,93,-77,-15},{55,106,21,-31,-61,93,-78,-15},
{56,106,21,-31,-62,94,-78,-15},{56,107,21,-31,-62,94,-79,-15},{56,107,21,-32,-62,95,-79,-15},{57,108,21,-32,-63,95,-79,-15},
{57,108,21,-32,-63,96,-80,-15},{57,109,22,-32,-63,96,-80,-15},{58,109,22,-32,-64,97,-81,-15},{58,110,22,-32,-64,97,-81,-15},
{58,111,22,-33,-64,98,-82,-15},{58,111,22,-33,-65,98,-82,-16},{59,112,22,-33,-65,99,-82,-16},{59,112,22,-33,-65,99,-83,-16},
{59,113,22,-33,-66,100,-83,-16},{60,113,22,-33,-66,100,-84,-16},{60,114,23,-34,-66,101,-84,-16},{60,115,23,-34,-67,101,-84,-16},
{60,115,23,-34,-67,102,-85,-16},{61,116,23,-34,-67,102,-85,-16},{61,116,23,-34,-68,103,-86,-16},{61,117,23,-34,-68,103,-86,-16},
{62,117,23,-35,-68,104,-87,-16},{62,118,23,-35,-69,104,-87,-16},{62,119,23,-35,-69,105,-87,-17},{63,119,24,-35,-69,105,-88,-17},
{63,120,24,-35,-70,106,-88,-17},{63,120,24,-35,-70,106,-89,-17},{63,121,24,-36,-70,107,-89,-17},{64,121,24,-36,-71,107,-90,-17},
{64,122,24,-36,-71,108,-90,-17},{64,123,24,-36,-71,108,-90,-17},{65,123,24,-36,-72,109,-91,-17},{65,124,24,-36,-72,109,-91,-17},
{65,124,25,-37,-72,110,-92,-17},{66,125,25,-37,-73,110,-92,-17},{66,125,25,-37,-73,111,-92,-18},{66,126,25,-37,-73,111,-93,-18},
{66,127,25,-37,-74,112,-93,-18},{67,127,25,-37,-74,112,-94,-18},{67,128,25,-38,-74,113,-94,-18},{67,128,25,-38,-75,113,-95,-18},
{68,129,25,-38,-75,114,-95,-18},{68,129,26,-38,-75,114,-95,-18},{68,130,26,-38,-76,115,-96,-18},{69,130,26,-38,-76,115,-96,-18},
{69,131,26,-39,-76,116,-97,-18},{69,132,26,-39,-77,116,-97,-18},{69,132,26,-39,-77,117,-97,-19},{70,133,26,-39,-77,117,-98,-19},
{70,133,26,-39,-78,118,-98,-19},{70,134,27,-39,-78,118,-99,-19},{71,134,27,-40,-78,119,-99,-19},{71,135,27,-40,-79,119,-100,-19},
{71,136,27,-40,-79,120,-100,-19},{72,136,27,-40,-79,120,-100,-19},{72,137,27,-40,-80,121,-101,-19},{72,137,27,-40,-80,121,-101,-19},
{72,138,27,-41,-80,122,-102,-19},{73,138,27,-41,-81,122,-102,-19},{73,139,28,-41,-81,123,-103,-19},{73,140,28,-41,-81,123,-103,-20},
{74,140,28,-41,-82,124,-103,-20},{74,141,28,-42,-82,124,-104,-20},{74,141,28,-42,-82,125,-104,-20},{75,142,28,-42,-83,125,-105,-20},
{75,142,28,-42,-83,126,-105,-20},{75,143,28,-42,-83,126,-105,-20},{75,144,28,-42,-84,127,-106,-20},{76,144,29,-43,-84,127,-106,-20}};
/* }}} */

/* {{{ [fold] qc_yuv_toplanar(unsigned char *rgb24frame, int length, int format) */
/*
 * Convert to planar formats
 * The input is an YCbYCr format.
 * Input: len : 2/3 maxi.
 *  | YUYV          | free |
 *  | 2/3           | 1/3  |
 * 1th conversion:
 *  | YY  | free    | U|V  |
 *  | 1/3 | 1/3     | 1/3  |
 * 2d conversion:
 *  | YY  | U  | V  | free |
 *  | 1/3 | 1/6|1/6 | 1/3  |
 * That the Y422P conversion.
 */
static int qc_yuv_toplanar(unsigned char *rgb24frame, int length, int format)
{
	unsigned char *ptr;
	int n = 0, l = 0, i;
	unsigned char *cr;
	unsigned char *cb;
	unsigned char *crptr, *cbptr;
	int mode = 0;
	
	l = length/2;
	switch(format) {
		case VIDEO_PALETTE_YUV411P:
			n = length/8;
			mode = 1;
			break;
		case VIDEO_PALETTE_YUV422P:
			n = length/4;
			break;
	}

	ptr = rgb24frame;
	crptr = &rgb24frame[length];
	cr = crptr;
	cbptr = &rgb24frame[length+n];
	cb = cbptr;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_yuv_toplanar: %d (%d + 2 * %d)",length, l, n);

	/* separate Y , U and V */
	for (i=0; i<length; i+=2) {
		*ptr++ = rgb24frame[i];
		if (mode) {
			if ((i/2)%4==3) {
				*cbptr++ = rgb24frame[i+1];
				*crptr++ = rgb24frame[i-1];
			}
		}
		else {
			if ((i/2)%2)
				*cbptr++ = rgb24frame[i+1];
			else
				*crptr++ = rgb24frame[i+1];
		}
	}

	/* copy the UV plans after the Y */
	memcpy(&rgb24frame[l], cr, n);
	memcpy(&rgb24frame[l+n], cb, n);
	// That is useless! frame->scanlength = length/2+ (2 * n);
	return length;
}
/* }}} */
/* {{{ [fold] qc_yuv_rgb2yuv(unsigned char *rgb24frame, int length, int format) */
/*
 * Convert the RGB24 image to YUV422.
 * return the size of the converted frame
 */
int qc_yuv_rgb2yuv(unsigned char *rgb24frame, int length, int format)
{
	int i;
	int l;			/* Data length */
	short Y;
	short U;
	short V;
	unsigned char *ptr;
	unsigned short w;

	//if (qcdebug&QC_DEBUGLOGIC)PDEBUG("qc_yuv_rgb2yuv(frame=%p,length=%i,palette=%s)",rgb24frame,length,qc_fmt_getname(format));
	ptr = rgb24frame;
	l = 0;

	if (format==VIDEO_PALETTE_RGB32) {
		/* we need to convert in reverse so as to not overwrite ourselves */
		for (ptr=ptr+length*4/3,i=length-3; i>=0; i-=3) {
			*--ptr = 0;
			*--ptr = (unsigned char) rgb24frame[i + 2];
			*--ptr = (unsigned char) rgb24frame[i + 1];
			*--ptr = (unsigned char) rgb24frame[i + 0];
			l += 4;
		}
	} else
	for (i=0; i<length; i+=3) {
		Y = qc_yuv_interp[rgb24frame[i]][0] + qc_yuv_interp[rgb24frame[i+1]][1] + qc_yuv_interp[rgb24frame[i+2]][2];
		U = qc_yuv_interp[rgb24frame[i]][3] + qc_yuv_interp[rgb24frame[i+1]][4] + qc_yuv_interp[rgb24frame[i+2]][5];
		V = qc_yuv_interp[rgb24frame[i]][5] + qc_yuv_interp[rgb24frame[i+1]][6] + qc_yuv_interp[rgb24frame[i+2]][7];

		/* color/brightness could be arranged here */

		Y = CLIP(Y, 0,255);
		U = CLIP(U, -127,127);
		V = CLIP(V, -127,127);

		switch(format) {
		case VIDEO_PALETTE_GREY:
			*ptr++ = (219 * Y)/255 + 16;
			l++;
			break;
		case VIDEO_PALETTE_YUV411:
			// Not yet supported.
			break;
		case VIDEO_PALETTE_YUV411P:
		case VIDEO_PALETTE_YUV422:
		case VIDEO_PALETTE_YUV422P:
			*ptr++ = (219 * Y)/255 + 16;
			l++;
			if ((i/3)%2)
				*ptr = (112 * U)/127 + 128; /* Cb */
			else
				*ptr = (112 * V)/127 + 128; /* Cr */
			ptr++;
			l++;
			break;
		case VIDEO_PALETTE_RGB565:
			/* take top five bits and pack while switch RGB with BGR */
			/* FIXME: do we need to take into account of non-x86 byte ordering? */
			w = ((unsigned short)
				((unsigned short) (rgb24frame[i + 2] & 0xF8) << (11 - 3)) |
				((unsigned short) (rgb24frame[i + 1] & 0xFC) << (5 - 2)) |
				((unsigned short) (rgb24frame[i + 0] >> 3)));
			*ptr++ = (unsigned char) (w & 0xFF);
			*ptr++ = (unsigned char) (w >> 8);
			l += 2;
			break;
		case VIDEO_PALETTE_RGB555:
			w = ((unsigned short)
				((unsigned short) (rgb24frame[i + 2] & 0xf8) << (10 - 3)) |
				((unsigned short) (rgb24frame[i + 1] & 0xf8) << (5 - 3)) |
				((unsigned short) (rgb24frame[i + 0] >> 3)));
			*ptr++ = (unsigned char) (w & 0xFF);
			*ptr++ = (unsigned char) (w >> 8);
			l += 2;
			break;
		default:
			*ptr++ = (219 * Y)/255 + 16;
			l++;
			*ptr = (112 * U)/127 + 128; /* Cb */
			ptr++;
			l++;
			*ptr = (112 * V)/127 + 128; /* Cr */
			ptr++;
			l++;
		}
	}
	if (format >= VIDEO_PALETTE_PLANAR) {
		length = qc_yuv_toplanar(rgb24frame, l, format);
	} else {
		length = l;
	}
	return length;
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_imag: Start of image processing functions ************************** */

/* {{{ [fold] qc_imag_bayer_midvalue(char *bay, int bay_line, int columns, int rows) */
/* Compute and return the average pixel intensity from the bayer data. 
 * The result can be used for automatic software exposure/gain control.
 * bay = points to the bayer image data (upper left pixel is green)
 * bay_line = bytes between the beginnings of two consecutive rows
 * columns, rows = bayer image size (both must be even)
 */
static unsigned char qc_imag_bayer_midvalue(unsigned char *bay, int bay_line, 
	unsigned int columns, unsigned int rows)
{
	static const unsigned int stepsize = 8;		/* Larger = faster and less accurate */
	unsigned char *cur_bay;
	unsigned int sum = 0;
	int column_cnt;
	int row_cnt;

	/* Skip 1/4 of left, right, top, and bottom pixels (use only center pixels) */
	columns /= 2;
	rows    /= 2;
	bay     += rows/4*2*bay_line + columns/4*2;

	columns /= stepsize*2;
	rows    /= stepsize*2;

	row_cnt = rows;
	do {
		column_cnt = columns;
		cur_bay = bay;
		do {
			sum += cur_bay[0] + cur_bay[1] + cur_bay[bay_line];	/* R + G + B */
			cur_bay += 2*stepsize;
		} while (--column_cnt > 0);
		bay += 2*stepsize*bay_line;
	} while (--row_cnt > 0);
	sum /= 3 * columns * rows;
	return sum;
}
/* }}} */
#if COMPRESS
/* {{{ [fold] qc_imag_rgb24_midvalue(char *rgb, int rgb_line, int columns, int rows) */
/* Compute and return the average pixel intensity from the RGB/BGR24 data. 
 * The result can be used for automatic software exposure/gain control.
 * rgb = points to the RGB image data
 * rgb_line = bytes between the beginnings of two consecutive rows
 * columns, rows = RGB image size
 */
static unsigned char qc_imag_rgb24_midvalue(unsigned char *rgb, int rgb_line, 
	unsigned int columns, unsigned int rows)
{
	static const unsigned int stepsize = 8;		/* Larger = faster and less accurate */
	unsigned char *cur_rgb;
	unsigned int sum = 0;
	int column_cnt;
	int row_cnt;

	/* Skip 1/4 of left, right, top, and bottom pixels (use only center pixels) */
	columns /= 2;
	rows    /= 2;
	rgb     += rows/2*rgb_line + columns/2*3;

	columns /= stepsize;
	rows    /= stepsize;

	row_cnt = rows;
	do {
		column_cnt = columns;
		cur_rgb = rgb;
		do {
			sum += cur_rgb[0] + cur_rgb[1] + cur_rgb[2];		/* R + G + B */
			cur_rgb += 3*stepsize;
		} while (--column_cnt > 0);
		rgb += stepsize*rgb_line;
	} while (--row_cnt > 0);
	sum /= 3 * columns * rows;
	return sum;
}
/* }}} */
#endif
/* {{{ [fold] qc_imag_userlut(unsigned char (*userlut)[QC_LUT_SIZE], unsigned char (*lut)[QC_LUT_SIZE]) */
/* Apply user-defined lookup-table to the given preinitialized lut */
static inline void qc_imag_userlut(unsigned char (*userlut)[QC_LUT_SIZE], unsigned char (*lut)[QC_LUT_SIZE])
{
	unsigned int i,p;
	for (i=0; i<256; i++) {
		p = (*lut)[QC_LUT_RED + i];
		p = (*userlut)[QC_LUT_RED + p];
		(*lut)[QC_LUT_RED + i] = p;
	}
	for (i=0; i<256; i++) {
		p = (*lut)[QC_LUT_GREEN + i];
		p = (*userlut)[QC_LUT_GREEN + p];
		(*lut)[QC_LUT_GREEN + i] = p;
	}
	for (i=0; i<256; i++) {
		p = (*lut)[QC_LUT_BLUE + i];
		p = (*userlut)[QC_LUT_BLUE + p];
		(*lut)[QC_LUT_BLUE + i] = p;
	}
}
/* }}} */
/* {{{ [fold] qc_imag_bayer_equalize(char *bay, int bay_line, int columns, int rows, char (*lut)[]) */
/* Compute and fill a lookup table which will equalize the image. 
 * bay = points to the bayer image data (upper left pixel is green)
 * bay_line = bytes between the beginnings of two consecutive rows
 * columns, rows = bayer image size (both must be even)
 * lut = lookup table to be filled, 3*256 char array
 */
static void qc_imag_bayer_equalize(unsigned char *bay, int bay_line, 
	unsigned int columns, unsigned int rows, unsigned char (*lut)[QC_LUT_SIZE])
{
	static const unsigned int stepsize = 4;		/* Larger = faster and less accurate */
	unsigned short red_cnt[256], green_cnt[256], blue_cnt[256];	/* FIXME: how much we can use stack? */
	unsigned int red_sum, green_sum, blue_sum;
	unsigned int red_tot, green_tot, blue_tot;
	unsigned char *cur_bay;
	int i, column_cnt, row_cnt;
	
	memset(red_cnt,   0, sizeof(red_cnt));
	memset(green_cnt, 0, sizeof(green_cnt));
	memset(blue_cnt,  0, sizeof(blue_cnt));
	
	columns /= 2*stepsize;
	rows    /= 2*stepsize;

	/* Compute histogram */
	row_cnt = rows;
	do {
		column_cnt = columns;
		cur_bay = bay;
		do {
			green_cnt[cur_bay[0]]++;
			red_cnt  [cur_bay[1]]++;
			blue_cnt [cur_bay[bay_line]]++;
			green_cnt[cur_bay[bay_line+1]]++;
			cur_bay += 2*stepsize;
		} while (--column_cnt > 0);
		bay += 2*stepsize*bay_line;
	} while (--row_cnt > 0);

	/* Compute lookup table based on the histogram */
	red_tot   = columns * rows;		/* Total number of pixels of each primary color */
	green_tot = red_tot * 2;
	blue_tot  = red_tot;
	red_sum   = 0;
	green_sum = 0;
	blue_sum  = 0;
	for (i=0; i<256; i++) {
		(*lut)[QC_LUT_RED   + i] = 255 * red_sum   / red_tot;
		(*lut)[QC_LUT_GREEN + i] = 255 * green_sum / green_tot;
		(*lut)[QC_LUT_BLUE  + i] = 255 * blue_sum  / blue_tot;
		red_sum   += red_cnt[i];
		green_sum += green_cnt[i];
		blue_sum  += blue_cnt[i];
	}
}
/* }}} */
/* {{{ [fold] qc_imag_bayer_lut(char *bay, int bay_line, int columns, int rows, char (*lut)[]) */
/* Transform pixel values in a bayer image with a given lookup table.
 * bay = points to the bayer image data (upper left pixel is green)
 * bay_line = bytes between the beginnings of two consecutive rows
 * columns, rows = bayer image size (both must be even)
 * lut = lookup table to be used, 3*256 char array
 */
static void qc_imag_bayer_lut(unsigned char *bay, int bay_line, 
	unsigned int columns, unsigned int rows, unsigned char (*lut)[QC_LUT_SIZE])
{
	unsigned char *cur_bay;
	unsigned int total_columns;
	
	total_columns = columns / 2;	/* Number of 2x2 bayer blocks */
	rows /= 2;
	do {
		columns = total_columns;
		cur_bay = bay;
		do {
			cur_bay[0] = (*lut)[QC_LUT_GREEN + cur_bay[0]];
			cur_bay[1] = (*lut)[QC_LUT_RED   + cur_bay[1]];
			cur_bay += 2;
		} while (--columns);
		bay += bay_line;
		columns = total_columns;
		cur_bay = bay;
		do {
			cur_bay[0] = (*lut)[QC_LUT_BLUE  + cur_bay[0]];
			cur_bay[1] = (*lut)[QC_LUT_GREEN + cur_bay[1]];
			cur_bay += 2;
		} while (--columns);
		bay += bay_line;
	} while (--rows);
}
/* }}} */
/* {{{ [fold] qc_imag_writergb(void *addr, int bpp, unsigned char r, unsigned char g, unsigned char b) */
/* Write RGB pixel value to the given address.
 * addr = memory address, to which the pixel is written
 * bpp = number of bytes in the pixel (should be 3 or 4)
 * r, g, b = pixel component values to be written (red, green, blue)
 * Looks horribly slow but the compiler should be able to inline optimize it.
 */
static inline void qc_imag_writergb(void *addr, int bpp,
	unsigned char r, unsigned char g, unsigned char b)
{
	if (DEFAULT_BGR) {
		/* Blue is first (in the lowest memory address */
		if (bpp==4) {
#if defined(__LITTLE_ENDIAN)
			*(unsigned int *)addr =
				(unsigned int)r << 16 |
				(unsigned int)g << 8 |
				(unsigned int)b;
#elif defined(__BIG_ENDIAN)
			*(unsigned int *)addr =
				(unsigned int)r << 8 |
				(unsigned int)g << 16 |
				(unsigned int)b << 24;
#else
			unsigned char *addr2 = (unsigned char *)addr;
			addr2[0] = b;
			addr2[1] = g;
			addr2[2] = r;
			addr2[3] = 0;
#endif
		} else {
			unsigned char *addr2 = (unsigned char *)addr;
			addr2[0] = b;
			addr2[1] = g;
			addr2[2] = r;
		}
	} else {
		/* Red is first (in the lowest memory address */
		if (bpp==4) {
#if defined(__LITTLE_ENDIAN)
			*(unsigned int *)addr =
				(unsigned int)b << 16 |
				(unsigned int)g << 8 |
				(unsigned int)r;
#elif defined(__BIG_ENDIAN)
			*(unsigned int *)addr =
				(unsigned int)b << 8 |
				(unsigned int)g << 16 |
				(unsigned int)r << 24;
#else
			unsigned char *addr2 = (unsigned char *)addr;
			addr2[0] = r;
			addr2[1] = g;
			addr2[2] = b;
			addr2[3] = 0;
#endif
		} else {
			unsigned char *addr2 = (unsigned char *)addr;
			addr2[0] = r;
			addr2[1] = g;
			addr2[2] = b;
		}
	}
}
/* }}} */

/*
 * Raw image data for Bayer-RGB-matrix:
 * G R for even rows
 * B G for odd rows
 */

#if 0
/* {{{ [fold] qc_imag_bay2rgb_noip(char *bay, int bay_line, char *rgb, int rgb_line, int columns, rows, bpp) */
/* Convert bayer image to RGB image without using interpolation.
 * bay = points to the bayer image data (upper left pixel is green)
 * bay_line = bytes between the beginnings of two consecutive rows
 * rgb = points to the rgb image data that is written
 * rgb_line = bytes between the beginnings of two consecutive rows
 * columns, rows = bayer image size (both must be even)
 * bpp = number of bytes in each pixel in the RGB image (should be 3 or 4)
 */
/* Execution time: 2391747-2653574 clock cycles for CIF image (Pentium II) */
/* Do NOT use this routine: cottnoip is faster with better image quality */
static inline void qc_imag_bay2rgb_noip(unsigned char *bay, int bay_line,
		unsigned char *rgb, int rgb_line,
		int columns, int rows, int bpp)
{
	unsigned char *cur_bay, *cur_rgb;
	int bay_line2, rgb_line2;
	int total_columns;

	/* Process 2 lines and rows per each iteration */
	total_columns = columns >> 1;
	rows >>= 1;
	bay_line2 = 2*bay_line;
	rgb_line2 = 2*rgb_line;
	do {
		cur_bay = bay;
		cur_rgb = rgb;
		columns = total_columns;
		do {
			qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
			qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
			qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
			qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
			cur_bay += 2;
			cur_rgb += 2*bpp;
		} while (--columns);
		bay += bay_line2;
		rgb += rgb_line2;
	} while (--rows);
}		  
/* }}} */
#endif
/* {{{ [fold] qc_imag_bay2rgb_horip(char *bay, int bay_line, char *rgb, int rgb_line, columns, rows, bpp) */
/* Convert bayer image to RGB image using fast horizontal-only interpolation.
 * bay = points to the bayer image data (upper left pixel is green)
 * bay_line = bytes between the beginnings of two consecutive rows
 * rgb = points to the rgb image data that is written
 * rgb_line = bytes between the beginnings of two consecutive rows
 * columns, rows = bayer image size (both must be even)
 * bpp = number of bytes in each pixel in the RGB image (should be 3 or 4)
 */
/* Execution time: 2735776-3095322 clock cycles for CIF image (Pentium II) */
/* Not recommended: ip seems to be somewhat faster, probably with better image quality.
 * cott is quite much faster, but possibly with slightly worse image quality */
static inline void qc_imag_bay2rgb_horip(unsigned char *bay, int bay_line,
		unsigned char *rgb, int rgb_line,
		unsigned int columns, unsigned int rows, int bpp) 
{
	unsigned char *cur_bay, *cur_rgb;
	int bay_line2, rgb_line2;
	int total_columns;
	unsigned char red, green, blue;
	unsigned int column_cnt, row_cnt;

	/* Process 2 lines and rows per each iteration */
	total_columns = (columns-2) / 2;
	row_cnt = rows / 2;
	bay_line2 = 2*bay_line;
	rgb_line2 = 2*rgb_line;
	
	do {
		qc_imag_writergb(rgb+0,        bpp, bay[1], bay[0], bay[bay_line]);
		qc_imag_writergb(rgb+rgb_line, bpp, bay[1], bay[0], bay[bay_line]);
		cur_bay = bay + 1;
		cur_rgb = rgb + bpp;
		column_cnt = total_columns;
		do {
			green = ((unsigned int)cur_bay[-1]+cur_bay[1]) / 2;
			blue  = ((unsigned int)cur_bay[bay_line-1]+cur_bay[bay_line+1]) / 2;
			qc_imag_writergb(cur_rgb+0, bpp, cur_bay[0], green, blue);
			red   = ((unsigned int)cur_bay[0]+cur_bay[2]) / 2;
			qc_imag_writergb(cur_rgb+bpp, bpp, red, cur_bay[1], cur_bay[bay_line+1]);
			green = ((unsigned int)cur_bay[bay_line]+cur_bay[bay_line+2]) / 2;
			qc_imag_writergb(cur_rgb+rgb_line, bpp, cur_bay[0], cur_bay[bay_line], blue);
			qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, red, cur_bay[1], cur_bay[bay_line+1]);
			cur_bay += 2;
			cur_rgb += 2*bpp;
		} while (--column_cnt);
		qc_imag_writergb(cur_rgb+0,        bpp, cur_bay[0], cur_bay[-1],       cur_bay[bay_line-1]);
		qc_imag_writergb(cur_rgb+rgb_line, bpp, cur_bay[0], cur_bay[bay_line], cur_bay[bay_line-1]);
		bay += bay_line2;
		rgb += rgb_line2;
	} while (--row_cnt);
}		  
/* }}} */
/* {{{ [fold] qc_imag_bay2rgb_ip(char *bay, int bay_line, char *rgb, int rgb_line, columns, rows, bpp) */
/* Convert bayer image to RGB image using full (slow) linear interpolation.
 * bay = points to the bayer image data (upper left pixel is green)
 * bay_line = bytes between the beginnings of two consecutive rows
 * rgb = points to the rgb image data that is written
 * rgb_line = bytes between the beginnings of two consecutive rows
 * columns, rows = bayer image size (both must be even)
 * bpp = number of bytes in each pixel in the RGB image (should be 3 or 4)
 */
/* Execution time: 2714077-2827455 clock cycles for CIF image (Pentium II) */
static inline void qc_imag_bay2rgb_ip(unsigned char *bay, int bay_line,
		unsigned char *rgb, int rgb_line,
		unsigned int columns, unsigned int rows, int bpp) 
{
	unsigned char *cur_bay, *cur_rgb;
	int bay_line2, rgb_line2;
	int total_columns;
	unsigned char red, green, blue;
	unsigned int column_cnt, row_cnt;

	/* Process 2 rows and columns each iteration */
	total_columns = (columns-2) / 2;
	row_cnt = (rows-2) / 2;
	bay_line2 = 2*bay_line;
	rgb_line2 = 2*rgb_line;

	/* First scanline is handled here as a special case */	
	qc_imag_writergb(rgb, bpp, bay[1], bay[0], bay[bay_line]);
	cur_bay = bay + 1;
	cur_rgb = rgb + bpp;
	column_cnt = total_columns;
	do {
		green  = ((unsigned int)cur_bay[-1] + cur_bay[1] + cur_bay[bay_line]) / 3;
		blue   = ((unsigned int)cur_bay[bay_line-1] + cur_bay[bay_line+1]) / 2;
		qc_imag_writergb(cur_rgb, bpp, cur_bay[0], green, blue);
		red    = ((unsigned int)cur_bay[0] + cur_bay[2]) / 2;
		qc_imag_writergb(cur_rgb+bpp, bpp, red, cur_bay[1], cur_bay[bay_line+1]);
		cur_bay += 2;
		cur_rgb += 2*bpp;
	} while (--column_cnt);
	green = ((unsigned int)cur_bay[-1] + cur_bay[bay_line]) / 2;
	qc_imag_writergb(cur_rgb, bpp, cur_bay[0], green, cur_bay[bay_line-1]);

	/* Process here all other scanlines except first and last */
	bay += bay_line;
	rgb += rgb_line;
	do {
		red = ((unsigned int)bay[-bay_line+1] + bay[bay_line+1]) / 2;
		green = ((unsigned int)bay[-bay_line] + bay[1] + bay[bay_line]) / 3;
		qc_imag_writergb(rgb+0, bpp, red, green, bay[0]);
		blue = ((unsigned int)bay[0] + bay[bay_line2]) / 2;
		qc_imag_writergb(rgb+rgb_line, bpp, bay[bay_line+1], bay[bay_line], blue);
		cur_bay = bay + 1;
		cur_rgb = rgb + bpp;
		column_cnt = total_columns;
		do {
			red   = ((unsigned int)cur_bay[-bay_line]+cur_bay[bay_line]) / 2;
			blue  = ((unsigned int)cur_bay[-1]+cur_bay[1]) / 2;
			qc_imag_writergb(cur_rgb+0, bpp, red, cur_bay[0], blue);
			red   = ((unsigned int)cur_bay[-bay_line]+cur_bay[-bay_line+2]+cur_bay[bay_line]+cur_bay[bay_line+2]) / 4;
			green = ((unsigned int)cur_bay[0]+cur_bay[2]+cur_bay[-bay_line+1]+cur_bay[bay_line+1]) / 4;
			qc_imag_writergb(cur_rgb+bpp, bpp, red, green, cur_bay[1]);
			green = ((unsigned int)cur_bay[0]+cur_bay[bay_line2]+cur_bay[bay_line-1]+cur_bay[bay_line+1]) / 4;
			blue  = ((unsigned int)cur_bay[-1]+cur_bay[1]+cur_bay[bay_line2-1]+cur_bay[bay_line2+1]) / 4;
			qc_imag_writergb(cur_rgb+rgb_line, bpp, cur_bay[bay_line], green, blue);
			red   = ((unsigned int)cur_bay[bay_line]+cur_bay[bay_line+2]) / 2;
			blue  = ((unsigned int)cur_bay[1]+cur_bay[bay_line2+1]) / 2;
			qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, red, cur_bay[bay_line+1], blue);
			cur_bay += 2;
			cur_rgb += 2*bpp;
		} while (--column_cnt);
		red = ((unsigned int)cur_bay[-bay_line] + cur_bay[bay_line]) / 2;
		qc_imag_writergb(cur_rgb, bpp, red, cur_bay[0], cur_bay[-1]);
		green = ((unsigned int)cur_bay[0] + cur_bay[bay_line-1] + cur_bay[bay_line2]) / 3;
		blue = ((unsigned int)cur_bay[-1] + cur_bay[bay_line2-1]) / 2;
		qc_imag_writergb(cur_rgb+rgb_line, bpp, cur_bay[bay_line], green, blue);
		bay += bay_line2;
		rgb += rgb_line2;
	} while (--row_cnt);

	/* Last scanline is handled here as a special case */	
	green = ((unsigned int)bay[-bay_line] + bay[1]) / 2;
	qc_imag_writergb(rgb, bpp, bay[-bay_line+1], green, bay[0]);
	cur_bay = bay + 1;
	cur_rgb = rgb + bpp;
	column_cnt = total_columns;
	do {
		blue   = ((unsigned int)cur_bay[-1] + cur_bay[1]) / 2;
		qc_imag_writergb(cur_rgb, bpp, cur_bay[-bay_line], cur_bay[0], blue);
		red    = ((unsigned int)cur_bay[-bay_line] + cur_bay[-bay_line+2]) / 2;
		green  = ((unsigned int)cur_bay[0] + cur_bay[-bay_line+1] + cur_bay[2]) / 3;
		qc_imag_writergb(cur_rgb+bpp, bpp, red, green, cur_bay[1]);
		cur_bay += 2;
		cur_rgb += 2*bpp;
	} while (--column_cnt);
	qc_imag_writergb(cur_rgb, bpp, cur_bay[-bay_line], cur_bay[0], cur_bay[-1]);
}
/* }}} */
/* {{{ [fold] qc_imag_bay2rgb_cott(unsigned char *bay, int bay_line, unsigned char *rgb, int rgb_line, int columns, int rows, int bpp) */
/* Convert bayer image to RGB image using 0.5 displaced light linear interpolation.
 * bay = points to the bayer image data (upper left pixel is green)
 * bay_line = bytes between the beginnings of two consecutive rows
 * rgb = points to the rgb image data that is written
 * rgb_line = bytes between the beginnings of two consecutive rows
 * columns, rows = bayer image size (both must be even)
 * bpp = number of bytes in each pixel in the RGB image (should be 3 or 4)
 */
/* Execution time: 2167685 clock cycles for CIF image (Pentium II) */
/* Original idea for this routine from Cagdas Ogut */
static inline void qc_imag_bay2rgb_cott(unsigned char *bay, int bay_line,
		unsigned char *rgb, int rgb_line,
		int columns, int rows, int bpp)
{
	unsigned char *cur_bay, *cur_rgb;
	int bay_line2, rgb_line2;
	int total_columns;

	/* Process 2 lines and rows per each iteration, but process the last row and column separately */
	total_columns = (columns>>1) - 1;
	rows = (rows>>1) - 1;
	bay_line2 = 2*bay_line;
	rgb_line2 = 2*rgb_line;
	do {
		cur_bay = bay;
		cur_rgb = rgb;
		columns = total_columns;
		do {
			qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1],           ((unsigned int)cur_bay[0] + cur_bay[bay_line+1])          /2, cur_bay[bay_line]);
			qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1],           ((unsigned int)cur_bay[2] + cur_bay[bay_line+1])          /2, cur_bay[bay_line+2]);
			qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[bay_line2+1], ((unsigned int)cur_bay[bay_line2] + cur_bay[bay_line+1])  /2, cur_bay[bay_line]);
			qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[bay_line2+1], ((unsigned int)cur_bay[bay_line2+2] + cur_bay[bay_line+1])/2, cur_bay[bay_line+2]);
			cur_bay += 2;
			cur_rgb += 2*bpp;
		} while (--columns);
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], ((unsigned int)cur_bay[0] + cur_bay[bay_line+1])/2, cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[bay_line2+1], ((unsigned int)cur_bay[bay_line2] + cur_bay[bay_line+1])/2, cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[bay_line2+1], cur_bay[bay_line+1], cur_bay[bay_line]);
		bay += bay_line2;
		rgb += rgb_line2;
	} while (--rows);
	/* Last scanline handled here as special case */
	cur_bay = bay;
	cur_rgb = rgb;
	columns = total_columns;
	do {
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], ((unsigned int)cur_bay[0] + cur_bay[bay_line+1])/2, cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], ((unsigned int)cur_bay[2] + cur_bay[bay_line+1])/2, cur_bay[bay_line+2]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line+2]);
		cur_bay += 2;
		cur_rgb += 2*bpp;
	} while (--columns);
	/* Last lower-right pixel is handled here as special case */
	qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], ((unsigned int)cur_bay[0] + cur_bay[bay_line+1])/2, cur_bay[bay_line]);
	qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
	qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
	qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
}
/* }}} */
/* {{{ [fold] qc_imag_bay2rgb_cottnoip(unsigned char *bay, int bay_line, unsigned char *rgb, int rgb_line, int columns, int rows, int bpp) */
/* Convert bayer image to RGB image using 0.5 displaced nearest neighbor.
 * bay = points to the bayer image data (upper left pixel is green)
 * bay_line = bytes between the beginnings of two consecutive rows
 * rgb = points to the rgb image data that is written
 * rgb_line = bytes between the beginnings of two consecutive rows
 * columns, rows = bayer image size (both must be even)
 * bpp = number of bytes in each pixel in the RGB image (should be 3 or 4)
 */
/* Execution time: 2133302 clock cycles for CIF image (Pentium II), fastest */
static inline void qc_imag_bay2rgb_cottnoip(unsigned char *bay, int bay_line,
		unsigned char *rgb, int rgb_line,
		int columns, int rows, int bpp)
{
	unsigned char *cur_bay, *cur_rgb;
	int bay_line2, rgb_line2;
	int total_columns;

	/* Process 2 lines and rows per each iteration, but process the last row and column separately */
	total_columns = (columns>>1) - 1;
	rows = (rows>>1) - 1;
	bay_line2 = 2*bay_line;
	rgb_line2 = 2*rgb_line;
	do {
		cur_bay = bay;
		cur_rgb = rgb;
		columns = total_columns;
		do {
			qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1],           cur_bay[0], cur_bay[bay_line]);
			qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1],           cur_bay[2], cur_bay[bay_line+2]);
			qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[bay_line2+1], cur_bay[bay_line+1], cur_bay[bay_line]);
			qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[bay_line2+1], cur_bay[bay_line+1], cur_bay[bay_line+2]);
			cur_bay += 2;
			cur_rgb += 2*bpp;
		} while (--columns);
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[bay_line2+1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[bay_line2+1], cur_bay[bay_line+1], cur_bay[bay_line]);
		bay += bay_line2;
		rgb += rgb_line2;
	} while (--rows);
	/* Last scanline handled here as special case */
	cur_bay = bay;
	cur_rgb = rgb;
	columns = total_columns;
	do {
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[2], cur_bay[bay_line+2]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line+2]);
		cur_bay += 2;
		cur_rgb += 2*bpp;
	} while (--columns);
	/* Last lower-right pixel is handled here as special case */
	qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0], cur_bay[bay_line]);
	qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
	qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
	qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
}
/* }}} */
/* {{{ [fold] qc_imag_bay2rgb_gptm_fast(unsigned char *bay, int bay_line, unsigned char *rgb, int rgb_line, int columns, int rows, int bpp) */
/* Convert Bayer image to RGB image using Generalized Pei-Tam method
 * Uses fixed weights */
/* Execution time: 3795517 clock cycles */
static inline void qc_imag_bay2rgb_gptm_fast(unsigned char *bay, int bay_line,
		   unsigned char *rgb, int rgb_line,
		   int columns, int rows, int bpp)
{
	int r,g,b,w;
	unsigned char *cur_bay, *cur_rgb;
	int bay_line2, bay_line3, rgb_line2;
	int total_columns;

	/* Process 2 lines and rows per each iteration, but process the first and last two columns and rows separately */
	total_columns = (columns>>1) - 2;
	rows = (rows>>1) - 2;
	bay_line2 = 2*bay_line;
	bay_line3 = 3*bay_line;
	rgb_line2 = 2*rgb_line;

	/* Process first two pixel rows here */
	cur_bay = bay;
	cur_rgb = rgb;
	columns = total_columns + 2;
	do {
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		cur_bay += 2;
		cur_rgb += 2*bpp;
	} while (--columns);
	bay += bay_line2;
	rgb += rgb_line2;

	do {
		cur_bay = bay;
		cur_rgb = rgb;
		columns = total_columns;
		
		/* Process first 2x2 pixel block in a row here */
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		cur_bay += 2;
		cur_rgb += 2*bpp;

		do {
			w = 4*cur_bay[0] - (cur_bay[-bay_line-1] + cur_bay[-bay_line+1] + cur_bay[bay_line-1] + cur_bay[bay_line+1]);
			r = (2*(cur_bay[-1] + cur_bay[1]) + w) >> 2;
			b = (2*(cur_bay[-bay_line] + cur_bay[bay_line]) + w) >> 2;
			qc_imag_writergb(cur_rgb+0, bpp, CLIP(r,0,255), cur_bay[0], CLIP(b,0,255));

			w = 4*cur_bay[1] - (cur_bay[-bay_line2+1] + cur_bay[-1] + cur_bay[3] + cur_bay[bay_line2+1]);
			g = (2*(cur_bay[-bay_line+1] + cur_bay[0] + cur_bay[2] + cur_bay[bay_line+1]) + w) >> 3;
			b = (2*(cur_bay[-bay_line] + cur_bay[-bay_line+2] + cur_bay[bay_line] + cur_bay[bay_line+2]) + w) >> 3;
			qc_imag_writergb(cur_rgb+bpp, bpp, cur_bay[1], CLIP(g,0,255), CLIP(b,0,255));

			w = 4*cur_bay[bay_line] - (cur_bay[-bay_line] + cur_bay[bay_line-2] + cur_bay[bay_line+2] + cur_bay[bay_line3]);
			r = ((cur_bay[-1] + cur_bay[1] + cur_bay[bay_line2-1] + cur_bay[bay_line2+1]) + w) >> 2;
			g = ((cur_bay[0] + cur_bay[bay_line-1] + cur_bay[bay_line+1] + cur_bay[bay_line2]) + w) >> 2;
			qc_imag_writergb(cur_rgb+rgb_line, bpp, CLIP(r,0,255), CLIP(g,0,255), cur_bay[bay_line]);

			w = 4*cur_bay[bay_line+1] - (cur_bay[0] + cur_bay[2] + cur_bay[bay_line2] + cur_bay[bay_line2+2]);
			r = (2*(cur_bay[1] + cur_bay[bay_line2+1]) + w) >> 2;
			b = (2*(cur_bay[bay_line] + cur_bay[bay_line+2]) + w) >> 2;
			qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, CLIP(r,0,255), cur_bay[bay_line+1], CLIP(b,0,255));

			cur_bay += 2;
			cur_rgb += 2*bpp;
		} while (--columns);

		/* Process last 2x2 pixel block in a row here */
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);

		bay += bay_line2;
		rgb += rgb_line2;
	} while (--rows);

	/* Process last two pixel rows here */
	cur_bay = bay;
	cur_rgb = rgb;
	columns = total_columns + 2;
	do {
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		cur_bay += 2;
		cur_rgb += 2*bpp;
	} while (--columns);
}
/* }}} */
/* {{{ [fold] qc_imag_bay2rgb_gptm(unsigned char *bay, int bay_line, unsigned char *rgb, int rgb_line, int columns, int rows, int bpp, int sharpness) */
/* Convert Bayer image to RGB image using Generalized Pei-Tam method (See:
 * "Effective Color Interpolation in CCD Color Filter Arrays Using Signal Correlation"
 * IEEE Transactions on Circuits and Systems for Video Technology, vol. 13, no. 6, June 2003.
 * Note that this is much improved version of the algorithm described in the paper)
 * bay = points to the bayer image data (upper left pixel is green)
 * bay_line = bytes between the beginnings of two consecutive rows
 * rgb = points to the rgb image data that is written
 * rgb_line = bytes between the beginnings of two consecutive rows
 * columns, rows = bayer image size (both must be even)
 * bpp = number of bytes in each pixel in the RGB image (should be 3 or 4)
 * sharpness = how sharp the image should be, between 0..65535 inclusive.
 *             23170 gives in theory image that corresponds to the original
 *             best, but human eye likes slightly sharper picture... 32768 is a good bet.
 *             When sharpness = 0, this routine is same as bilinear interpolation.
 */
/* Execution time: 4344042 clock cycles for CIF image (Pentium II) */
static inline void qc_imag_bay2rgb_gptm(unsigned char *bay, int bay_line,
		   unsigned char *rgb, int rgb_line,
		   int columns, int rows, int bpp, unsigned int sharpness)
{

	/* 0.8 fixed point weights, should be between 0-256. Larger value = sharper, zero corresponds to bilinear interpolation. */
	/* Best PSNR with sharpness = 23170 */
	static const int wrg0 = 144;		/* Weight for Red on Green */
	static const int wbg0 = 160;
	static const int wgr0 = 120;
	static const int wbr0 = 192;
	static const int wgb0 = 120;
	static const int wrb0 = 168;

	int wrg;
	int wbg;
	int wgr;
	int wbr;
	int wgb;
	int wrb;

	unsigned int wu;
	int r,g,b,w;
	unsigned char *cur_bay, *cur_rgb;
	int bay_line2, bay_line3, rgb_line2;
	int total_columns;

	/* Compute weights */
	wu = (sharpness * sharpness) >> 16;
 	wu = (wu * wu) >> 16;
	wrg = (wrg0 * wu) >> 10;
	wbg = (wbg0 * wu) >> 10;
	wgr = (wgr0 * wu) >> 10;
	wbr = (wbr0 * wu) >> 10;
	wgb = (wgb0 * wu) >> 10;
	wrb = (wrb0 * wu) >> 10;

	/* Process 2 lines and rows per each iteration, but process the first and last two columns and rows separately */
	total_columns = (columns>>1) - 2;
	rows = (rows>>1) - 2;
	bay_line2 = 2*bay_line;
	bay_line3 = 3*bay_line;
	rgb_line2 = 2*rgb_line;

	/* Process first two pixel rows here */
	cur_bay = bay;
	cur_rgb = rgb;
	columns = total_columns + 2;
	do {
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		cur_bay += 2;
		cur_rgb += 2*bpp;
	} while (--columns);
	bay += bay_line2;
	rgb += rgb_line2;

	do {
		cur_bay = bay;
		cur_rgb = rgb;
		columns = total_columns;
		
		/* Process first 2x2 pixel block in a row here */
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		cur_bay += 2;
		cur_rgb += 2*bpp;

		do {
			w = 4*cur_bay[0] - (cur_bay[-bay_line-1] + cur_bay[-bay_line+1] + cur_bay[bay_line-1] + cur_bay[bay_line+1]);
			r = (512*(cur_bay[-1] + cur_bay[1]) + w*wrg) >> 10;
			b = (512*(cur_bay[-bay_line] + cur_bay[bay_line]) + w*wbg) >> 10;
			qc_imag_writergb(cur_rgb+0, bpp, CLIP(r,0,255), cur_bay[0], CLIP(b,0,255));

			w = 4*cur_bay[1] - (cur_bay[-bay_line2+1] + cur_bay[-1] + cur_bay[3] + cur_bay[bay_line2+1]);
			g = (256*(cur_bay[-bay_line+1] + cur_bay[0] + cur_bay[2] + cur_bay[bay_line+1]) + w*wgr) >> 10;
			b = (256*(cur_bay[-bay_line] + cur_bay[-bay_line+2] + cur_bay[bay_line] + cur_bay[bay_line+2]) + w*wbr) >> 10;
			qc_imag_writergb(cur_rgb+bpp, bpp, cur_bay[1], CLIP(g,0,255), CLIP(b,0,255));

			w = 4*cur_bay[bay_line] - (cur_bay[-bay_line] + cur_bay[bay_line-2] + cur_bay[bay_line+2] + cur_bay[bay_line3]);
			r = (256*(cur_bay[-1] + cur_bay[1] + cur_bay[bay_line2-1] + cur_bay[bay_line2+1]) + w*wrb) >> 10;
			g = (256*(cur_bay[0] + cur_bay[bay_line-1] + cur_bay[bay_line+1] + cur_bay[bay_line2]) + w*wgb) >> 10;
			qc_imag_writergb(cur_rgb+rgb_line, bpp, CLIP(r,0,255), CLIP(g,0,255), cur_bay[bay_line]);

			w = 4*cur_bay[bay_line+1] - (cur_bay[0] + cur_bay[2] + cur_bay[bay_line2] + cur_bay[bay_line2+2]);
			r = (512*(cur_bay[1] + cur_bay[bay_line2+1]) + w*wrg) >> 10;
			b = (512*(cur_bay[bay_line] + cur_bay[bay_line+2]) + w*wbg) >> 10;
			qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, CLIP(r,0,255), cur_bay[bay_line+1], CLIP(b,0,255));

			cur_bay += 2;
			cur_rgb += 2*bpp;
		} while (--columns);

		/* Process last 2x2 pixel block in a row here */
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);

		bay += bay_line2;
		rgb += rgb_line2;
	} while (--rows);

	/* Process last two pixel rows here */
	cur_bay = bay;
	cur_rgb = rgb;
	columns = total_columns + 2;
	do {
		qc_imag_writergb(cur_rgb+0,            bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+bpp,          bpp, cur_bay[1], cur_bay[0],          cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line,     bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		qc_imag_writergb(cur_rgb+rgb_line+bpp, bpp, cur_bay[1], cur_bay[bay_line+1], cur_bay[bay_line]);
		cur_bay += 2;
		cur_rgb += 2*bpp;
	} while (--columns);
}
/* }}} */
/* {{{ [fold] qc_imag_rgbbgr(unsigned char *dst, int pixels, int bpp) */
/* Convert RGB image to BGR or vice versa with the given number of pixels and
 * bytes per pixel
 */
static void inline qc_imag_rgbbgr(unsigned char *dst, int pixels, int bpp)
{
	unsigned char r,b;
	do {
		r = dst[0];
		b = dst[2];
		dst[0] = b;
		dst[2] = r;
		dst += bpp;
	} while (--pixels);
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_fmt:  Start of generic format query functions ********************** */

/* {{{ [fold] struct qc_fmt_format: a format definition */
struct qc_fmt_format {
	u32 fcc;		/* M$ defined fourcc code, see http://www.fourcc.org */
	signed char bpp;	/* 0=variable, -1=unknown (FIXME:what bpps do AVIs use here?) */
	char order;		/* 'R' = RGB, 'B'=BGR, 0=not specified */
	unsigned char nr, ng, nb;	/* Number of red, green, blue levels (0=256 levels) */
	char *name;		/* Human-readable name */
	Bool supported;		/* Can be converted to? */
	/* Here we could add a pointer to list containing conversion routines to other fourcc's */
	/* Then write code to create minimum spanning tree of format conversions */
	/* Include estimated cost per pixel to apply a conversion routine to weight edges */
};
/* }}} */
/* {{{ [fold] List of supported formats */
#define BF_RGB(r,g,b)	'R', (b)&0xFF, (g)&0xFF, (r)&0xFF
#define BF_BGR(r,g,b)	'B', (b)&0xFF, (g)&0xFF, (r)&0xFF
#define NO_BF		0, 0, 0, 0
#define FORMAT(ID,FCC1,FCC2,FCC3,FCC4,BPP,BF,NAME,SUPP) \
static const struct qc_fmt_format qc_fmt_formats_##ID = { v4l2_fourcc(FCC1,FCC2,FCC3,FCC4), BPP, BF, NAME, SUPP }
FORMAT(Y800,     'Y','8','0','0',   8, NO_BF,                 "GREY",   TRUE);
FORMAT(RGB_HI,   'q','c','R','B',   8, BF_RGB(6, 6, 6),       "HI240",  FALSE);	/* Not sure: BF_RGB or BF_BGR? Same as BT20? Don't think so */
FORMAT(RGB_332,  3,0,0,0,           8, BF_RGB(8, 8, 4),       "RGB332", FALSE);
/* Little endian RGB formats (least significant byte at lowest address) */
FORMAT(RGB_555L, 3,0,0,0,          16, BF_RGB(32, 32, 32),    "RGB555L",TRUE);	/* Should this be 15 or 16 bpp? Is this same as RGB2? */
FORMAT(RGB_565L, 3,0,0,0,          16, BF_RGB(32, 64, 32),    "RGB565L",TRUE);	/* Is this same as RGB2? */
FORMAT(RGB_24L,  'R','G','B','2',  24, BF_RGB(256, 256, 256), "RGB24L", TRUE);
FORMAT(BGR_24L,  'R','G','B','2',  24, BF_BGR(256, 256, 256), "BGR24L", TRUE);
FORMAT(RGB_32L,  'R','G','B','2',  32, BF_RGB(256, 256, 256), "RGB32L", TRUE);
FORMAT(BGR_32L,  'R','G','B','2',  32, BF_BGR(256, 256, 256), "BGR32L", TRUE);
/* Big endian RGB formats (most significant byte at lowest address) */
FORMAT(RGB_555B, 'q','c','R','B',  16, BF_RGB(32, 32, 32),    "RGB555B",FALSE);
FORMAT(RGB_565B, 'q','c','R','B',  16, BF_RGB(32, 64, 32),    "RGB565B",FALSE);
/* Component YUV formats */
FORMAT(YUY2,     'Y','U','Y','2',  16, NO_BF, "YUV422",    TRUE);		/* 4:2:2 packed pixel YUYV */
FORMAT(UYVY,     'U','Y','V','Y',  16, NO_BF, "UYVY",      FALSE);
FORMAT(IYUV,     'I','Y','U','V',  12, NO_BF, "YUV420",    FALSE);
/* Planar YUV formats */
FORMAT(YV12,     'Y','V','1','2',  12, NO_BF, "YV12",      FALSE);
FORMAT(YVU9,     'Y','V','U','9',   9, NO_BF, "YVU9",      FALSE);
FORMAT(Y41P,     'Y','4','1','P',  12, NO_BF, "Y41P",      FALSE);		/* 4:1:1 packed pixel UYVY UYVY YYYY */
FORMAT(qcY1,     'q','c','Y','1',  12, NO_BF, "YUV411P",   FALSE);		/* Like Y41P but planar and Y, U and V planes are in this order */
FORMAT(qcY2,     'q','c','Y','2',  16, NO_BF, "YUV422P",   TRUE);		/* Like YUY2 but planar */
FORMAT(qcV1,     'q','c','V','1',  12, NO_BF, "YVU411P",   FALSE);		/* Like qcY1 but V and U planes are in this order */
FORMAT(qcV2,     'q','c','V','2',  16, NO_BF, "YVU422P",   FALSE);		/* Like qcY2 but V and U planes are in this order */
FORMAT(qcU9,     'q','c','U','9',   9, NO_BF, "YUV410P",   TRUE);		/* Like YVU9 but U and V planes are in this order */
FORMAT(qcYY,     'q','c','Y','Y',  12, NO_BF, "YYUV",      FALSE);		/* Packed 4:2:2 sampling, Y, Y, U, V  */
FORMAT(NV12,     'N','V','1','2',  12, NO_BF, "NV12",      FALSE);
FORMAT(NV21,     'N','V','2','1',  12, NO_BF, "NV21",      FALSE);
/* Special formats */
FORMAT(qcBT,     'q','c','B','T',  -1, NO_BF, "BT848 RAW", FALSE);		/* RAW is raw scanline data sampled (before PAL decoding) */
FORMAT(qcBR,     'q','c','B','R',   8, NO_BF, "BAYER",     TRUE);		/* Same as STVA? */
FORMAT(qcMJ,     'q','c','M','J',   0, NO_BF, "MJPEG",     TRUE);		/* Same as MJPG? */
FORMAT(qcWN,     'q','c','W','N',  -1, NO_BF, "Winnov hw", FALSE);		/* Same as WNV1 (or CHAM, WINX, YUV8)? */
/* }}} */
/* {{{ [fold] struct qc_fmt_alias: Alias fourcc codes for above formats */
static struct qc_fmt_alias {
	u32 fcc;
	struct qc_fmt_format const *format;
} const qc_fmt_aliases[] = {
	{ v4l2_fourcc(0,0,0,0),         &qc_fmt_formats_RGB_24L },	/* Could be any format with fourcc 'RGB2' */
	{ v4l2_fourcc('Y','8',' ',' '), &qc_fmt_formats_Y800 },
	{ v4l2_fourcc('Y','U','N','V'), &qc_fmt_formats_YUY2 },
	{ v4l2_fourcc('V','4','2','2'), &qc_fmt_formats_YUY2 },
	{ v4l2_fourcc('Y','4','2','2'), &qc_fmt_formats_UYVY },
	{ v4l2_fourcc('U','Y','N','V'), &qc_fmt_formats_UYVY },
	{ v4l2_fourcc('I','4','2','0'), &qc_fmt_formats_IYUV },
};
/* }}} */
/* {{{ [fold] struct qc_fmt_palette: table to convert V4L code into fourcc, supported formats */
static struct qc_fmt_palette {
	int palette;		/* V4L1 standard palette type */
	struct qc_fmt_format const *format;
} const qc_fmt_palettes[] = {
 	{ VIDEO_PALETTE_GREY,    &qc_fmt_formats_Y800 },
	{ VIDEO_PALETTE_HI240,   &qc_fmt_formats_RGB_HI },
	/* RGB formats */
	{ VIDEO_PALETTE_RGB565,  &qc_fmt_formats_RGB_565L },
	{ VIDEO_PALETTE_RGB24,   &qc_fmt_formats_BGR_24L },
	{ VIDEO_PALETTE_RGB32,   &qc_fmt_formats_BGR_32L },
	{ VIDEO_PALETTE_RGB555,  &qc_fmt_formats_RGB_555L },
	/* Component YUV formats */
	{ VIDEO_PALETTE_YUV422,  &qc_fmt_formats_YUY2 },	/* Assume this is YUY2, even though V4L1 docs say this is 8 bpp format */
	{ VIDEO_PALETTE_YUYV,	 &qc_fmt_formats_YUY2 },	/* Benedict Bridgwater <bennyb@ntplx.net>: Bt848 maps this to Y41P, but it is simply wrong--we follow V4L2 v4l_compat.c */
	{ VIDEO_PALETTE_UYVY,	 &qc_fmt_formats_UYVY },
	{ VIDEO_PALETTE_YUV420,  &qc_fmt_formats_IYUV },	/* Assume this is planar, even though V4L1 header file indicates otherwise */
	{ VIDEO_PALETTE_YUV411,  &qc_fmt_formats_qcY1 },	/* Assume this is planar, even though V4L1 header file indicates otherwise (could be also fourcc 'Y41P') -from benedict */
	{ VIDEO_PALETTE_RAW,	 &qc_fmt_formats_qcBT },
	/* Planar YUV formats */
	{ VIDEO_PALETTE_YUV422P, &qc_fmt_formats_qcY2 },
	{ VIDEO_PALETTE_YUV411P, &qc_fmt_formats_qcY1 },
	{ VIDEO_PALETTE_YUV420P, &qc_fmt_formats_IYUV },
	{ VIDEO_PALETTE_YUV410P, &qc_fmt_formats_qcU9 },
	/* Special formats */
	{ VIDEO_PALETTE_BAYER,   &qc_fmt_formats_qcBR },
	{ VIDEO_PALETTE_MJPEG,   &qc_fmt_formats_qcMJ },
};
/* }}} */
/* {{{ [fold] struct qc_fmt_v4l2: V4L2 defines its own conflicting format codes */
static struct qc_fmt_v4l2 {
	u32 v4l2code;
	struct qc_fmt_format const *format;
} const qc_fmt_v4l2s[] = {
	{ v4l2_fourcc('R','G','B','1'), &qc_fmt_formats_RGB_332 },	/* V4L2_PIX_FMT_RGB332   8  RGB-3-3-2	  */
	{ v4l2_fourcc('R','G','B','O'), &qc_fmt_formats_RGB_555L },	/* V4L2_PIX_FMT_RGB555  16  RGB-5-5-5	  */
	{ v4l2_fourcc('R','G','B','P'), &qc_fmt_formats_RGB_565L },	/* V4L2_PIX_FMT_RGB565  16  RGB-5-6-5	  */
	{ v4l2_fourcc('R','G','B','Q'), &qc_fmt_formats_RGB_555B },	/* V4L2_PIX_FMT_RGB555X 16  RGB-5-5-5 BE  */
	{ v4l2_fourcc('R','G','B','R'), &qc_fmt_formats_RGB_565B },	/* V4L2_PIX_FMT_RGB565X 16  RGB-5-6-5 BE  */
	{ v4l2_fourcc('B','G','R','3'), &qc_fmt_formats_BGR_24L },	/* V4L2_PIX_FMT_BGR24	24  BGR-8-8-8	  */
	{ v4l2_fourcc('R','G','B','3'), &qc_fmt_formats_RGB_24L },	/* V4L2_PIX_FMT_RGB24	24  RGB-8-8-8	  */
	{ v4l2_fourcc('B','G','R','4'), &qc_fmt_formats_BGR_32L },	/* V4L2_PIX_FMT_BGR32	32  BGR-8-8-8-8   */
	{ v4l2_fourcc('R','G','B','4'), &qc_fmt_formats_RGB_32L },	/* V4L2_PIX_FMT_RGB32	32  RGB-8-8-8-8   */
	{ v4l2_fourcc('G','R','E','Y'), &qc_fmt_formats_Y800 },		/* V4L2_PIX_FMT_GREY	 8  Greyscale	  */
	{ v4l2_fourcc('Y','V','U','9'), &qc_fmt_formats_YVU9 },		/* V4L2_PIX_FMT_YVU410   9  YVU 4:1:0	  */
	{ v4l2_fourcc('Y','V','1','2'), &qc_fmt_formats_YV12 },		/* V4L2_PIX_FMT_YVU420  12  YVU 4:2:0	  */
	{ v4l2_fourcc('Y','U','Y','V'), &qc_fmt_formats_YUY2 },		/* V4L2_PIX_FMT_YUYV	16  YUV 4:2:2	  */
	{ v4l2_fourcc('Y','U','Y','2'), &qc_fmt_formats_YUY2 },		/* V4L2_PIX_FMT_YUY2	16  YUV 4:2:2: undocumented, guess same as YUY2 */
	{ v4l2_fourcc('U','Y','V','Y'), &qc_fmt_formats_UYVY },		/* V4L2_PIX_FMT_UYVY	16  YUV 4:2:2	  */
	{ v4l2_fourcc('Y','4','1','P'), &qc_fmt_formats_Y41P },		/* V4L2_PIX_FMT_Y41P	12  YUV 4:1:1	  */
	{ v4l2_fourcc('Y','U','V','9'), &qc_fmt_formats_qcU9 },		/* V4L2_PIX_FMT_YUV410   9  YUV 4:1:0	  */
	{ v4l2_fourcc('Y','U','1','2'), &qc_fmt_formats_IYUV },		/* V4L2_PIX_FMT_YUV420  12  YUV 4:2:0	  */
	{ v4l2_fourcc('P','4','2','2'), &qc_fmt_formats_qcY2 },		/* V4L2_PIX_FMT_YUV422P 16  YUV422 planar */
	{ v4l2_fourcc('P','4','1','1'), &qc_fmt_formats_qcY1 },		/* V4L2_PIX_FMT_YUV411P 16  YUV411 planar: assume bpp should be 12  */
	{ v4l2_fourcc('N','V','1','2'), &qc_fmt_formats_NV12 },		/* V4L2_PIX_FMT_NV12	12  Y/UV 4:2:0    */
	{ v4l2_fourcc('4','2','2','P'), &qc_fmt_formats_qcV2 },		/* V4L2_PIX_FMT_YVU422P 16  YVU422 planar */
	{ v4l2_fourcc('4','1','1','P'), &qc_fmt_formats_qcV1 },		/* V4L2_PIX_FMT_YVU411P 16  YVU411 planar: assume bpp should be 12 */
	{ v4l2_fourcc('Y','Y','U','V'), &qc_fmt_formats_qcYY },		/* V4L2_PIX_FMT_YYUV	16  YUV 4:2:2: undocumented, guess this is qc YY */
	{ v4l2_fourcc('H','I','2','4'), &qc_fmt_formats_RGB_HI },	/* V4L2_PIX_FMT_HI240	 8  8-bit color   */
	{ v4l2_fourcc('N','V','2','1'), &qc_fmt_formats_NV21 },		/* V4L2_PIX_FMT_NV21	12  Y/UV 4:2:0    */
	{ v4l2_fourcc('W','N','V','A'), &qc_fmt_formats_qcWN },		/* V4L2_PIX_FMT_WNVA	Winnov hw compres */
};
/* }}} */

/* {{{ [fold] qc_fmt_issupported(int palette) */
/* Check the format (can be called even before qc_fmt_init) */
int qc_fmt_issupported(int palette)
{
	int i;
	for (i=0; i<SIZE(qc_fmt_palettes); i++) {
		if (qc_fmt_palettes[i].palette==palette && qc_fmt_palettes[i].format->supported)
			return 0;
	}
	return -EINVAL;
}
/* }}} */
/* {{{ [fold] qc_fmt_getname(int palette) */
/* Return the format name (can be called even before qc_fmt_init) */
const char *qc_fmt_getname(int palette)
{
	int i;
	for (i=0; i<SIZE(qc_fmt_palettes); i++) {
		if (qc_fmt_palettes[i].palette==palette)
			return qc_fmt_palettes[i].format->name;
	}
	return "Unknown";
}
/* }}} */
/* {{{ [fold] qc_fmt_getdepth(int palette) */
/* Return bits per pixel for the format, or 
 * 0=variable number (compressed formats), -1=unknown 
 * (can be called even before qc_fmt_init) */
int qc_fmt_getdepth(int palette)
{
	int i;
	for (i=0; i<SIZE(qc_fmt_palettes); i++) {
		if (qc_fmt_palettes[i].palette==palette)
			return qc_fmt_palettes[i].format->bpp;
	}
	return -1;	/* Unknown bit depth */
}
/* }}} */
/* {{{ [fold] qc_fmt_init(struct quickcam *qc) */
int qc_fmt_init(struct quickcam *qc)
{
	int r = 0;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_fmt_init(qc=%p/compr=%i)",qc,qc->settings.compress);
#if COMPRESS
	if (qc->settings.compress) {
		qc->fmt_data.compress = TRUE;
		r = qc_mjpeg_init(&qc->fmt_data.mjpeg_data, 24, DEFAULT_BGR);
	} else {
		qc->fmt_data.compress = FALSE;
	}
#endif
	if (r>=0) IDEBUG_INIT(qc->fmt_data);
	return r;
}
/* }}} */
/* {{{ [fold] qc_fmt_exit(struct quickcam *qc) */
void qc_fmt_exit(struct quickcam *qc)
{
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_fmt_exit(qc=%p)",qc);
#if COMPRESS
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_fmt_exit/compress=%i",qc->fmt_data.compress);
	if (qc->fmt_data.compress) qc_mjpeg_exit(&qc->fmt_data.mjpeg_data);
	POISON(qc->fmt_data.compress);
	POISON(qc->fmt_data.mjpeg_data);
#endif
	POISON(qc->fmt_data.lut);
	IDEBUG_EXIT_NOPOISON(qc->fmt_data);
}
/* }}} */
/* {{{ [fold] qc_fmt_convert(struct quickcam *qc, unsigned char *src, unsigned int src_len, unsigned char *dst, unsigned int dst_len, int *midvalue) */
/* Called after each full frame of bayer or compressed image obtained */
/* Convert camera data in src to the application requested palette in dst */
/* Return negative error code if failure, otherwise data length stored in dst */
/* src_len is the length of actual data in src, dst_len is the maximum data size storable in dst */
/* Also src buffer may be modified */
/* Return image average brightness in midvalue (or -1 if not computed) */
int qc_fmt_convert(struct quickcam *qc, unsigned char *src, unsigned int src_len, unsigned char *dst, unsigned int dst_len, int *midvalue)
{
	signed int originx, originy;	/* Upper-left corner coordinates of the capture window in the bayer image */
	unsigned char *bayerwin;
	int length;		/* Converted image data length in bytes */
	int r = 0;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_imag_convert(qc=%p,src=%p,src_len=%i,dst=%p,dst_len=%i)",qc,src,src_len,dst,dst_len);
	if (PARANOID && (qc==NULL || src==NULL || dst==NULL)) { PRINTK(KERN_CRIT,"NULL"); return -EINVAL; }
	if (PARANOID && midvalue==NULL) { PRINTK(KERN_CRIT,"NULL"); return -EINVAL; }
	IDEBUG_TEST(qc->fmt_data);
	*midvalue = -1;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("sensor_width=%i sensor_height=%i vwin.width=%i vwin.height=%i",
			qc->sensor_data.width,qc->sensor_data.height,qc->vwin.width,qc->vwin.height);

	if (qc->sensor_data.width < qc->vwin.width || qc->sensor_data.height < qc->vwin.height) {
		if (qcdebug&QC_DEBUGERRORS) PDEBUG("sensor window is smaller than requested");
		r = -ENOMSG;
		goto fail;
	}

#if DUMPDATA
	if (midvalue) *midvalue = -1;
	memset(dst, 0, dst_len);
	memcpy(dst, src, src_len>dst_len ? dst_len : src_len);
	length = (int)qc->vwin.width * (int)qc->vwin.height * 3;
	return length;
#endif

#if COMPRESS
	if (!qc->sensor_data.compress) {
#endif
		/* src buffer contains fixed length data in bayer format */
		/* sensor_data.height/width = frame size that is captured from the camera */
		/* vwin.height/width        = frame size that the application is expecting */

		/* Check if src buffer contains enough data */
		if (src_len < qc->sensor_data.width * qc->sensor_data.height) {
			if (qcdebug&QC_DEBUGERRORS) PDEBUG("too little data by %i (expected %i)", qc->sensor_data.width*qc->sensor_data.height - src_len, qc->sensor_data.width * qc->sensor_data.height);
			r = -EBADE;
			goto fail;
		}
		/* calculate view window origin inside the whole frame */
		originy = ((signed int)qc->sensor_data.height - (signed int)qc->vwin.height) / 2;
		originx = ((signed int)qc->sensor_data.width - (signed int)qc->vwin.width) / 2;
		originx &= ~1;		/* Set upper-left corner to a even coordinate */
		originy &= ~1;		/* so that the first bayer pixel is green */
		bayerwin = src + originy * qc->sensor_data.width + originx;
		if (qcdebug&QC_DEBUGLOGIC) PDEBUG("originy=%i  originx=%i", originy,originx);

		if (qc->settings.adaptive && midvalue!=NULL)
			*midvalue = qc_imag_bayer_midvalue(bayerwin, qc->sensor_data.width, qc->vwin.width, qc->vwin.height);
		if (qc->settings.equalize || qc->settings.userlut) {
			if (qc->settings.equalize) {
				qc_imag_bayer_equalize(bayerwin, qc->sensor_data.width, qc->vwin.width, qc->vwin.height, &qc->fmt_data.lut);
			} else {
				/* Initialize LUT */
				int i;
				for (i=0; i<256; i++) qc->fmt_data.lut[QC_LUT_RED+i]   = i;
				for (i=0; i<256; i++) qc->fmt_data.lut[QC_LUT_GREEN+i] = i;
				for (i=0; i<256; i++) qc->fmt_data.lut[QC_LUT_BLUE+i]  = i;
			}
			if (qc->settings.userlut) {
				qc_imag_userlut(&qc->fmt_data.userlut, &qc->fmt_data.lut);
			}
			/* Could do here other effects to the lookup table */
			qc_imag_bayer_lut(bayerwin, qc->sensor_data.width, qc->vwin.width, qc->vwin.height, &qc->fmt_data.lut);
		}

		if (qc->vpic.palette==VIDEO_PALETTE_BAYER) {
			int i;
			length = (int)qc->vwin.width * (int)qc->vwin.height;
			if (length > dst_len) {
				r = -ENOSPC;
				goto fail;
			}
			/* It would be more efficient to capture data directly to the mmapped buffer,
			 * but more complex and hardly any application will use bayer palette anyway */
			for (i=0; i<qc->vwin.height; i++) {
				memcpy(dst, bayerwin, qc->vwin.width);
				bayerwin += qc->sensor_data.width;
				dst += qc->vwin.width;
			}
		} else {
			/* Convert the current frame to RGB */
			length = (int)qc->vwin.width * (int)qc->vwin.height * 3;
			if (length > dst_len) {
				r = -ENOSPC;
				goto fail;
			}
			switch (qc->settings.quality) {
			case 0:
				qc_imag_bay2rgb_cottnoip(bayerwin, qc->sensor_data.width,
						dst, 3*qc->vwin.width, 
						qc->vwin.width, qc->vwin.height, 3);
				break;
			case 1:
				qc_imag_bay2rgb_cott(bayerwin, qc->sensor_data.width,
						dst, 3*qc->vwin.width, 
						qc->vwin.width, qc->vwin.height, 3);
				break;
			case 2:
				qc_imag_bay2rgb_horip(bayerwin, qc->sensor_data.width,
						dst, 3*qc->vwin.width, 
						qc->vwin.width, qc->vwin.height, 3);
				break;
			case 3:
				qc_imag_bay2rgb_ip(bayerwin, qc->sensor_data.width,
						dst, 3*qc->vwin.width, 
						qc->vwin.width, qc->vwin.height, 3);
				break;
			default:
			case 4:
				qc_imag_bay2rgb_gptm_fast(bayerwin, qc->sensor_data.width,
						dst, 3*qc->vwin.width, 
						qc->vwin.width, qc->vwin.height, 3);
				break;
			case 5:
				qc_imag_bay2rgb_gptm(bayerwin, qc->sensor_data.width,
						dst, 3*qc->vwin.width, 
						qc->vwin.width, qc->vwin.height, 3, qc->vpic.whiteness);
				break;
			}
			goto rgb24;
		}

#if COMPRESS
	} else {
		/* src buffer contains variable length data in mjpeg format */
		if (qc->vpic.palette==VIDEO_PALETTE_MJPEG) {
			/* Directly copy data from src to dst, can not resize */
			length = src_len;
			if (length > dst_len) {
				r = -ENOSPC;
				goto fail;
			}
			memcpy(dst, src, src_len);
		} else {
			/* Decode compressed images */
			if (!qc->fmt_data.compress) {
				r = -EINVAL;
				goto fail;
			}
			length = qc->sensor_data.width * qc->sensor_data.height * 3;
			if (length > dst_len) {
				r = -ENOSPC;
				goto fail;
			}
			r = qc_mjpeg_decode(&qc->fmt_data.mjpeg_data, src, src_len, dst);
			if (r<0) goto fail;

			if ((int)qc->vwin.width < qc->sensor_data.width || (int)qc->vwin.height < qc->sensor_data.height) {
				/* User requested smaller image than camera sent, so crop the image */
				unsigned char *s, *d = dst;
				int i;
				s = dst;
				s += (qc->sensor_data.height - (int)qc->vwin.height)/2 * qc->sensor_data.width*3;
				s += (qc->sensor_data.width - (int)qc->vwin.width)/2 * 3;
				for (i=0; i<qc->vwin.height; i++) {
					memcpy(d, s, (int)qc->vwin.width*3);
					s += qc->sensor_data.width * 3;
					d += (int)qc->vwin.width * 3;
				}
				/* vwin.width/height is always smaller or equal to sensor_data.width/height */
				length = (int)qc->vwin.width * (int)qc->vwin.height * 3;
			}
			if (qc->settings.adaptive && midvalue!=NULL) *midvalue = qc_imag_rgb24_midvalue(dst, 3*qc->sensor_data.width, (int)qc->vwin.width, (int)qc->vwin.height);
			goto rgb24;
		}
	}
#endif
	return length;

rgb24:	/* We have now RGB (24 bpp) data in dst. If some other format is desired, */
	/* convert the RGB image to it (e.g. YUV) */

#ifdef DEBUG
#if 1
{
/* Draw red rectangle around image (useful for debugging boundaries) */
static const int R = 255;
static const int G = 0;
static const int B = 0;
 int ty,tx;
 for (tx=0; tx<qc->vwin.width; tx++) {
  ty=0;
  dst[ty*qc->vwin.width*3+tx*3] = B;
  dst[ty*qc->vwin.width*3+tx*3+1] = G;
  dst[ty*qc->vwin.width*3+tx*3+2] = R;
  ty=qc->vwin.height-1;
  dst[ty*qc->vwin.width*3+tx*3] = B;
  dst[ty*qc->vwin.width*3+tx*3+1] = G;
  dst[ty*qc->vwin.width*3+tx*3+2] = R;
 }
 for (ty=0; ty<qc->vwin.height; ty++) {
  tx=0;
  dst[ty*qc->vwin.width*3+tx*3] = B;
  dst[ty*qc->vwin.width*3+tx*3+1] = G;
  dst[ty*qc->vwin.width*3+tx*3+2] = R;
  tx=qc->vwin.width-1;
  dst[ty*qc->vwin.width*3+tx*3] = B;
  dst[ty*qc->vwin.width*3+tx*3+1] = G;
  dst[ty*qc->vwin.width*3+tx*3+2] = R;
 }
}
#endif
#endif

	if (qc->vpic.palette != VIDEO_PALETTE_RGB24) {
		// FIXME: should check here that dst_len <= resulted image length
		length = qc_yuv_rgb2yuv(dst, length, qc->vpic.palette);
	} else if (qc->settings.compat_torgb) {
		qc_imag_rgbbgr(dst, length/3, 3);
	}
	return length;

fail:	if (qcdebug&(QC_DEBUGERRORS|QC_DEBUGLOGIC)) PDEBUG("failed qc_imag_convert()=%i",r);
	return r;
}
/* }}} */

/* }}} */

/* End of file */
