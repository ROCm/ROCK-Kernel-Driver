/* Start of file */

/* {{{ [fold] Comments */
/*
 * MJPEG decompression routines are from mpeg2dec,
 * Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 * Modified by Tuukka Toivonen and Jochen Hoenicke.
 *
 * Portions of this code are from the MPEG software simulation group
 * idct implementation. This code will be replaced with a new
 * implementation soon.
 *
 * The MJPEG routines are from mpeg2dec, a free MPEG-2 video stream decoder.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/* }}} */

#ifdef NOKERNEL
#include "quickcam.h"
#else
#include <linux/quickcam.h>
#endif

#ifdef __KERNEL__	/* show.c will include this file directly into compilation for userspace */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#include <linux/slab.h>
#else
#include <linux/malloc.h>
#endif
#endif /* __KERNEL__ */

#if COMPRESS
/* {{{ [fold] **** qc_mjpeg_yuv2rgb: MJPEG decoding: YUV to RGB conversion routines *************** */

/* {{{ [fold] Macros */
#define MODE_RGB  1
#define MODE_BGR  2

#define RGB(i)							\
	U = pu[i];						\
	V = pv[i];						\
	r = md->table_rV[V];					\
	g = (void *)(((u8 *)md->table_gU[U]) + md->table_gV[V]);\
	b = md->table_bU[U];

#define DST1(i)					\
	Y = py_1[2*i];				\
	dst_1[2*i] = r[Y] + g[Y] + b[Y];	\
	Y = py_1[2*i+1];			\
	dst_1[2*i+1] = r[Y] + g[Y] + b[Y];

#define DST2(i)					\
	Y = py_2[2*i];				\
	dst_2[2*i] = r[Y] + g[Y] + b[Y];	\
	Y = py_2[2*i+1];			\
	dst_2[2*i+1] = r[Y] + g[Y] + b[Y];

#define DST1RGB(i)							\
	Y = py_1[2*i];							\
	dst_1[6*i] = r[Y]; dst_1[6*i+1] = g[Y]; dst_1[6*i+2] = b[Y];	\
	Y = py_1[2*i+1];						\
	dst_1[6*i+3] = r[Y]; dst_1[6*i+4] = g[Y]; dst_1[6*i+5] = b[Y];

#define DST2RGB(i)							\
	Y = py_2[2*i];							\
	dst_2[6*i] = r[Y]; dst_2[6*i+1] = g[Y]; dst_2[6*i+2] = b[Y];	\
	Y = py_2[2*i+1];						\
	dst_2[6*i+3] = r[Y]; dst_2[6*i+4] = g[Y]; dst_2[6*i+5] = b[Y];

#define DST1BGR(i)							\
	Y = py_1[2*i];							\
	dst_1[6*i] = b[Y]; dst_1[6*i+1] = g[Y]; dst_1[6*i+2] = r[Y];	\
	Y = py_1[2*i+1];						\
	dst_1[6*i+3] = b[Y]; dst_1[6*i+4] = g[Y]; dst_1[6*i+5] = r[Y];

#define DST2BGR(i)							\
	Y = py_2[2*i];							\
	dst_2[6*i] = b[Y]; dst_2[6*i+1] = g[Y]; dst_2[6*i+2] = r[Y];	\
	Y = py_2[2*i+1];						\
	dst_2[6*i+3] = b[Y]; dst_2[6*i+4] = g[Y]; dst_2[6*i+5] = r[Y];
/* }}} */

