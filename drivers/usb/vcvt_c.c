/*
   Colour conversion routines (RGB <-> YUV) in plain C, with viewport 
   extension.
   (C) 2001 Nemosoft Unv.    nemosoft@smcc.demon.nl
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


/* Colour conversion routines that use a viewport. See the assembly code/.h
   files for more information.

   If you are always converting images where the image- and viewport size
   is the same, you can hack out the 'plus' variable and delete the 
   offset calculation at the bottom of every for(line) {} loop. Otherwise,
   just call the functions with plus = width.
   
   NB: for these function, the YUV420 format is defined as:
   even lines:  YYYY UU YYYY UU YYYY ..
   odd lines:   YYYY VV YYYY VV YYYY ..
*/   

#include "vcvt.h"


#define PUSH_RGB24	1
#define PUSH_BGR24	2
#define PUSH_RGB32	3
#define PUSH_BGR32	4

/**
  \brief convert YUV 4:2:0 data into RGB, BGR, RGBa or BGRa
  \param width Width of yuv data, in pixels
  \param height Height of yuv data, in pixels
  \param plus Width of viewport, in pixels
  \param src beginning of YUV data
  \param dst beginning of RGB data, \b including the initial offset into the viewport
  \param push The requested RGB format 

  \e push can be any of PUSH_RGB24, PUSH_BGR24, PUSH_RGB32 or PUSH_BGR32
  
 This is a really simplistic approach. Speedups are welcomed. 
*/
static void vcvt_420i(int width, int height, int plus, unsigned char *src, unsigned char *dst, int push)
{
	int line, col, linewidth;
	int y, u, v, yy, vr = 0, ug = 0, vg = 0, ub = 0;
	int r, g, b;
	unsigned char *sy, *su, *sv;

	linewidth = width + (width >> 1);
	sy = src;
	su = sy + 4;
	sv = su + linewidth;

	/* The biggest problem is the interlaced data, and the fact that odd
	   add even lines have V and U data, resp. 
	 */
	for (line = 0; line < height; line++) {
		for (col = 0; col < width; col++) {
			y = *sy++;
			yy = y << 8;
			if ((col & 1) == 0) {
				/* only at even colums we update the u/v data */
				u = *su - 128;
				ug =   88 * u;
				ub =  454 * u;
				v = *sv - 128;
				vg =  183 * v;
				vr =  359 * v;

				su++;
				sv++;
			}
			if ((col & 3) == 3) {
				sy += 2; /* skip u/v */
				su += 4; /* skip y */
				sv += 4; /* skip y */
			}

			r = (yy +      vr) >> 8;
			g = (yy - ug - vg) >> 8;
			b = (yy + ub     ) >> 8;
			/* At moments like this, you crave for MMX instructions with saturation */
			if (r <   0) r =   0;
			if (r > 255) r = 255;
			if (g <   0) g =   0;
			if (g > 255) g = 255;
			if (b <   0) b =   0;
			if (b > 255) b = 255;
			
			switch(push) {
			case PUSH_RGB24:
				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				break;

			case PUSH_BGR24:
				*dst++ = b;
				*dst++ = g;
				*dst++ = r;
				break;
			
			case PUSH_RGB32:
				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				*dst++ = 0;
				break;

			case PUSH_BGR32:
				*dst++ = b;
				*dst++ = g;
				*dst++ = r;
				*dst++ = 0;
				break;
			}
		} /* ..for col */
		if (line & 1) { // odd line: go to next band
			su += linewidth;
			sv += linewidth;
		}
		else { // rewind u/v pointers
			su -= linewidth;
			sv -= linewidth;
		}
		/* Adjust destination pointer, using viewport. We have just
		   filled one line worth of data, so only skip the difference
		   between the view width and the image width.
		 */
		if (push == PUSH_RGB24 || push == PUSH_BGR24)
			dst += ((plus - width) * 3);
		else
			dst += ((plus - width) * 4);
	} /* ..for line */
}

void vcvt_420i_rgb24(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420i(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_RGB24);
}

void vcvt_420i_bgr24(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420i(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_BGR24);
}

void vcvt_420i_rgb32(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420i(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_RGB32);
}

void vcvt_420i_bgr32(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420i(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_BGR32);
}


/** \brief Convert from interlaces YUV 420 to planar format 
*/
void vcvt_420i_420p(int width, int height, int plus, void *src, void *dsty, void *dstu, void *dstv)
{
	short *s, *dy, *du, *dv;
	int line, col;

	s = (short *)src;
	dy = (short *)dsty;
	du = (short *)dstu;
	dv = (short *)dstv;
	for (line = 0; line < height; line++) {
		for (col = 0; col < width; col += 4) {
			*dy++ = *s++;
			*dy++ = *s++;
			if (line & 1)
				*dv++ = *s++;
			else
				*du++ = *s++;
		} /* ..for col */
		dy += (plus - width);
		dv += ((plus - width) >> 1);
		du += ((plus - width) >> 1);
	} /* ..for line */
}

void vcvt_420i_yuyv(int width, int height, int plus, void *src, void *dst)
{
	int line, col, linewidth;
	unsigned char *sy, *su, *sv, *d;

	linewidth = width + (width >> 1);
	sy = (unsigned char *)src;
	su = sy + 4;
	sv = su + linewidth;
	d = (unsigned char *)dst;

	for (line = 0; line < height; line++) {
		for (col = 0; col < width; col += 4) {
			/* four pixels in one go */
			*d++ = *sy++;
			*d++ = *su;
			*d++ = *sy++;
			*d++ = *sv;
			
			*d++ = *sy++;
			*d++ = *su++;
			*d++ = *sy++;
			*d++ = *sv++;

			sy += 2;
			su += 4;
			sv += 4;
		} /* ..for col */
		if (line & 1) { // odd line: go to next band
			su += linewidth;
			sv += linewidth;
		}
		else { // rewind u/v pointers
			su -= linewidth;
			sv -= linewidth;
		}
		/* Adjust for viewport width */
		d += ((plus - width) << 1);
	} /* ..for line */
}