/* {{{ [fold] qc_mjpeg_yuv2rgb_32() */
static void qc_mjpeg_yuv2rgb_32(struct qc_mjpeg_data *md, u8 *py_1, u8 *py_2, u8 *pu, u8 *pv,
	void *_dst_1, void *_dst_2, int width)
{
	int U, V, Y;
	u32 *r, *g, *b;
	u32 *dst_1, *dst_2;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_32(md=%p, py_1=%p, py_2=%p, pu=%p, pv=%p, _dst_1=%p, _dst_2=%p, width=%i",md,py_1,py_2,pu,pv,_dst_1,_dst_2,width);
	width >>= 3;
	dst_1 = _dst_1;
	dst_2 = _dst_2;

	do {
		RGB(0);
		DST1(0);
		DST2(0);

		RGB(1);
		DST2(1);
		DST1(1);

		RGB(2);
		DST1(2);
		DST2(2);

		RGB(3);
		DST2(3);
		DST1(3);

		pu += 4;
		pv += 4;
		py_1 += 8;
		py_2 += 8;
		dst_1 += 8;
		dst_2 += 8;
	} while (--width);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_32() done");
}
/* }}} */
/* {{{ [fold] qc_mjpeg_yuv2rgb_24rgb() */
/* This is very near from the yuv2rgb_32 code */
static void qc_mjpeg_yuv2rgb_24rgb(struct qc_mjpeg_data *md, u8 *py_1, u8 *py_2, u8 *pu, u8 *pv, 
	void *_dst_1, void *_dst_2, int width)
{
	int U, V, Y;
	u8 *r, *g, *b;
	u8 *dst_1, *dst_2;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_24rgb(md=%p, py_1=%p, py_2=%p, pu=%p, pv=%p, _dst_1=%p, _dst_2=%p, width=%i",md,py_1,py_2,pu,pv,_dst_1,_dst_2,width);

	width >>= 3;
	dst_1 = _dst_1;
	dst_2 = _dst_2;

	do {
		RGB(0);
		DST1RGB(0);
		DST2RGB(0);

		RGB(1);
		DST2RGB(1);
		DST1RGB(1);

		RGB(2);
		DST1RGB(2);
		DST2RGB(2);

		RGB(3);
		DST2RGB(3);
		DST1RGB(3);

		pu += 4;
		pv += 4;
		py_1 += 8;
		py_2 += 8;
		dst_1 += 24;
		dst_2 += 24;
	} while (--width);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_24rgb() done");
}
/* }}} */
/* {{{ [fold] qc_mjpeg_yuv2rgb_24bgr() */
/* only trivial mods from yuv2rgb_24rgb */
static void qc_mjpeg_yuv2rgb_24bgr(struct qc_mjpeg_data *md, u8 *py_1, u8 *py_2, u8 *pu, u8 *pv, 
	void *_dst_1, void *_dst_2, int width)
{
	int U, V, Y;
	u8 *r, *g, *b;
	u8 *dst_1, *dst_2;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_24bgr(md=%p, py_1=%p, py_2=%p, pu=%p, pv=%p, _dst_1=%p, _dst_2=%p, width=%i",md,py_1,py_2,pu,pv,_dst_1,_dst_2,width);
	width >>= 3;
	dst_1 = _dst_1;
	dst_2 = _dst_2;

	do {
		RGB(0);
		DST1BGR(0);
		DST2BGR(0);

		RGB(1);
		DST2BGR(1);
		DST1BGR(1);

		RGB(2);
		DST1BGR(2);
		DST2BGR(2);

		RGB(3);
		DST2BGR(3);
		DST1BGR(3);

		pu += 4;
		pv += 4;
		py_1 += 8;
		py_2 += 8;
		dst_1 += 24;
		dst_2 += 24;
	} while (--width);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_24bgr() done");
}
/* }}} */
/* {{{ [fold] qc_mjpeg_yuv2rgb_16() */
/* This is exactly the same code as yuv2rgb_32 except for the types of */
/* r, g, b, dst_1, dst_2 */
static void qc_mjpeg_yuv2rgb_16(struct qc_mjpeg_data *md, u8 *py_1, u8 *py_2, u8 *pu, u8 *pv, 
	void *_dst_1, void *_dst_2, int width)
{
	int U, V, Y;
	u16 *r, *g, *b;
	u16 *dst_1, *dst_2;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_16(md=%p, py_1=%p, py_2=%p, pu=%p, pv=%p, _dst_1=%p, _dst_2=%p, width=%i",md,py_1,py_2,pu,pv,_dst_1,_dst_2,width);
	width >>= 3;
	dst_1 = _dst_1;
	dst_2 = _dst_2;

	do {
		RGB(0);
		DST1(0);
		DST2(0);

		RGB(1);
		DST2(1);
		DST1(1);

		RGB(2);
		DST1(2);
		DST2(2);

		RGB(3);
		DST2(3);
		DST1(3);

		pu += 4;
		pv += 4;
		py_1 += 8;
		py_2 += 8;
		dst_1 += 8;
		dst_2 += 8;
	} while (--width);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_16() done");
}
/* }}} */
/* {{{ [fold] qc_mjpeg_yuv2rgb() */
/* Convert YUV image to RGB */
static void qc_mjpeg_yuv2rgb(struct qc_mjpeg_data *md, void *dst, u8 *py, u8 *pu, u8 *pv, 
	int width, int height, int rgb_stride, int y_stride, int uv_stride)
{
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb(md=%p, dst=%p, py=%p, pu=%p, pv=%p, width=%i, height=%i, rgb_stride=%i, y_stride=%i, uv_stride=%i",md,dst,py,pu,pv,width,height,rgb_stride,y_stride,uv_stride);
	height >>= 1;
	do {
		md->yuv2rgb_func(md, py, py + y_stride, pu, pv, dst, ((u8 *)dst) + rgb_stride, width);
		py += 2 * y_stride;
		pu += uv_stride;
		pv += uv_stride;
		dst = ((u8 *)dst) + 2 * rgb_stride;
	} while (--height);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb() done");
}
/* }}} */

static const u32 matrix_coefficients = 6;
static const s32 Inverse_Table_6_9[8][4] = {
	{ 117504, 138453, 13954, 34903 },	/* 0: no sequence_display_extension */
	{ 117504, 138453, 13954, 34903 },	/* 1: ITU-R Rec. 709 (1990) */
	{ 104597, 132201, 25675, 53279 },	/* 2: unspecified */
	{ 104597, 132201, 25675, 53279 },	/* 3: reserved */
	{ 104448, 132798, 24759, 53109 },	/* 4: FCC */
	{ 104597, 132201, 25675, 53279 },	/* 5: ITU-R Rec. 624-4 System B, G */
	{ 104597, 132201, 25675, 53279 },	/* 6: SMPTE 170M */
	{ 117579, 136230, 16907, 35559 } 	/* 7: SMPTE 240M (1987) */
};

/* {{{ [fold] div_round(int dividend, int divisor) */
static int div_round(int dividend, int divisor)
{
	if (dividend > 0) 
		return (dividend + (divisor>>1)) / divisor;
	else
		return -((-dividend + (divisor>>1)) / divisor);
}
/* }}} */
/* {{{ [fold] qc_mjpeg_yuv2rgb_init(struct qc_mjpeg_data *md, int bpp, int mode) */
/* Initialization of yuv2rgb routines. Return error code if failure */
static inline int qc_mjpeg_yuv2rgb_init(struct qc_mjpeg_data *md, int bpp, int mode)
{
	static const int table_Y_size = 1024;
	u8 *table_Y;
	int i, ret = -ENOMEM;
	int entry_size = 0;
	void *table_r = NULL, *table_g = NULL, *table_b = NULL;
	int crv = Inverse_Table_6_9[matrix_coefficients][0];
	int cbu = Inverse_Table_6_9[matrix_coefficients][1];
	int cgu = -Inverse_Table_6_9[matrix_coefficients][2];
	int cgv = -Inverse_Table_6_9[matrix_coefficients][3];
	u32 *table_32;
	u16 *table_16;
	u8 *table_8;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_init(md=%p, bpp=%i, mode=%i)",md,bpp,mode);
	table_Y = kmalloc(table_Y_size,GFP_KERNEL);	/* Allocate with kmalloc(), it might not fit into stack */
	if (table_Y==NULL) return -ENOMEM;

	for (i=0; i<1024; i++) {
		int j;
		j = (76309 * (i - 384 - 16) + 32768) >> 16;
		j = (j < 0) ? 0 : ((j > 255) ? 255 : j);
		table_Y[i] = j;
	}

	switch (bpp) {
	case 32:
		md->yuv2rgb_func = qc_mjpeg_yuv2rgb_32;
		table_32 = md->table = kmalloc((197 + 2*682 + 256 + 132) * sizeof(u32), GFP_KERNEL);	/* 0..1948 x 4 */
		if (!md->table) goto fail;
		entry_size = sizeof(u32);
		table_r = table_32 + 197;		/* R: -197..1751 */
		table_b = table_32 + 197 + 685;		/* B: -882..1066 */
		table_g = table_32 + 197 + 2*682;	/* G: -1561..387 */
		for (i=-197; i<256+197; i++)		/* Ri = -197...452 */
			((u32 *) table_r)[i] = table_Y[i+384] << ((mode==MODE_RGB) ? 16 : 0);
		for (i=-132; i<256+132; i++)		/* Gi = -132...387 */
			((u32 *) table_g)[i] = table_Y[i+384] << 8;
		for (i=-232; i<256+232; i++)		/* Bi = -232...487 */
			((u32 *) table_b)[i] = table_Y[i+384] << ((mode==MODE_RGB) ? 0 : 16);
		break;
	case 24:
		md->yuv2rgb_func = (mode==MODE_RGB) ? qc_mjpeg_yuv2rgb_24rgb : qc_mjpeg_yuv2rgb_24bgr;
		table_8 = md->table = kmalloc((256 + 2*232) * sizeof(u8), GFP_KERNEL);			/* 0..719 x 1 */
		if (!md->table) goto fail;
		entry_size = sizeof(u8);
		table_r = table_g = table_b = table_8 + 232;	/* -232..487 */
		for (i=-232; i<256+232; i++)			/* i = -232..487 */
			((u8 *)table_b)[i] = table_Y[i+384];
		break;
	case 15:
	case 16:
		md->yuv2rgb_func = qc_mjpeg_yuv2rgb_16;
		table_16 = md->table = kmalloc((197 + 2*682 + 256 + 132) * sizeof(u16), GFP_KERNEL);	/* 0..1948 x 2 */
		if (!md->table) goto fail;
		entry_size = sizeof(u16);
		table_r = table_16 + 197;		/* R: -197..1751 */
		table_b = table_16 + 197 + 685;		/* B: -882..1066 */
		table_g = table_16 + 197 + 2*682;	/* G: -1561..387 */
		for (i=-197; i<256+197; i++) {		/* Ri = -197..452 */
			int j = table_Y[i+384] >> 3;
			if (mode == MODE_RGB) j <<= ((bpp==16) ? 11 : 10);
			((u16 *)table_r)[i] = j;
		}
		for (i=-132; i<256+132; i++) {		/* Gi = -132..387 */
			int j = table_Y[i+384] >> ((bpp==16) ? 2 : 3);
			((u16 *)table_g)[i] = j << 5;
		}
		for (i=-232; i<256+232; i++) {		/* Bi = -232..487 */
			int j = table_Y[i+384] >> 3;
			if (mode == MODE_BGR) j <<= ((bpp==16) ? 11 : 10);
			((u16 *)table_b)[i] = j;
		}
		break;
	default:
		PDEBUG("%i bpp not supported by yuv2rgb", bpp);
		ret = -EINVAL;
		goto fail;
	}
	for (i=0; i<256; i++) {
		md->table_rV[i] = (((u8 *)table_r) + entry_size * div_round(crv * (i-128), 76309));
		md->table_gU[i] = (((u8 *)table_g) + entry_size * div_round(cgu * (i-128), 76309));
		md->table_gV[i] = entry_size * div_round(cgv * (i-128), 76309);
		md->table_bU[i] = (((u8 *)table_b) + entry_size * div_round(cbu * (i-128), 76309));
	}
	ret = 0;
fail:	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_init()=%i done", ret);
	if (PARANOID) memset(table_Y, POISON_VAL, table_Y_size);
	kfree(table_Y);
	return ret;
}
/* }}} */
/* {{{ [fold] qc_mjpeg_yuv2rgb_exit(struct qc_mjpeg_data *md) */
static inline void qc_mjpeg_yuv2rgb_exit(struct qc_mjpeg_data *md)
{
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_exit(md=%p)",md);
	kfree(md->table);
	POISON(md->table);
	POISON(md->yuv2rgb_func);
	POISON(md->table_rV);
	POISON(md->table_gU);
	POISON(md->table_bU);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_yuv2rgb_exit() done");
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_mjpeg_idct:    MJPEG decoding: Inverse DCT routines ************************* */
/**********************************************************/
/* inverse two dimensional DCT, Chen-Wang algorithm */
/* (cf. IEEE ASSP-32, pp. 803-816, Aug. 1984) */
/* 32-bit integer arithmetic (8 bit coefficients) */
/* 11 mults, 29 adds per DCT */
/* sE, 18.8.91 */
/**********************************************************/
/* coefficients extended to 12 bit for IEEE1180-1990 */
/* compliance sE, 2.1.94 */
/**********************************************************/

/* this code assumes >> to be a two's-complement arithmetic */
/* right shift: (-2)>>1 == -1 , (-3)>>1 == -2 */

#define W1 2841		/* 2048*sqrt (2)*cos (1*pi/16) */
#define W2 2676		/* 2048*sqrt (2)*cos (2*pi/16) */
#define W3 2408		/* 2048*sqrt (2)*cos (3*pi/16) */
#define W5 1609		/* 2048*sqrt (2)*cos (5*pi/16) */
#define W6 1108		/* 2048*sqrt (2)*cos (6*pi/16) */
#define W7 565		/* 2048*sqrt (2)*cos (7*pi/16) */

/* {{{ [fold] qc_mjpeg_idct_row(s16 *block) */
/* row (horizontal) IDCT
 *
 * 7 pi 1
 * dst[k] = sum c[l] * src[l] * cos ( -- * ( k + - ) * l )
 * l=0 8 2
 *
 * where: c[0] = 128
 * c[1..7] = 128*sqrt (2)
 */
static void inline qc_mjpeg_idct_row(s16 *block)
{
	int x0, x1, x2, x3, x4, x5, x6, x7, x8;

	x1 = block[4] << 11;
	x2 = block[6];
	x3 = block[2];
	x4 = block[1];
	x5 = block[7];
	x6 = block[5];
	x7 = block[3];

	/* shortcut */
	if (! (x1 | x2 | x3 | x4 | x5 | x6 | x7)) {
		block[0] = block[1] = block[2] = block[3] = block[4] =
			block[5] = block[6] = block[7] = block[0]<<3;
		return;
	}

	x0 = (block[0] << 11) + 128; /* for proper rounding in the fourth stage */

	/* first stage */
	x8 = W7 * (x4 + x5);
	x4 = x8 + (W1 - W7) * x4;
	x5 = x8 - (W1 + W7) * x5;
	x8 = W3 * (x6 + x7);
	x6 = x8 - (W3 - W5) * x6;
	x7 = x8 - (W3 + W5) * x7;

	/* second stage */
	x8 = x0 + x1;
	x0 -= x1;
	x1 = W6 * (x3 + x2);
	x2 = x1 - (W2 + W6) * x2;
	x3 = x1 + (W2 - W6) * x3;
	x1 = x4 + x6;
	x4 -= x6;
	x6 = x5 + x7;
	x5 -= x7;

	/* third stage */
	x7 = x8 + x3;
	x8 -= x3;
	x3 = x0 + x2;
	x0 -= x2;
	x2 = (181 * (x4 + x5) + 128) >> 8;
	x4 = (181 * (x4 - x5) + 128) >> 8;

	/* fourth stage */
	block[0] = (x7 + x1) >> 8;
	block[1] = (x3 + x2) >> 8;
	block[2] = (x0 + x4) >> 8;
	block[3] = (x8 + x6) >> 8;
	block[4] = (x8 - x6) >> 8;
	block[5] = (x0 - x4) >> 8;
	block[6] = (x3 - x2) >> 8;
	block[7] = (x7 - x1) >> 8;
}
/* }}} */
/* {{{ [fold] qc_mjpeg_idct_col(s16 *block) */
/* column (vertical) IDCT
 *
 * 7 pi 1
 * dst[8*k] = sum c[l] * src[8*l] * cos ( -- * ( k + - ) * l )
 * l=0 8 2
 *
 * where: c[0] = 1/1024
 * c[1..7] = (1/1024)*sqrt (2)
 */
static void inline qc_mjpeg_idct_col(s16 *block)
{
	int x0, x1, x2, x3, x4, x5, x6, x7, x8;

	/* shortcut */
	x1 = block [8*4] << 8;
	x2 = block [8*6];
	x3 = block [8*2];
	x4 = block [8*1];
	x5 = block [8*7];
	x6 = block [8*5];
	x7 = block [8*3];
#if 0
	if (! (x1 | x2 | x3 | x4 | x5 | x6 | x7 )) {
		block[8*0] = block[8*1] = block[8*2] = block[8*3] = block[8*4] =
			block[8*5] = block[8*6] = block[8*7] = (block[8*0] + 32) >> 6;
		return;
	}
#endif
	x0 = (block[8*0] << 8) + 8192;

	/* first stage */
	x8 = W7 * (x4 + x5) + 4;
	x4 = (x8 + (W1 - W7) * x4) >> 3;
	x5 = (x8 - (W1 + W7) * x5) >> 3;
	x8 = W3 * (x6 + x7) + 4;
	x6 = (x8 - (W3 - W5) * x6) >> 3;
	x7 = (x8 - (W3 + W5) * x7) >> 3;

	/* second stage */
	x8 = x0 + x1;
	x0 -= x1;
	x1 = W6 * (x3 + x2) + 4;
	x2 = (x1 - (W2 + W6) * x2) >> 3;
	x3 = (x1 + (W2 - W6) * x3) >> 3;
	x1 = x4 + x6;
	x4 -= x6;
	x6 = x5 + x7;
	x5 -= x7;

	/* third stage */
	x7 = x8 + x3;
	x8 -= x3;
	x3 = x0 + x2;
	x0 -= x2;
	x2 = (181 * (x4 + x5) + 128) >> 8;
	x4 = (181 * (x4 - x5) + 128) >> 8;

	/* fourth stage */
	block[8*0] = (x7 + x1) >> 14;
	block[8*1] = (x3 + x2) >> 14;
	block[8*2] = (x0 + x4) >> 14;
	block[8*3] = (x8 + x6) >> 14;
	block[8*4] = (x8 - x6) >> 14;
	block[8*5] = (x0 - x4) >> 14;
	block[8*6] = (x3 - x2) >> 14;
	block[8*7] = (x7 - x1) >> 14;
}
/* }}} */
/* {{{ [fold] qc_mjpeg_idct(s16 *block, u8 *dest, int stride) */
/* Inverse discrete cosine transform block, store result to dest */
static void qc_mjpeg_idct(s16 *block, u8 *dest, int stride)
{
	int i;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_idct(block=%p,dest=%p,stride=%i)",block,dest,stride);
	for (i=0; i<8; i++) qc_mjpeg_idct_row(block + 8*i);
	for (i=0; i<8; i++) qc_mjpeg_idct_col(block + i);
	i = 8;
	do {
		/* The original code used lookup-tables instead of explicit
		 * comparisons (as CLIP is doing here). However, sometimes
		 * the values pointed outside the LUT which caused problems
		 * in the kernel driver. Thus, the LUTs are removed here. */
		dest[0] = CLIP(block[0],0,255);
		dest[1] = CLIP(block[1],0,255);
		dest[2] = CLIP(block[2],0,255);
		dest[3] = CLIP(block[3],0,255);
		dest[4] = CLIP(block[4],0,255);
		dest[5] = CLIP(block[5],0,255);
		dest[6] = CLIP(block[6],0,255);
		dest[7] = CLIP(block[7],0,255);
		dest += stride;
		block += 8;
	} while (--i);
}
/* }}} */

/* }}} */
/* {{{ [fold] **** MJPEG decoding: bitstream processing (structures and macros) * */
/* the idea is taken from zlib, but the bits are ordered the other way, so
 * I modified the code.
 * Variables:
 * p points to next unread byte in stream.
 * k number of bits read but not processed.
 * b contains the read but not processed bits in the k most significant bits.
 */
struct bitstream {
	u32 b;
	u8 *p;
	u8 *end;
	int k;
};

#define GETWORD(p) ((p)[0] << 8 | (p)[1])
#define NEEDBITS(b,k,p) \
  do { \
    if ((k) > 0) { \
      (b) |= GETWORD(p) << (k); \
      (p) += 2; \
      (k) -= 16; \
    } \
  } while(0)
#define DUMPBITS(b,k,j) do { (k) += (j); (b) <<= (j); } while (0)
#define BITVALUE(b,j) ((b)>>(32-(j)))
/* }}} */
/* {{{ [fold] **** qc_mjpeg_lvc:     MJPEG decoding: variable length code decoding **************** */

/* {{{ [fold] u8 shiftTables[18][64] */
static const u8 shiftTables[18][64] = {
	{2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
	{2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	 1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2 },
	{2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,
	 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2 },
	{2,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	 2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3 },
	{2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,
	 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3 },
	{2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
	 3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4 },
	{2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,
	 4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4 },
	{2,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
	 4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5 },
	{2,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,
	 6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6 },
	{2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2 },
	{2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	 2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3 },
	{2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,
	 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3 },
	{2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
	 3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4 },
	{2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,
	 4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4 },
	{2,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
	 4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5 },
	{2,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,
	 5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5 },
	{2,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
	 5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6 },
	{2,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,
	 7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7 },
};
/* }}} */
/* {{{ [fold] u8 shiftTblIndex[] */
static const u8 shiftTblIndex[] = {
	8, 17, 8, 16, 7, 16, 7, 15, 
	6, 15, 6, 14, 5, 14, 5, 13,
	4, 13, 4, 12, 3, 12, 3, 11,
	2, 11, 2, 10, 1,  9, 0,  9
};
/* }}} */
/* {{{ [fold] s16 scaleTable[64] */
static const s16 scaleTable[64] = {
	 8192, 16704, 16704, 17733, 17032, 17733, 18204, 18081,
	18081, 18204, 18724, 18561, 19195, 18561, 18724, 19265,
	19091, 19704, 19704, 19091, 19265, 21406, 19642, 20267,
	20228, 20267, 19642, 21406, 22725, 21826, 20852, 20805,
	20805, 20852, 21826, 22725, 23170, 23170, 21406, 21399,
	21406, 23170, 23170, 24597, 23785, 22017, 22017, 23785,
	24597, 25250, 24464, 22653, 24464, 25250, 25971, 25171,
	25171, 25971, 26722, 27969, 26722, 29691, 29691, 31520
};
/* }}} */
/* {{{ [fold] u8 scan_norm[64] */
static const u8 scan_norm[64] = {	/* Octals */
	000, 001, 010, 020, 011, 002, 003, 012, 
	021, 030, 040, 031, 022, 013, 004, 005, 
	014, 023, 032, 041, 050, 060, 051, 042,
	033, 024, 015, 006, 007, 016, 025, 034, 
	043, 052, 061, 070, 071, 062, 053, 044,
	035, 026, 017, 027, 036, 045, 054, 063, 
	072, 073, 064, 055, 046, 037, 047, 056, 
	065, 074, 075, 066, 057, 067, 076, 077
};
/* }}} */
/* {{{ [fold] hufftable[960] */
struct hufftable_entry {
	s16 value;
	u8  bits;
	u8  skip;
};
static const struct hufftable_entry hufftable[960] = {
	/* first level entries */
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{     1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{    -1,  3,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{     2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{    -2,  4,   1 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{ 32767,  4, 255 },
	{     1,  5,   2 },
	{     1,  5,   2 },
	{     1,  5,   2 },
	{     1,  5,   2 },
	{     1,  5,   2 },
	{     1,  5,   2 },
	{     1,  5,   2 },
	{     1,  5,   2 },
	{    -1,  5,   2 },
	{    -1,  5,   2 },
	{    -1,  5,   2 },
	{    -1,  5,   2 },
	{    -1,  5,   2 },
	{    -1,  5,   2 },
	{    -1,  5,   2 },
	{    -1,  5,   2 },
	{     3,  5,   1 },
	{     3,  5,   1 },
	{     3,  5,   1 },
	{     3,  5,   1 },
	{     3,  5,   1 },
	{     3,  5,   1 },
	{     3,  5,   1 },
	{     3,  5,   1 },
	{    -3,  5,   1 },
	{    -3,  5,   1 },
	{    -3,  5,   1 },
	{    -3,  5,   1 },
	{    -3,  5,   1 },
	{    -3,  5,   1 },
	{    -3,  5,   1 },
	{    -3,  5,   1 },
	{     4,  5,   1 },
	{     4,  5,   1 },
	{     4,  5,   1 },
	{     4,  5,   1 },
	{     4,  5,   1 },
	{     4,  5,   1 },
	{     4,  5,   1 },
	{     4,  5,   1 },
	{    -4,  5,   1 },
	{    -4,  5,   1 },
	{    -4,  5,   1 },
	{    -4,  5,   1 },
	{    -4,  5,   1 },
	{    -4,  5,   1 },
	{    -4,  5,   1 },
	{    -4,  5,   1 },
	{     1,  6,   3 },
	{     1,  6,   3 },
	{     1,  6,   3 },
	{     1,  6,   3 },
	{    -1,  6,   3 },
	{    -1,  6,   3 },
	{    -1,  6,   3 },
	{    -1,  6,   3 },
	{     2,  6,   2 },
	{     2,  6,   2 },
	{     2,  6,   2 },
	{     2,  6,   2 },
	{    -2,  6,   2 },
	{    -2,  6,   2 },
	{    -2,  6,   2 },
	{    -2,  6,   2 },
	{     5,  6,   1 },
	{     5,  6,   1 },
	{     5,  6,   1 },
	{     5,  6,   1 },
	{    -5,  6,   1 },
	{    -5,  6,   1 },
	{    -5,  6,   1 },
	{    -5,  6,   1 },
	{     6,  6,   1 },
	{     6,  6,   1 },
	{     6,  6,   1 },
	{     6,  6,   1 },
	{    -6,  6,   1 },
	{    -6,  6,   1 },
	{    -6,  6,   1 },
	{    -6,  6,   1 },
	{     1,  7,   4 },
	{     1,  7,   4 },
	{    -1,  7,   4 },
	{    -1,  7,   4 },
	{     1,  7,   5 },
	{     1,  7,   5 },
	{    -1,  7,   5 },
	{    -1,  7,   5 },
	{     7,  7,   1 },
	{     7,  7,   1 },
	{    -7,  7,   1 },
	{    -7,  7,   1 },
	{     8,  7,   1 },
	{     8,  7,   1 },
	{    -8,  7,   1 },
	{    -8,  7,   1 },
	{     1,  8,   6 },
	{    -1,  8,   6 },
	{     1,  8,   7 },
	{    -1,  8,   7 },
	{     2,  8,   3 },
	{    -2,  8,   3 },
	{     3,  8,   2 },
	{    -3,  8,   2 },
	{     4,  8,   2 },
	{    -4,  8,   2 },
	{     9,  8,   1 },
	{    -9,  8,   1 },
	{    10,  8,   1 },
	{   -10,  8,   1 },
	{    11,  8,   1 },
	{   -11,  8,   1 },
	{   256,  9,  99 },
	{   258,  9,  99 },
	{   260,  9,  99 },
	{   262,  9,  99 },
	{   264,  9,  99 },
	{   266,  9,  99 },
	{   268,  9,  99 },
	{   270,  9,  99 },
	{   272,  9,  99 },
	{   274,  9,  99 },
	{   276,  9,  99 },
	{   278,  9,  99 },
	{   280,  9,  99 },
	{   282,  9,  99 },
	{   284,  9,  99 },
	{   286,  9,  99 },
	{   288, 10,  99 },
	{   292, 10,  99 },
	{   296, 10,  99 },
	{   300, 10,  99 },
	{   304, 10,  99 },
	{   308, 10,  99 },
	{   312, 10,  99 },
	{   316, 10,  99 },
	{   320, 11,  99 },
	{   328, 11,  99 },
	{   336, 12,  99 },
	{   352, 13,  99 },
	{   384, 13,  99 },
	{   416, 13,  99 },
	{   448, 16,  99 },
	{   704, 16,  99 },
	/* indirect entries */
	{     1,  9,   8 },
	{    -1,  9,   8 },
	{     1,  9,   9 },
	{    -1,  9,   9 },
	{     1,  9,  10 },
	{    -1,  9,  10 },
	{     1,  9,  11 },
	{    -1,  9,  11 },
	{     2,  9,   4 },
	{    -2,  9,   4 },
	{     2,  9,   5 },
	{    -2,  9,   5 },
	{     3,  9,   3 },
	{    -3,  9,   3 },
	{     5,  9,   2 },
	{    -5,  9,   2 },
	{     6,  9,   2 },
	{    -6,  9,   2 },
	{     7,  9,   2 },
	{    -7,  9,   2 },
	{    12,  9,   1 },
	{   -12,  9,   1 },
	{    13,  9,   1 },
	{   -13,  9,   1 },
	{    14,  9,   1 },
	{   -14,  9,   1 },
	{    15,  9,   1 },
	{   -15,  9,   1 },
	{    16,  9,   1 },
	{   -16,  9,   1 },
	{    17,  9,   1 },
	{   -17,  9,   1 },
	{     1, 10,  12 },
	{    -1, 10,  12 },
	{     1, 10,  13 },
	{    -1, 10,  13 },
	{     1, 10,  14 },
	{    -1, 10,  14 },
	{     1, 10,  15 },
	{    -1, 10,  15 },
	{     2, 10,   6 },
	{    -2, 10,   6 },
	{     2, 10,   7 },
	{    -2, 10,   7 },
	{     3, 10,   4 },
	{    -3, 10,   4 },
	{     3, 10,   5 },
	{    -3, 10,   5 },
	{     4, 10,   3 },
	{    -4, 10,   3 },
	{     5, 10,   3 },
	{    -5, 10,   3 },
	{     8, 10,   2 },
	{    -8, 10,   2 },
	{    18, 10,   1 },
	{   -18, 10,   1 },
	{    19, 10,   1 },
	{   -19, 10,   1 },
	{    20, 10,   1 },
	{   -20, 10,   1 },
	{    21, 10,   1 },
	{   -21, 10,   1 },
	{    22, 10,   1 },
	{   -22, 10,   1 },
	{     3, 11,   6 },
	{    -3, 11,   6 },
	{     4, 11,   4 },
	{    -4, 11,   4 },
	{     5, 11,   4 },
	{    -5, 11,   4 },
	{     6, 11,   3 },
	{    -6, 11,   3 },
	{     9, 11,   2 },
	{    -9, 11,   2 },
	{    10, 11,   2 },
	{   -10, 11,   2 },
	{    11, 11,   2 },
	{   -11, 11,   2 },
	{     0, 11,   1 },
	{     0, 11,   2 },
	{     3, 12,   7 },
	{    -3, 12,   7 },
	{     4, 12,   5 },
	{    -4, 12,   5 },
	{     6, 12,   4 },
	{    -6, 12,   4 },
	{    12, 12,   2 },
	{   -12, 12,   2 },
	{    13, 12,   2 },
	{   -13, 12,   2 },
	{    14, 12,   2 },
	{   -14, 12,   2 },
	{     0, 12,   3 },
	{     0, 12,   4 },
	{     0, 12,   5 },
	{     0, 12,   6 },
	{     2, 13,   8 },
	{    -2, 13,   8 },
	{     2, 13,   9 },
	{    -2, 13,   9 },
	{     2, 13,  10 },
	{    -2, 13,  10 },
	{     2, 13,  11 },
	{    -2, 13,  11 },
	{     3, 13,   8 },
	{    -3, 13,   8 },
	{     3, 13,   9 },
	{    -3, 13,   9 },
	{     5, 13,   5 },
	{    -5, 13,   5 },
	{     7, 13,   4 },
	{    -7, 13,   4 },
	{     7, 13,   3 },
	{    -7, 13,   3 },
	{     8, 13,   3 },
	{    -8, 13,   3 },
	{     9, 13,   3 },
	{    -9, 13,   3 },
	{    10, 13,   3 },
	{   -10, 13,   3 },
	{    11, 13,   3 },
	{   -11, 13,   3 },
	{    15, 13,   2 },
	{   -15, 13,   2 },
	{    16, 13,   2 },
	{   -16, 13,   2 },
	{    17, 13,   2 },
	{   -17, 13,   2 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{     0, 13,   7 },
	{     0, 13,   8 },
	{     0, 13,   9 },
	{     0, 13,  10 },
	{     0, 13,  11 },
	{     0, 13,  12 },
	{     0, 13,  13 },
	{     0, 13,  14 },
	{     0, 13,  15 },
	{     0, 13,  16 },
	{     0, 13,  17 },
	{     0, 13,  18 },
	{     0, 13,  19 },
	{     0, 13,  20 },
	{     0, 13,  21 },
	{     0, 13,  22 },
	{     0, 13,  23 },
	{     0, 13,  24 },
	{     0, 13,  25 },
	{     0, 13,  26 },
	{     0, 13,  27 },
	{     0, 13,  28 },
	{     0, 13,  29 },
	{     0, 13,  30 },
	{     0, 13,  31 },
	{     0, 13,  32 },
	{     0, 13,  33 },
	{     0, 13,  34 },
	{     0, 13,  35 },
	{     0, 13,  36 },
	{     0, 13,  37 },
	{     0, 13,  38 },
	{     0, 13,  39 },
	{     0, 13,  40 },
	{     0, 13,  41 },
	{     0, 13,  42 },
	{     0, 13,  43 },
	{     0, 13,  44 },
	{     0, 13,  45 },
	{     0, 13,  46 },
	{     0, 13,  47 },
	{     0, 13,  48 },
	{     0, 13,  49 },
	{     0, 13,  50 },
	{     0, 13,  51 },
	{     0, 13,  52 },
	{     0, 13,  53 },
	{     0, 13,  54 },
	{     0, 13,  55 },
	{     0, 13,  56 },
	{     0, 13,  57 },
	{     0, 13,  58 },
	{     0, 13,  59 },
	{     0, 13,  60 },
	{     0, 13,  61 },
	{     0, 13,  62 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{ 32767,  0, 255 },
	{    23, 16,   1 },
	{   -23, 16,   1 },
	{    24, 16,   1 },
	{   -24, 16,   1 },
	{    25, 16,   1 },
	{   -25, 16,   1 },
	{    26, 16,   1 },
	{   -26, 16,   1 },
	{    27, 16,   1 },
	{   -27, 16,   1 },
	{    28, 16,   1 },
	{   -28, 16,   1 },
	{    29, 16,   1 },
	{   -29, 16,   1 },
	{    30, 16,   1 },
	{   -30, 16,   1 },
	{    31, 16,   1 },
	{   -31, 16,   1 },
	{    32, 16,   1 },
	{   -32, 16,   1 },
	{    33, 16,   1 },
	{   -33, 16,   1 },
	{    34, 16,   1 },
	{   -34, 16,   1 },
	{    35, 16,   1 },
	{   -35, 16,   1 },
	{    36, 16,   1 },
	{   -36, 16,   1 },
	{    37, 16,   1 },
	{   -37, 16,   1 },
	{    38, 16,   1 },
	{   -38, 16,   1 },
	{    39, 16,   1 },
	{   -39, 16,   1 },
	{    40, 16,   1 },
	{   -40, 16,   1 },
	{    41, 16,   1 },
	{   -41, 16,   1 },
	{    42, 16,   1 },
	{   -42, 16,   1 },
	{    43, 16,   1 },
	{   -43, 16,   1 },
	{    44, 16,   1 },
	{   -44, 16,   1 },
	{    45, 16,   1 },
	{   -45, 16,   1 },
	{    46, 16,   1 },
	{   -46, 16,   1 },
	{    47, 16,   1 },
	{   -47, 16,   1 },
	{    48, 16,   1 },
	{   -48, 16,   1 },
	{    49, 16,   1 },
	{   -49, 16,   1 },
	{    50, 16,   1 },
	{   -50, 16,   1 },
	{    51, 16,   1 },
	{   -51, 16,   1 },
	{    52, 16,   1 },
	{   -52, 16,   1 },
	{    53, 16,   1 },
	{   -53, 16,   1 },
	{    54, 16,   1 },
	{   -54, 16,   1 },
	{    55, 16,   1 },
	{   -55, 16,   1 },
	{    56, 16,   1 },
	{   -56, 16,   1 },
	{    57, 16,   1 },
	{   -57, 16,   1 },
	{    58, 16,   1 },
	{   -58, 16,   1 },
	{    59, 16,   1 },
	{   -59, 16,   1 },
	{    60, 16,   1 },
	{   -60, 16,   1 },
	{    61, 16,   1 },
	{   -61, 16,   1 },
	{    62, 16,   1 },
	{   -62, 16,   1 },
	{    63, 16,   1 },
	{   -63, 16,   1 },
	{    64, 16,   1 },
	{   -64, 16,   1 },
	{    65, 16,   1 },
	{   -65, 16,   1 },
	{    66, 16,   1 },
	{   -66, 16,   1 },
	{    67, 16,   1 },
	{   -67, 16,   1 },
	{    68, 16,   1 },
	{   -68, 16,   1 },
	{    69, 16,   1 },
	{   -69, 16,   1 },
	{    70, 16,   1 },
	{   -70, 16,   1 },
	{    71, 16,   1 },
	{   -71, 16,   1 },
	{    72, 16,   1 },
	{   -72, 16,   1 },
	{    73, 16,   1 },
	{   -73, 16,   1 },
	{    74, 16,   1 },
	{   -74, 16,   1 },
	{    75, 16,   1 },
	{   -75, 16,   1 },
	{    76, 16,   1 },
	{   -76, 16,   1 },
	{    77, 16,   1 },
	{   -77, 16,   1 },
	{    78, 16,   1 },
	{   -78, 16,   1 },
	{    79, 16,   1 },
	{   -79, 16,   1 },
	{    80, 16,   1 },
	{   -80, 16,   1 },
	{    81, 16,   1 },
	{   -81, 16,   1 },
	{    82, 16,   1 },
	{   -82, 16,   1 },
	{    83, 16,   1 },
	{   -83, 16,   1 },
	{    84, 16,   1 },
	{   -84, 16,   1 },
	{    85, 16,   1 },
	{   -85, 16,   1 },
	{    86, 16,   1 },
	{   -86, 16,   1 },
	{    87, 16,   1 },
	{   -87, 16,   1 },
	{    88, 16,   1 },
	{   -88, 16,   1 },
	{    89, 16,   1 },
	{   -89, 16,   1 },
	{    90, 16,   1 },
	{   -90, 16,   1 },
	{    91, 16,   1 },
	{   -91, 16,   1 },
	{    92, 16,   1 },
	{   -92, 16,   1 },
	{    93, 16,   1 },
	{   -93, 16,   1 },
	{    94, 16,   1 },
	{   -94, 16,   1 },
	{    95, 16,   1 },
	{   -95, 16,   1 },
	{    96, 16,   1 },
	{   -96, 16,   1 },
	{    97, 16,   1 },
	{   -97, 16,   1 },
	{    98, 16,   1 },
	{   -98, 16,   1 },
	{    99, 16,   1 },
	{   -99, 16,   1 },
	{   100, 16,   1 },
	{  -100, 16,   1 },
	{   101, 16,   1 },
	{  -101, 16,   1 },
	{   102, 16,   1 },
	{  -102, 16,   1 },
	{   103, 16,   1 },
	{  -103, 16,   1 },
	{   104, 16,   1 },
	{  -104, 16,   1 },
	{   105, 16,   1 },
	{  -105, 16,   1 },
	{   106, 16,   1 },
	{  -106, 16,   1 },
	{   107, 16,   1 },
	{  -107, 16,   1 },
	{   108, 16,   1 },
	{  -108, 16,   1 },
	{   109, 16,   1 },
	{  -109, 16,   1 },
	{   110, 16,   1 },
	{  -110, 16,   1 },
	{   111, 16,   1 },
	{  -111, 16,   1 },
	{   112, 16,   1 },
	{  -112, 16,   1 },
	{   113, 16,   1 },
	{  -113, 16,   1 },
	{   114, 16,   1 },
	{  -114, 16,   1 },
	{   115, 16,   1 },
	{  -115, 16,   1 },
	{   116, 16,   1 },
	{  -116, 16,   1 },
	{   117, 16,   1 },
	{  -117, 16,   1 },
	{   118, 16,   1 },
	{  -118, 16,   1 },
	{   119, 16,   1 },
	{  -119, 16,   1 },
	{   120, 16,   1 },
	{  -120, 16,   1 },
	{   121, 16,   1 },
	{  -121, 16,   1 },
	{   122, 16,   1 },
	{  -122, 16,   1 },
	{   123, 16,   1 },
	{  -123, 16,   1 },
	{   124, 16,   1 },
	{  -124, 16,   1 },
	{   125, 16,   1 },
	{  -125, 16,   1 },
	{   126, 16,   1 },
	{  -126, 16,   1 },
	{   127, 16,   1 },
	{  -127, 16,   1 },
	{   128, 16,   1 },
	{  -128, 16,   1 },
	{   129, 16,   1 },
	{  -129, 16,   1 },
	{   130, 16,   1 },
	{  -130, 16,   1 },
	{   131, 16,   1 },
	{  -131, 16,   1 },
	{   132, 16,   1 },
	{  -132, 16,   1 },
	{   133, 16,   1 },
	{  -133, 16,   1 },
	{   134, 16,   1 },
	{  -134, 16,   1 },
	{   135, 16,   1 },
	{  -135, 16,   1 },
	{   136, 16,   1 },
	{  -136, 16,   1 },
	{   137, 16,   1 },
	{  -137, 16,   1 },
	{   138, 16,   1 },
	{  -138, 16,   1 },
	{   139, 16,   1 },
	{  -139, 16,   1 },
	{   140, 16,   1 },
	{  -140, 16,   1 },
	{   141, 16,   1 },
	{  -141, 16,   1 },
	{   142, 16,   1 },
	{  -142, 16,   1 },
	{   143, 16,   1 },
	{  -143, 16,   1 },
	{   144, 16,   1 },
	{  -144, 16,   1 },
	{   145, 16,   1 },
	{  -145, 16,   1 },
	{   146, 16,   1 },
	{  -146, 16,   1 },
	{   147, 16,   1 },
	{  -147, 16,   1 },
	{   148, 16,   1 },
	{  -148, 16,   1 },
	{   149, 16,   1 },
	{  -149, 16,   1 },
	{   150, 16,   1 },
	{  -150, 16,   1 },
	{   151, 16,   1 },
	{  -151, 16,   1 },
	{   152, 16,   1 },
	{  -152, 16,   1 },
	{   153, 16,   1 },
	{  -153, 16,   1 },
	{   154, 16,   1 },
	{  -154, 16,   1 },
	{   155, 16,   1 },
	{  -155, 16,   1 },
	{   156, 16,   1 },
	{  -156, 16,   1 },
	{   157, 16,   1 },
	{  -157, 16,   1 },
	{   158, 16,   1 },
	{  -158, 16,   1 },
	{   159, 16,   1 },
	{  -159, 16,   1 },
	{   160, 16,   1 },
	{  -160, 16,   1 },
	{   161, 16,   1 },
	{  -161, 16,   1 },
	{   162, 16,   1 },
	{  -162, 16,   1 },
	{   163, 16,   1 },
	{  -163, 16,   1 },
	{   164, 16,   1 },
	{  -164, 16,   1 },
	{   165, 16,   1 },
	{  -165, 16,   1 },
	{   166, 16,   1 },
	{  -166, 16,   1 },
	{   167, 16,   1 },
	{  -167, 16,   1 },
	{   168, 16,   1 },
	{  -168, 16,   1 },
	{   169, 16,   1 },
	{  -169, 16,   1 },
	{   170, 16,   1 },
	{  -170, 16,   1 },
	{   171, 16,   1 },
	{  -171, 16,   1 },
	{   172, 16,   1 },
	{  -172, 16,   1 },
	{   173, 16,   1 },
	{  -173, 16,   1 },
	{   174, 16,   1 },
	{  -174, 16,   1 },
	{   175, 16,   1 },
	{  -175, 16,   1 },
	{   176, 16,   1 },
	{  -176, 16,   1 },
	{   177, 16,   1 },
	{  -177, 16,   1 },
	{   178, 16,   1 },
	{  -178, 16,   1 },
	{   179, 16,   1 },
	{  -179, 16,   1 },
	{   180, 16,   1 },
	{  -180, 16,   1 },
	{   181, 16,   1 },
	{  -181, 16,   1 },
	{   182, 16,   1 },
	{  -182, 16,   1 },
	{   183, 16,   1 },
	{  -183, 16,   1 },
	{   184, 16,   1 },
	{  -184, 16,   1 },
	{   185, 16,   1 },
	{  -185, 16,   1 },
	{   186, 16,   1 },
	{  -186, 16,   1 },
	{   187, 16,   1 },
	{  -187, 16,   1 },
	{   188, 16,   1 },
	{  -188, 16,   1 },
	{   189, 16,   1 },
	{  -189, 16,   1 },
	{   190, 16,   1 },
	{  -190, 16,   1 },
	{   191, 16,   1 },
	{  -191, 16,   1 },
	{   192, 16,   1 },
	{  -192, 16,   1 },
	{   193, 16,   1 },
	{  -193, 16,   1 },
	{   194, 16,   1 },
	{  -194, 16,   1 },
	{   195, 16,   1 },
	{  -195, 16,   1 },
	{   196, 16,   1 },
	{  -196, 16,   1 },
	{   197, 16,   1 },
	{  -197, 16,   1 },
	{   198, 16,   1 },
	{  -198, 16,   1 },
	{   199, 16,   1 },
	{  -199, 16,   1 },
	{   200, 16,   1 },
	{  -200, 16,   1 },
	{   201, 16,   1 },
	{  -201, 16,   1 },
	{   202, 16,   1 },
	{  -202, 16,   1 },
	{   203, 16,   1 },
	{  -203, 16,   1 },
	{   204, 16,   1 },
	{  -204, 16,   1 },
	{   205, 16,   1 },
	{  -205, 16,   1 },
	{   206, 16,   1 },
	{  -206, 16,   1 },
	{   207, 16,   1 },
	{  -207, 16,   1 },
	{   208, 16,   1 },
	{  -208, 16,   1 },
	{   209, 16,   1 },
	{  -209, 16,   1 },
	{   210, 16,   1 },
	{  -210, 16,   1 },
	{   211, 16,   1 },
	{  -211, 16,   1 },
	{   212, 16,   1 },
	{  -212, 16,   1 },
	{   213, 16,   1 },
	{  -213, 16,   1 },
	{   214, 16,   1 },
	{  -214, 16,   1 },
	{   215, 16,   1 },
	{  -215, 16,   1 },
	{   216, 16,   1 },
	{  -216, 16,   1 },
	{   217, 16,   1 },
	{  -217, 16,   1 },
	{   218, 16,   1 },
	{  -218, 16,   1 },
	{   219, 16,   1 },
	{  -219, 16,   1 },
	{   220, 16,   1 },
	{  -220, 16,   1 },
	{   221, 16,   1 },
	{  -221, 16,   1 },
	{   222, 16,   1 },
	{  -222, 16,   1 },
	{   223, 16,   1 },
	{  -223, 16,   1 },
	{   224, 16,   1 },
	{  -224, 16,   1 },
	{   225, 16,   1 },
	{  -225, 16,   1 },
	{   226, 16,   1 },
	{  -226, 16,   1 },
	{   227, 16,   1 },
	{  -227, 16,   1 },
	{   228, 16,   1 },
	{  -228, 16,   1 },
	{   229, 16,   1 },
	{  -229, 16,   1 },
	{   230, 16,   1 },
	{  -230, 16,   1 },
	{   231, 16,   1 },
	{  -231, 16,   1 },
	{   232, 16,   1 },
	{  -232, 16,   1 },
	{   233, 16,   1 },
	{  -233, 16,   1 },
	{   234, 16,   1 },
	{  -234, 16,   1 },
	{   235, 16,   1 },
	{  -235, 16,   1 },
	{   236, 16,   1 },
	{  -236, 16,   1 },
	{   237, 16,   1 },
	{  -237, 16,   1 },
	{   238, 16,   1 },
	{  -238, 16,   1 },
	{   239, 16,   1 },
	{  -239, 16,   1 },
	{   240, 16,   1 },
	{  -240, 16,   1 },
	{   241, 16,   1 },
	{  -241, 16,   1 },
	{   242, 16,   1 },
	{  -242, 16,   1 },
	{   243, 16,   1 },
	{  -243, 16,   1 },
	{   244, 16,   1 },
	{  -244, 16,   1 },
	{   245, 16,   1 },
	{  -245, 16,   1 },
	{   246, 16,   1 },
	{  -246, 16,   1 },
	{   247, 16,   1 },
	{  -247, 16,   1 },
	{   248, 16,   1 },
	{  -248, 16,   1 },
	{   249, 16,   1 },
	{  -249, 16,   1 },
	{   250, 16,   1 },
	{  -250, 16,   1 },
	{   251, 16,   1 },
	{  -251, 16,   1 },
	{   252, 16,   1 },
	{  -252, 16,   1 },
	{   253, 16,   1 },
	{  -253, 16,   1 },
	{   254, 16,   1 },
	{  -254, 16,   1 },
	{   255, 16,   1 },
	{  -255, 16,   1 }
};
/* }}} */
/* {{{ [fold] qc_mjpeg_lvc_decode_block(struct bitstream *bitsrc, s16 *output, int blockval) */
static inline void qc_mjpeg_lvc_decode_block(struct bitstream *bitsrc, s16 *output, int blockval)
{
	u32 b;
	u8 *p;
	int k;
	int value, skip, bits;
	struct hufftable_entry entry;
	int offset = 0;
	const u8 *shiftPtr;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_lvc_decode_block(bitsrc=%p, output=%p, blockval=%i)", bitsrc, output, blockval);
	b = bitsrc->b;
	k = bitsrc->k;
	p = bitsrc->p;
	memset(output, 0, 64 * sizeof(s16));
	if (blockval!=7) PDEBUG("blockval=%i",blockval);
	NEEDBITS(b,k,p);
	shiftPtr = shiftTables[shiftTblIndex[2*blockval+BITVALUE(b,1)]];
	DUMPBITS(b,k,1);
	value = BITVALUE(((signed)b),10);
	DUMPBITS(b,k,10);
	do {
		value = ((value << shiftPtr[offset]) * scaleTable[offset]) >> 14;
		output[scan_norm[offset]] = value;
		NEEDBITS(b,k,p);
		entry = hufftable[BITVALUE(b,8)];
		bits = entry.bits;
		if (bits > 8) {
			entry = hufftable[entry.value + ((b & 0x00ffffff) >> (32 - bits))];
			if (PARANOID && entry.bits!=bits) {
				PDEBUG("entry.bits!=bits shouldn't happen");
				bits = entry.bits;
			}
		}
		DUMPBITS(b,k,bits);
		skip = entry.skip;
		value = entry.value;
		offset += skip;
	} while (offset < 64);
	bitsrc->b = b;
	bitsrc->k = k;
	bitsrc->p = p;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_lvc_decode_block() exit");
}
/* }}} */

/* {{{ [fold] struct blockorder */
struct blockorder {
	char widthPad;		/* pad width to multiple of this */
	char heightPad;		/* pad height to multiple of this */
	char uvWshift;		/* shift width by this to get width of U/V image */
	char uvHshift;		/* dito for height */
	char blockWidth[2];	/* width of a block for each pass*/
	char subblockCount[2];	/* number of sub block in a block for each pass */
	u32 subblockMap[2];
};
static const struct blockorder order_I420 = {
	32, 16, 1, 1,
	{ 32, 16 }, { 4, 4 },
	{ 0x00, 0x90 }
};
#if 0
static const struct blockorder order_L422 = {
	16, 16, 1, 0, 
	{ 16, 16 }, { 4, 4 },
	{ 0x90, 0x90 }
};

static const struct blockorder order_L410 = {
	64, 16, 2, 1,
	{ 32, 64 }, { 4, 12 },
	{ 0x00, 0x909000 }
};
#endif
/* }}} */
/* {{{ [fold] qc_mjpeg_lvc_decode() */
/* Decode given compressed image to YUV image. Return error code if bad data */
static int qc_mjpeg_lvc_decode(u8 *outY, u8 *outU, u8 *outV,
	u8 *input, u32 length, unsigned int width, unsigned int height)
{
	struct bitstream stream;
	const struct blockorder *blkorder;

	unsigned int blockx, blocky;
	unsigned int pass, subblock, blockval = 0;
	unsigned int blocknr = 0;
	unsigned int uvWidth;

	s16 blockbuffer[64];

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_lvc_decode(outY=%p, outU=%p, outV=%p)", outY, outU, outV);
	stream.b   = 0;
	stream.k   = 16;
	stream.p   = input;
	stream.end = input+length;

	blkorder = &order_I420;		/* Select compression type */

	uvWidth = (width >> blkorder->uvWshift);

	if ((width & (blkorder->widthPad - 1)) || (height & (blkorder->heightPad - 1))) {
		PDEBUG("something's wrong");
		return -EILSEQ;
	}

	for (blocky=0; blocky<height; blocky+=blkorder->heightPad) {
		for (pass = 0; pass < 2; pass++) {
			int blockwidth    = blkorder->blockWidth[pass];
			int subblockcount = blkorder->subblockCount[pass];
			u32 map = blkorder->subblockMap[pass];
			for (blockx=0; blockx<width; blockx+=blockwidth) {
				u32 subblkmap = map;
				for (subblock=0; subblock<subblockcount; subblock++) {
					if ((blocknr++ & 3) == 0) {
						u32 b = stream.b;
						int k = stream.k;
						u8 *p = stream.p;

						NEEDBITS(b, k, p);

						/* Make sure from time to time that we don't read
						 * far too much.  I hope it is okay to read a bit
						 * beyond the end
						 */
						if (p > stream.end) {
							PDEBUG("p>stream.end");
							return -EILSEQ;
						}

						blockval = BITVALUE(b, 4);
						DUMPBITS(b,k,4);
						stream.b = b;
						stream.k = k;
						stream.p = p;
					}
					qc_mjpeg_lvc_decode_block(&stream, blockbuffer, blockval);
					blockbuffer[0] += 1024;
					switch (subblkmap & 3) {
					case 0:
						qc_mjpeg_idct(blockbuffer, outY, width);
						outY += 8;
						break;
					case 1:
						qc_mjpeg_idct(blockbuffer, outU, uvWidth);
						outU += 8;
						break;
					case 2:
						qc_mjpeg_idct(blockbuffer, outV, uvWidth);
						outV += 8;
						break;
					}
					subblkmap >>= 2;
				} /* for (subblock = 0; subblock < subblockcount; subblock++) */
			} /* for (blockx = 0; blockx < width; blockx += blockwidth) */
			outY += 7 * width;
			if (map) {
				outU += 7 * uvWidth;
				outV += 7 * uvWidth;
			}
		} /* for (pass = 0; pass < 2; pass++) */

		/* next block starts at next 4 byte boundary */
		stream.p -= (16 - stream.k) >> 3;  /* push back unread bits */
		stream.p += (input - stream.p) & 3;
		stream.k = 16;
		stream.b = 0;
	} /* for (blocky=0; blocky<height; blocky+=blkorder->heightPad) */

	if (stream.p != stream.end) {
		PDEBUG("stream.p != stream.end");
		return -EILSEQ;
	}
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_lvc_decode() done");
	return 0;
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_mjpeg:         Motion JPEG decoding main routines *************************** */

static const int qc_mjpeg_width  = 320;		/* Size of the compressed image */
static const int qc_mjpeg_height = 240;

/* {{{ [fold] qc_mjpeg_decode() */
/* Decode and uncompress given data, return error code if failure
 * src = points to compressed bitstream data
 * src_len = compressed data length in bytes
 * dst = decompressed image will be stored here, size 320x240 x bytes per pixel (2-4)
 */
int qc_mjpeg_decode(struct qc_mjpeg_data *md, unsigned char *src, int src_len, unsigned char *dst)
{
	int r;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_decode(src_len=%i,W=%i,H=%i,depth=%i)",src_len,qc_mjpeg_width,qc_mjpeg_height,md->depth);
	IDEBUG_TEST(*md);
	if (src_len >= 100000) {
		PDEBUG("long frame, length=%i", src_len);
		return -EILSEQ;
	}
	r = qc_mjpeg_lvc_decode(md->encY, md->encU, md->encV, src, src_len, qc_mjpeg_width, qc_mjpeg_height);
	if (r<0) {
		PRINTK(KERN_ERR,"frame corrupted, len=%i",src_len);
		return r;
	}
	qc_mjpeg_yuv2rgb(md, dst, md->encY, md->encU, md->encV, 
		qc_mjpeg_width, qc_mjpeg_height, 		/* Image size */
		qc_mjpeg_width * ((md->depth+1)/8),		/* RGB stride */
		qc_mjpeg_width,					/* Y stride */
		qc_mjpeg_width/2);				/* U and V stride */
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_decode() done");
	return 0;
}
/* }}} */
/* {{{ [fold] qc_mjpeg_init(struct qc_mjpeg_data *md, int depth, Bool tobgr) */
/* Initialize Motion JPEG decompression.
 * depth = bit depth of the decoded image, either 15=16,24 or 32
 * tobgr = use blue in the lowest address (red otherwise)
 */
int qc_mjpeg_init(struct qc_mjpeg_data *md, int depth, Bool tobgr)
{
	int r = -ENOMEM;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_init(depth=%i)",depth);
	md->depth = depth;
	/* Temporary buffers used for decoding the image (FIXME:too big for stack?) */
	/* Note: originally this allocated one extra byte for encY/U/V. I removed that. */
	md->encY = kmalloc(qc_mjpeg_width*qc_mjpeg_height, GFP_KERNEL);	
	if (!md->encY) goto fail1;
	md->encU = kmalloc(qc_mjpeg_width*qc_mjpeg_height/4, GFP_KERNEL);
	if (!md->encU) goto fail2;
	md->encV = kmalloc(qc_mjpeg_width*qc_mjpeg_height/4, GFP_KERNEL);
	if (!md->encV) goto fail3;
	if ((r=qc_mjpeg_yuv2rgb_init(md, depth, tobgr ? MODE_BGR : MODE_RGB))<0) goto fail4;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_init() done");
	IDEBUG_INIT(*md);
	return 0;

fail4:	kfree(md->encV);
fail3:	kfree(md->encU);
fail2:	kfree(md->encY);
fail1:	PDEBUG("failed qc_mjpeg_init() = %i", r);
	POISON(*md);
	return r;
}
/* }}} */
/* {{{ [fold] qc_mjpeg_exit(struct qc_mjpeg_data *md) */
/* Free up resources allocated for image decompression */
void qc_mjpeg_exit(struct qc_mjpeg_data *md)
{
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_exit()");
	IDEBUG_TEST(*md);
	qc_mjpeg_yuv2rgb_exit(md);
	kfree(md->encV);
	kfree(md->encU);
	kfree(md->encY);
	POISON(md->encV);
	POISON(md->encU);
	POISON(md->encY);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_mjpeg_exit() done");
	IDEBUG_EXIT(*md);
}
/* }}} */

/* }}} */

#else /* COMPRESS=0 */
int qc_mjpeg_decode(struct qc_mjpeg_data *md, unsigned char *src, int src_len, unsigned char *dst) { return -ENXIO; }
int qc_mjpeg_init(struct qc_mjpeg_data *md, int depth, Bool tobgr) { return -ENXIO; }
void qc_mjpeg_exit(struct qc_mjpeg_data *md) { }
#endif

/* End of file */
