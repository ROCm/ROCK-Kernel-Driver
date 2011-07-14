/*
 *           linux/drivers/video/bootsplash/bootsplash.c -
 *                 splash screen handling functions.
 *
 *	(w) 2001-2004 by Volker Poplawski, <volker@poplawski.de>,
 *			Stefan Reinauer, <stepan@suse.de>,
 *			Steffen Winterfeldt, <snwint@suse.de>,
 *			Michael Schroeder <mls@suse.de>
 *	    2009-2011 Egbert Eich <eich@suse.de>
 *
 *        Ideas & SuSE screen work by Ken Wimer, <wimer@suse.de>
 *
 *  For more information on this code check http://www.bootsplash.org/
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/vmalloc.h>
#include <linux/unistd.h>
#include <linux/syscalls.h>
#include <linux/console.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#include <asm/irq.h>
#include <asm/system.h>

#include "../console/fbcon.h"
#include <linux/bootsplash.h>
#include "decode-jpg.h"

#ifndef DEBUG
# define SPLASH_DEBUG(fmt, args...)
#else
# define SPLASH_DEBUG(fmt, args...) \
	printk(KERN_WARNING "%s: " fmt "\n", __func__, ##args)
#endif
extern signed char con2fb_map[MAX_NR_CONSOLES];

#define SPLASH_VERSION "3.2.0-2010/03/31"

/* These errors have to match fbcon-jpegdec.h */
static unsigned char *jpg_errors[] = {
	"no SOI found",
	"not 8 bit",
	"height mismatch",
	"width mismatch",
	"bad width or height",
	"too many COMPPs",
	"illegal HV",
	"quant table selector",
	"picture is not YCBCR 221111",
	"unknow CID in scan",
	"dct not sequential",
	"wrong marker",
	"no EOI",
	"bad tables",
	"depth mismatch",
	"scale error",
	"out of memory"
};

static int splash_usesilent;
static unsigned long splash_default = 0xf01;

static int jpeg_get(unsigned char *buf, unsigned char *pic,
		    int width, int height, int depth,
		    struct jpeg_decdata *decdata);
static int splash_look_for_jpeg(struct vc_data *vc, int width, int height);

static int __init splash_setup(char *options)
{
	splash_usesilent = 0;

	if (!strncmp("silent", options, 6)) {
		printk(KERN_INFO "bootsplash: silent mode.\n");
		splash_usesilent = 1;
		/* skip "silent," */
		if (strlen(options) == 6)
			return 0;
		options += 7;
	}
	if (!strncmp("verbose", options, 7)) {
		printk(KERN_INFO "bootsplash: verbose mode.\n");
		splash_usesilent = 0;
		return 0;
	}
	if (strict_strtoul(options, 0, &splash_default) == -EINVAL)
		splash_default = 0;

	return 0;
}

__setup("splash=", splash_setup);


static int splash_hasinter(unsigned char *buf, int num)
{
	unsigned char *bufend = buf + num * 12;
	while (buf < bufend) {
		if (buf[1] > 127)		/* inter? */
			return 1;
		buf += buf[3] > 127 ? 24 : 12;	/* blend? */
	}
	return 0;
}

static int boxextract(unsigned char *buf, unsigned short *dp,
		      unsigned char *cols, int *blendp)
{
	dp[0] = buf[0] | buf[1] << 8;
	dp[1] = buf[2] | buf[3] << 8;
	dp[2] = buf[4] | buf[5] << 8;
	dp[3] = buf[6] | buf[7] << 8;
	*(unsigned int *)(cols + 0) =
		*(unsigned int *)(cols + 4) =
		*(unsigned int *)(cols + 8) =
		*(unsigned int *)(cols + 12) = *(unsigned int *)(buf + 8);
	if (dp[1] > 32767) {
		dp[1] = ~dp[1];
		*(unsigned int *)(cols + 4) = *(unsigned int *)(buf + 12);
		*(unsigned int *)(cols + 8) = *(unsigned int *)(buf + 16);
		*(unsigned int *)(cols + 12) = *(unsigned int *)(buf + 20);
		*blendp = 1;
		return 24;
	}
	return 12;
}

static void boxit(unsigned char *pic, int bytes, unsigned char *buf, int num,
		  int percent, int xoff, int yoff, int overpaint, int octpp)
{
	int x, y, p, doblend, r, g, b, a, add;
	unsigned int i = 0;
	unsigned short data1[4];
	unsigned char cols1[16];
	unsigned short data2[4];
	unsigned char cols2[16];
	unsigned char *bufend;
	union pt picp;
	unsigned int stipple[32], sti, stin, stinn, stixs, stixe, stiys, stiye;
	int xs, xe, ys, ye, xo, yo;

	SPLASH_DEBUG();
	if (num == 0 || percent < -1)
		return;
	bufend = buf + num * 12;
	stipple[0] = 0xffffffff;
	stin = 1;
	stinn = 0;
	stixs = stixe = 0;
	stiys = stiye = 0;
	while (buf < bufend) {
		doblend = 0;
		buf += boxextract(buf, data1, cols1, &doblend);
		if (data1[0] == 32767 && data1[1] == 32767) {
			/* box stipple */
			if (stinn == 32)
				continue;
			if (stinn == 0) {
				stixs = data1[2];
				stixe = data1[3];
				stiys = stiye = 0;
			} else if (stinn == 4) {
				stiys = data1[2];
				stiye = data1[3];
			}
			stipple[stinn++] = (cols1[0]  << 24) |
				(cols1[1]  << 16) |
				(cols1[2]  << 8)  |
				cols1[3] ;
			stipple[stinn++] = (cols1[4]  << 24) |
				(cols1[5]  << 16) |
				(cols1[6]  << 8)  |
				cols1[7] ;
			stipple[stinn++] = (cols1[8]  << 24) |
				(cols1[9]  << 16) |
				(cols1[10] << 8)  |
				cols1[11] ;
			stipple[stinn++] = (cols1[12] << 24) |
				(cols1[13] << 16) |
				(cols1[14] << 8)  |
				cols1[15] ;
			stin = stinn;
			continue;
		}
		stinn = 0;
		if (data1[0] > 32767)
			buf += boxextract(buf, data2, cols2, &doblend);
		if (data1[0] == 32767 && data1[1] == 32766) {
			/* box copy */
			i = 12 * (short)data1[3];
			doblend = 0;
			i += boxextract(buf + i, data1, cols1, &doblend);
			if (data1[0] > 32767)
				boxextract(buf + i, data2, cols2, &doblend);
		}
		if (data1[0] == 32767)
			continue;
		if (data1[2] > 32767) {
			if (overpaint)
				continue;
			data1[2] = ~data1[2];
		}
		if (data1[3] > 32767) {
			if (percent == 65536)
				continue;
			data1[3] = ~data1[3];
		}
		if (data1[0] > 32767) {
			if (percent < 0)
				continue;
			data1[0] = ~data1[0];
			for (i = 0; i < 4; i++)
				data1[i] = (data1[i] * (65536 - percent)
					    + data2[i] * percent) >> 16;
			for (i = 0; i < 16; i++)
				cols1[i] = (cols1[i] * (65536 - percent)
					    + cols2[i] * percent) >> 16;
		}
		*(unsigned int *)cols2 = *(unsigned int *)cols1;
		a = cols2[3];
		if (a == 0 && !doblend)
			continue;

		if (stixs >= 32768) {
			xo = xs = (stixs ^ 65535) + data1[0];
			xe = stixe ? stixe + data1[0] : data1[2];
		} else if (stixe >= 32768) {
			xs = stixs ? data1[2] - stixs : data1[0];
			xe = data1[2] - (stixe ^ 65535);
			xo = xe + 1;
		} else {
			xo = xs = stixs;
			xe = stixe ? stixe : data1[2];
		}
		if (stiys >= 32768) {
			yo = ys = (stiys ^ 65535) + data1[1];
			ye = stiye ? stiye + data1[1] : data1[3];
		} else if (stiye >= 32768) {
			ys = stiys ? data1[3] - stiys : data1[1];
			ye = data1[3] - (stiye ^ 65535);
			yo = ye + 1;
		} else {
			yo = ys = stiys;
			ye = stiye ? stiye : data1[3];
		}
		xo = 32 - (xo & 31);
		yo = stin - (yo % stin);
		if (xs < data1[0])
			xs = data1[0];
		if (xe > data1[2])
			xe = data1[2];
		if (ys < data1[1])
			ys = data1[1];
		if (ye > data1[3])
			ye = data1[3];

		for (y = ys; y <= ye; y++) {
			sti = stipple[(y + yo) % stin];
			x = (xs + xo) & 31;
			if (x)
				sti = (sti << x) | (sti >> (32 - x));
			if (doblend) {
				p = data1[3] - data1[1];
				if (p != 0)
					p = ((y - data1[1]) << 16) / p;
				for (i = 0; i < 8; i++)
					cols2[i + 8] = (cols1[i] * (65536 - p)
							+ cols1[i + 8] * p)
						>> 16;
			}
			add = (xs & 1);
			add ^= (add ^ y) & 1 ? 1 : 3; /*2x2 ordered dithering*/
			picp.ub = (pic + (xs + xoff) * octpp
				   + (y + yoff) * bytes);
			for (x = xs; x <= xe; x++) {
				if (!(sti & 0x80000000)) {
					sti <<= 1;
					switch (octpp) {
					case 2:
						picp.us++;
						break;
					case 3:
						picp.ub += 3;
						break;
					case 4:
						picp.ul++;
						break;
					}
					add ^= 3;
					continue;
				}
				sti = (sti << 1) | 1;
				if (doblend) {
					p = data1[2] - data1[0];
					if (p != 0)
						p = ((x - data1[0]) << 16) / p;
					for (i = 0; i < 4; i++)
						cols2[i] = (cols2[i + 8] * (65536 - p)
							    + cols2[i + 12] * p)
							>> 16;
					a = cols2[3];
				}
				r = cols2[0];
				g = cols2[1];
				b = cols2[2];
#define CLAMP(x) ((x) >= 256 ? 255 : (x))
#define BLEND(x, v, a) ((x * (255 - a) + v * a) / 255)
				switch (octpp) {
				case 2:
					if (a != 255) {
						i = *picp.us;
						r = BLEND((i>>8 & 0xf8), r, a);
						g = BLEND((i>>3 & 0xfc), g, a);
						b = BLEND((i<<3 & 0xf8), b, a);
					}
					r += add * 2 + 1;
					g += add;
					b += add * 2 + 1;
					i =     ((CLAMP(r) & 0xf8) <<  8) |
						((CLAMP(g) & 0xfc) <<  3) |
						((CLAMP(b))        >>  3);
					*(picp.us++) = i;
					break;
				case 3:
					if (a != 255) {
						i = *picp.ub;
						r = BLEND((i & 0xff), r, a);
						i = *(picp.ub + 1);
						g = BLEND((i & 0xff), g, a);
						i = *(picp.ub + 2);
						b = BLEND((i & 0xff), b, a);
					}
					*(picp.ub++) = CLAMP(r);
					*(picp.ub++) = CLAMP(g);
					*(picp.ub++) = CLAMP(b);
					picp.ub += 3;
					break;
				case 4:
					if (a != 255) {
						i = *picp.ul;
						r = BLEND((i>>16 & 0xff), r, a);
						g = BLEND((i>>8  & 0xff), g, a);
						b = BLEND((i     & 0xff), b, a);
					}
					i = ((CLAMP(r) << 16)
					     | (CLAMP(g) << 8)
					     | (CLAMP(b)));
					*(picp.ul++) = i;
					break;
				}
				add ^= 3;
			}
		}
	}
}

static void box_offsets(unsigned char *buf, int num,
			int screen_w, int screen_h, int pic_w, int pic_h,
			int *x_off, int *y_off)
{
	int a, doblend;
	int x_min = pic_w, x_max = 0;
	int y_min = pic_h, y_max = 0;
	unsigned int i = 0;
	unsigned short data1[4];
	unsigned char cols1[16];
	unsigned short data2[4];
	unsigned char cols2[16];
	unsigned char *bufend;
	unsigned int stin, stinn, stixs, stixe, stiys, stiye;
	int xs, xe, ys, ye;

	SPLASH_DEBUG();

	if ((screen_w == pic_w && screen_h == pic_h) || num == 0)
		*x_off = *y_off = 0;

	bufend = buf + num * 12;
	stin = 1;
	stinn = 0;
	stixs = stixe = 0;
	stiys = stiye = 0;

	while (buf < bufend) {
		doblend = 0;
		buf += boxextract(buf, data1, cols1, &doblend);
		if (data1[0] == 32767 && data1[1] == 32767) {
			/* box stipple */
			if (stinn == 32)
				continue;
			if (stinn == 0) {
				stixs = data1[2];
				stixe = data1[3];
				stiys = stiye = 0;
			} else if (stinn == 4) {
				stiys = data1[2];
				stiye = data1[3];
			}
			stin = stinn;
			continue;
		}
		stinn = 0;
		if (data1[0] > 32767)
			buf += boxextract(buf, data2, cols2, &doblend);
		if (data1[0] == 32767 && data1[1] == 32766) {
			/* box copy */
			i = 12 * (short)data1[3];
			doblend = 0;
			i += boxextract(buf + i, data1, cols1, &doblend);
			if (data1[0] > 32767)
				boxextract(buf + i, data2, cols2, &doblend);
		}
		if (data1[0] == 32767)
			continue;
		if (data1[2] > 32767)
			data1[2] = ~data1[2];
		if (data1[3] > 32767)
			data1[3] = ~data1[3];
		if (data1[0] > 32767) {
			data1[0] = ~data1[0];
			for (i = 0; i < 4; i++)
				data1[i] = (data1[i] * (65536 - 1)
					    + data2[i] * 1) >> 16;
		}
		*(unsigned int *)cols2 = *(unsigned int *)cols1;
		a = cols2[3];
		if (a == 0 && !doblend)
			continue;

		if (stixs >= 32768) {
			xs = (stixs ^ 65535) + data1[0];
			xe = stixe ? stixe + data1[0] : data1[2];
		} else if (stixe >= 32768) {
			xs = stixs ? data1[2] - stixs : data1[0];
			xe = data1[2] - (stixe ^ 65535);
		} else {
			xs = stixs;
			xe = stixe ? stixe : data1[2];
		}
		if (stiys >= 32768) {
			ys = (stiys ^ 65535) + data1[1];
			ye = stiye ? stiye + data1[1] : data1[3];
		} else if (stiye >= 32768) {
			ys = stiys ? data1[3] - stiys : data1[1];
			ye = data1[3] - (stiye ^ 65535);
		} else {
			ys = stiys;
			ye = stiye ? stiye : data1[3];
		}
		if (xs < data1[0])
			xs = data1[0];
		if (xe > data1[2])
			xe = data1[2];
		if (ys < data1[1])
			ys = data1[1];
		if (ye > data1[3])
			ye = data1[3];

		if (xs < x_min)
			x_min = xs;
		if (xe > x_max)
			x_max = xe;
		if (ys < y_min)
			y_min = ys;
		if (ye > y_max)
			y_max = ye;
	}
	{
		int x_center = (x_min + x_max) / 2;
		int y_center = (y_min + y_max) / 2;

		if (screen_w == pic_w)
			*x_off = 0;
		else {
			if (x_center < (pic_w + pic_w / 10) >> 1
			    && x_center > (pic_w - pic_w / 10) >> 1) {
				*x_off = (screen_w - pic_w) >> 1;
			} else {
				int x = x_center * screen_w / pic_w;
				*x_off = x - x_center;
				if (x_min + x_off > 0)
					*x_off = 0;
				if (x_max + *x_off > screen_w)
					*x_off = screen_w - pic_w;
			}
		}
		if (screen_h == pic_h)
			*y_off = 0;
		else {
			if (y_center < (pic_h + pic_h / 10) >> 1 &&
			    y_center > (pic_h - pic_h / 10) >> 1)
				*y_off = (screen_h - pic_h) >> 1;
			else {
				int x = y_center * screen_h / pic_h;
				*y_off = x - y_center;
				if (y_min + y_off > 0)
					*y_off = 0;
				if (y_max + *x_off > screen_h)
					*y_off = screen_h - pic_h;
			}
		}
	}
}

static int splash_check_jpeg(unsigned char *jpeg,
			     int width, int height, int depth)
{
	int size, err;
	unsigned char *mem;
	struct jpeg_decdata *decdata; /* private decoder data */


	size = ((width + 15) & ~15) * ((height + 15) & ~15)
		* ((depth + 1) >> 3);
	mem = vmalloc(size);
	if (!mem) {
		printk(KERN_INFO "bootsplash: no memory for decoded picture.\n");
		return -1;
	}
	decdata = vmalloc(sizeof(*decdata));
	if (!decdata) {
		printk(KERN_INFO "bootsplash: not enough memory.\n");
		vfree(mem);
		return -1;
	}
	err = jpeg_decode(jpeg, mem,
			  ((width + 15) & ~15), ((height + 15) & ~15), depth,
			  decdata);
	if (err)
		printk(KERN_INFO "bootsplash: "
		       "error while decompressing picture: %s (%d)\n",
		       jpg_errors[err - 1], err);
	vfree(decdata);
	vfree(mem);
	return err ? -1 : 0;
}

static void splash_free(struct vc_data *vc, struct fb_info *info)
{
	struct splash_data *sd;
	struct splash_data *next;
	SPLASH_DEBUG();
	for (sd = vc->vc_splash_data; sd; sd = next) {
		next = sd->next;
		sd->pic->ref_cnt--;
		if (!sd->pic->ref_cnt) {
			vfree(sd->pic->splash_pic);
			vfree(sd->pic);
		}
		sd->imgd->ref_cnt--;
		if (!sd->imgd->ref_cnt) {
			vfree(sd->imgd->splash_sboxes);
			vfree(sd->imgd);
		}
		vfree(sd);
	}
	vc->vc_splash_data = 0;
	if (info)
		info->splash_data = 0;
}

static int splash_mkpenguin(struct splash_data *data,
			    int pxo, int pyo, int pwi, int phe,
			    int pr, int pg, int pb)
{
	unsigned char *buf;
	int i;

	if (pwi == 0 || phe == 0)
		return 0;

	buf = (unsigned char *)data + sizeof(*data);

	pwi += pxo - 1;
	phe += pyo - 1;

	*buf++ = pxo;
	*buf++ = pxo >> 8;
	*buf++ = pyo;
	*buf++ = pyo >> 8;
	*buf++ = pwi;
	*buf++ = pwi >> 8;
	*buf++ = phe;
	*buf++ = phe >> 8;
	*buf++ = pr;
	*buf++ = pg;
	*buf++ = pb;
	*buf++ = 0;

	for (i = 0; i < 12; i++, buf++)
		*buf = buf[-12];

	buf[-24] ^= 0xff;
	buf[-23] ^= 0xff;
	buf[-1] = 0xff;

	return 2;
}

static const int splash_offsets[3][16] = {
    /* len, unit, size, state, fgcol, col, xo, yo, wi, he
       boxcnt, ssize, sboxcnt, percent, overok, palcnt */
    /* V1 */
	{   20,   -1,   16,    -1,    -1,  -1,  8, 10, 12, 14,
	    -1,    -1,      -1,      -1,     -1,     -1 },
	/* V2 */
	{   35,    8,   12,     9,    10,  11, 16, 18, 20, 22,
	    -1,    -1,      -1,      -1,     -1,     -1 },
	/* V3 */
	{   38,    8,   12,     9,    10,  11, 16, 18, 20, 22,
	    24,    28,      32,      34,     36,     37 },
};

#define SPLASH_OFF_LEN     offsets[0]
#define SPLASH_OFF_UNIT    offsets[1]
#define SPLASH_OFF_SIZE    offsets[2]
#define SPLASH_OFF_STATE   offsets[3]
#define SPLASH_OFF_FGCOL   offsets[4]
#define SPLASH_OFF_COL     offsets[5]
#define SPLASH_OFF_XO      offsets[6]
#define SPLASH_OFF_YO      offsets[7]
#define SPLASH_OFF_WI      offsets[8]
#define SPLASH_OFF_HE      offsets[9]
#define SPLASH_OFF_BOXCNT  offsets[10]
#define SPLASH_OFF_SSIZE   offsets[11]
#define SPLASH_OFF_SBOXCNT offsets[12]
#define SPLASH_OFF_PERCENT offsets[13]
#define SPLASH_OFF_OVEROK  offsets[14]
#define SPLASH_OFF_PALCNT  offsets[15]

static inline int splash_getb(unsigned char *pos, int off)
{
	return off == -1 ? 0 : pos[off];
}

static inline int splash_gets(unsigned char *pos, int off)
{
	return off == -1 ? 0 : pos[off] | pos[off + 1] << 8;
}

static inline int splash_geti(unsigned char *pos, int off)
{
	return off == -1 ? 0 : (pos[off] |
				pos[off + 1] << 8 |
				pos[off + 2] << 16 |
				pos[off + 3] << 24);
}

/* move the given splash_data to the current one */
static void splash_pivot_current(struct vc_data *vc, struct splash_data *new)
{
	struct splash_data *sd;
	struct splash_pic_data *pic;
	int state, percent, silent;

	sd = vc->vc_splash_data;
	if (!sd || sd == new)
		return;

	state = sd->splash_state;
	percent = sd->splash_percent;
	silent = sd->splash_dosilent;
	if (sd->pic->ref_cnt > 1) {
		pic = kzalloc(sizeof(struct splash_pic_data), GFP_KERNEL);
		if (!pic)
			return;
		sd->pic = pic;
	}
	sd->pic->ref_cnt = 1;
	sd->pic->splash_pic_size = 0;
	sd->pic->splash_pic = NULL;
	sd->splash_vc_text_wi = sd->imgd->splash_text_wi;
	sd->splash_vc_text_he = sd->imgd->splash_text_he;
	for (; sd->next; sd = sd->next) {
		if (sd->next == new) {
			sd->next = new->next;
			new->next = vc->vc_splash_data;
			vc->vc_splash_data = new;
			/* copy the current states */
			new->splash_state = state;
			new->splash_percent = percent;
			new->splash_dosilent = silent;
			new->splash_vc_text_wi = new->imgd->splash_text_wi;
			new->splash_vc_text_he = new->imgd->splash_text_he;

			new->splash_boxes_xoff = 0;
			new->splash_boxes_yoff = 0;
			new->splash_sboxes_xoff = 0;
			new->splash_sboxes_yoff = 0;

			if (new->pic->ref_cnt > 1) {
				struct splash_pic_data *pic;
				pic = kzalloc(sizeof(struct splash_pic_data),
					      GFP_KERNEL);
				if (!pic)
					return;

				new->pic = pic;
			}
			new->pic->ref_cnt = 1;
			new->pic->splash_pic_size = 0;
			new->pic->splash_pic = NULL;

			return;
		}
	}
}

static int get_raw_v2(struct vc_data *vc,
	       const int *offsets,
	       unsigned char *ndata, int len, unsigned char * end,
	       int *update)
{
	int boxcnt;
	int sboxcnt;
	struct splash_data *sd;
	struct splash_img_data *imgd;
	int i;

	sd = vc->vc_splash_data;
	if (sd != 0) {
		int up = 0;
		imgd = sd->imgd;
		i = splash_getb(ndata, SPLASH_OFF_STATE);
		if (i != 255) {
			sd->splash_state = i; /*@!@*/
			up = -1;
		}
		i = splash_getb(ndata, SPLASH_OFF_FGCOL);
		if (i != 255) {
			imgd->splash_fg_color = i;
			up = -1;
		}
		i = splash_getb(ndata, SPLASH_OFF_COL);
		if (i != 255) {
			imgd->splash_color = i;
			up = -1;
		}
		boxcnt = sboxcnt = 0;
		if (ndata + len <= end) {
			boxcnt = splash_gets(ndata, SPLASH_OFF_BOXCNT);
			sboxcnt = splash_gets(ndata, SPLASH_OFF_SBOXCNT);
		}
		if (boxcnt) {
			i = splash_gets(ndata, len);
			if (boxcnt + i
			    <= imgd->splash_boxcount &&
			    ndata + len + 2 + boxcnt * 12
			    <= end) {
				if (splash_geti(ndata, len + 2)
				    != 0x7ffd7fff ||
				    !memcmp(ndata + len + 2,
					    imgd->splash_boxes + i * 12,
					    8)) {
					memcpy(imgd->splash_boxes + i * 12,
					       ndata + len + 2,
					       boxcnt * 12);
					up |= 1;
				}
			}
			len += boxcnt * 12 + 2;
		}
		if (sboxcnt) {
			i = splash_gets(ndata, len);
			if ((sboxcnt + i <= imgd->splash_sboxcount) &&
			    (ndata + len + 2 + sboxcnt * 12 <= end)) {
				if ((splash_geti(ndata, len + 2) != 0x7ffd7fff)
				    || !memcmp(ndata + len + 2,
					       imgd->splash_sboxes + i * 12,
					       8)) {
					memcpy(imgd->splash_sboxes + i * 12,
					       ndata + len + 2,
					       sboxcnt * 12);
					up |= 2;
				}
			}
		}
		if (update)
			*update = up;

		if (sd->pic->ref_cnt > 1) {
			struct splash_pic_data *pic;
			pic = kzalloc(sizeof
				      (struct splash_pic_data),
				      GFP_KERNEL);
			if (!pic)
				return -1;
			sd->pic = pic;
		}
		sd->pic->ref_cnt = 1;
		sd->pic->splash_pic = NULL;
		sd->pic->splash_pic_size = 0;
	}
	return 0;
}

static int splash_getraw(unsigned char *start, unsigned char *end, int *update)
{
	unsigned char *ndata;
	int version;
	int splash_size;
	int unit;
	int width, height;
	int silentsize;
	int boxcnt;
	int sboxcnt;
	int palcnt;
	int len;
	const int *offsets;
	struct vc_data *vc = NULL;
	struct fb_info *info = NULL;
	struct splash_data *sd;
	struct splash_img_data *imgd;
	struct splash_pic_data *pic;
	struct splash_data *splash_found = NULL;
	int unit_found = -1;
	int oldpercent, oldsilent;

	if (update)
		*update = -1;

	if (!update ||
	    start[7] < '2' ||
	    start[7] > '3' ||
	    splash_geti(start, 12) != (int)0xffffffff)
		printk(KERN_INFO "bootsplash %s: looking for picture...\n",
		       SPLASH_VERSION);

	oldpercent = -3;
	oldsilent = -1;
	for (ndata = start; ndata < end; ndata++) {
		if (ndata[0] != 'B' ||
		    ndata[1] != 'O' ||
		    ndata[2] != 'O' ||
		    ndata[3] != 'T')
			continue;
		if (ndata[4] != 'S' ||
		    ndata[5] != 'P' ||
		    ndata[6] != 'L' ||
		    ndata[7]  < '1' ||
		    ndata[7]  > '3')
			continue;

		version = ndata[7] - '0';
		offsets = splash_offsets[version - 1];
		len = SPLASH_OFF_LEN;

		unit = splash_getb(ndata, SPLASH_OFF_UNIT);
		if (unit >= MAX_NR_CONSOLES)
			continue;

		if (unit)
			vc_allocate(unit);

		vc = vc_cons[unit].d;
		if (!vc)
			continue;

		info = registered_fb[(int)con2fb_map[unit]];

		splash_size = splash_geti(ndata, SPLASH_OFF_SIZE);

		/*
		 * Update. Wonder what should happen here now
		 * since we can have multiple splash_data records
		 */
		if (splash_size == (int)0xffffffff && version > 1) {
			if (get_raw_v2(vc, offsets, ndata, len, end, update) < 0)
				return -1;

			return unit;
		}

		if (splash_size == 0) {
			printk(KERN_INFO
			       "bootsplash: ...found, freeing memory.\n");
			if (vc->vc_splash_data)
				splash_free(vc, info);
			return unit;
		}
		boxcnt = splash_gets(ndata, SPLASH_OFF_BOXCNT);
		palcnt = 3 * splash_getb(ndata, SPLASH_OFF_PALCNT);
		if (ndata + len + splash_size > end) {
			printk(KERN_ERR
			       "bootsplash: ...found, but truncated!\n");
			return -1;
		}
		silentsize = splash_geti(ndata, SPLASH_OFF_SSIZE);
		if (silentsize)
			printk(KERN_INFO
			       "bootsplash: silentjpeg size %d bytes\n",
			       silentsize);
		if (silentsize >= splash_size) {
			printk(KERN_ERR "bootsplash: bigger than splashsize!\n");
			return -1;
		}
		splash_size -= silentsize;
		if (!splash_usesilent)
			silentsize = 0;

		sboxcnt = splash_gets(ndata, SPLASH_OFF_SBOXCNT);
		if (vc->vc_splash_data) {
			oldpercent = vc->vc_splash_data->splash_percent;/*@!@*/
			oldsilent = vc->vc_splash_data->splash_dosilent;/*@!@*/
		}
		sd = kzalloc(sizeof(*sd), GFP_KERNEL);
		if (!sd)
			break;
		imgd = vmalloc(sizeof(*imgd)
			       + splash_size + (version < 3 ? 2 * 12 : 0));
		if (!imgd) {
			vfree(sd);
			break;
		}
		pic = kzalloc(sizeof(*pic), GFP_KERNEL);
		if (!pic) {
			vfree(sd);
			vfree(pic);
			break;
		}
		memset(imgd, 0, sizeof(*imgd));
		sd->imgd = imgd;
		sd->pic = pic;
		imgd->ref_cnt = 1;
		pic->ref_cnt = 1;
		jpeg_get_size(ndata + len + boxcnt * 12 + palcnt,
			      &imgd->splash_width, &imgd->splash_height);
		if (splash_check_jpeg(ndata + len + boxcnt * 12 + palcnt,
				      imgd->splash_width, imgd->splash_height,
				      info ? info->var.bits_per_pixel : 16)) {
			ndata += len + splash_size - 1;
			vfree(imgd);
			vfree(sd);
			continue;
		}
		if (silentsize) {
			imgd->splash_silentjpeg = vmalloc(silentsize);
			if (imgd->splash_silentjpeg) {
				memcpy(imgd->splash_silentjpeg,
				       ndata + len + splash_size, silentsize);
				imgd->splash_sboxes = imgd->splash_silentjpeg;
				imgd->splash_silentjpeg += 12 * sboxcnt;
				imgd->splash_sboxcount = sboxcnt;
			}
		}
		imgd->splash_fg_color = splash_getb(ndata, SPLASH_OFF_FGCOL);
		imgd->splash_color = splash_getb(ndata, SPLASH_OFF_COL);
		imgd->splash_overpaintok = splash_getb(ndata, SPLASH_OFF_OVEROK);
		imgd->splash_text_xo = splash_gets(ndata, SPLASH_OFF_XO);
		imgd->splash_text_yo = splash_gets(ndata, SPLASH_OFF_YO);
		imgd->splash_text_wi = splash_gets(ndata, SPLASH_OFF_WI);
		imgd->splash_text_he = splash_gets(ndata, SPLASH_OFF_HE);
		if (version == 1) {
			imgd->splash_text_xo *= 8;
			imgd->splash_text_wi *= 8;
			imgd->splash_text_yo *= 16;
			imgd->splash_text_he *= 16;
			imgd->splash_color    = (splash_default >> 8) & 0x0f;
			imgd->splash_fg_color = (splash_default >> 4) & 0x0f;
			sd->splash_state    = splash_default & 1;  /*@!@*/
		}

		/* fake penguin box for older formats */
		if (version == 1)
			boxcnt = splash_mkpenguin(sd, imgd->splash_text_xo + 10,
						  imgd->splash_text_yo + 10,
						  imgd->splash_text_wi - 20,
						  imgd->splash_text_he - 20,
						  0xf0, 0xf0, 0xf0);
		else if (version == 2)
			boxcnt = splash_mkpenguin(sd,
						  splash_gets(ndata, 24),
						  splash_gets(ndata, 26),
						  splash_gets(ndata, 28),
						  splash_gets(ndata, 30),
						  splash_getb(ndata, 32),
						  splash_getb(ndata, 33),
						  splash_getb(ndata, 34));

		memcpy((char *)imgd
		       + sizeof(*imgd) + (version < 3 ? boxcnt * 12 : 0),
		       ndata + len,
		       splash_size);
		imgd->splash_boxcount = boxcnt;
		imgd->splash_boxes = (unsigned char *)imgd + sizeof(*imgd);
		imgd->splash_palette = imgd->splash_boxes + boxcnt * 12;
		imgd->splash_jpeg = imgd->splash_palette + palcnt;

		sd->splash_state = splash_getb(ndata, SPLASH_OFF_STATE);/*@!@*/
		sd->splash_percent = oldpercent == -3 ?
			splash_gets(ndata, SPLASH_OFF_PERCENT) :
			oldpercent; /*@!@*/
		sd->pic->splash_pic = NULL;
		sd->pic->splash_pic_size = 0;

		sd->splash_dosilent = imgd->splash_silentjpeg != 0 ?
			(oldsilent == -1 ? 1 : oldsilent) :
			0; /* @!@ */

		sd->splash_vc_text_wi = imgd->splash_text_wi;
		sd->splash_vc_text_he = imgd->splash_text_he;

		sd->next = vc->vc_splash_data;
		vc->vc_splash_data = sd;

		if (info) {
			width = info->var.xres;
			height = info->var.yres;
			if (imgd->splash_width != width ||
			    imgd->splash_height != height) {
				ndata += len + splash_size - 1;
				continue;
			}
		}
		printk(KERN_INFO
		       "bootsplash: ...found (%dx%d, %d bytes, v%d).\n",
		       imgd->splash_width, imgd->splash_height,
		       splash_size, version);
		if (version == 1) {
			printk(KERN_WARNING
			       "bootsplash: Using deprecated v1 header. "
			       "Updating your splash utility recommended.\n");
			printk(KERN_INFO
			       "bootsplash: Find the latest version at "
		       "http://www.bootsplash.org/\n");
		}

		splash_found = sd;
		unit_found = unit;
	}

	if (splash_found) {
		splash_pivot_current(vc, splash_found);
		return unit_found;
	} else {
		vc = vc_cons[0].d;
		if (vc) {
			info = registered_fb[(int)con2fb_map[0]];
			if (info) {
				width = info->var.xres;
				height = info->var.yres;
			} else
				width = height = 0;
			if (!splash_look_for_jpeg(vc, width, height))
				return -1;
			return 0;
		}
	}

	printk(KERN_ERR "bootsplash: ...no good signature found.\n");
	return -1;
}

static void splash_update_redraw(struct vc_data *vc, struct fb_info *info)
{
	update_region(vc,
		      vc->vc_origin + vc->vc_size_row * vc->vc_top,
		      vc->vc_size_row * (vc->vc_bottom - vc->vc_top) / 2);
	splash_clear_margins(vc, info, 0);
}

int splash_do_verbose(void)
{
	struct vc_data *vc;
	struct fb_info *info;
	int ret = 0;

	SPLASH_DEBUG();
	if (!oops_in_progress)
		console_lock();

	if (!splash_usesilent)
		goto done;

	vc = vc_cons[0].d;

	if (!vc || !vc->vc_splash_data || !vc->vc_splash_data->splash_state)
		goto done;
	if (fg_console != vc->vc_num)
		goto done;
	if (!vc->vc_splash_data->imgd->splash_silentjpeg)
		goto done;

	if (!vc->vc_splash_data->splash_dosilent)
		goto done;
	vc->vc_splash_data->splash_dosilent = 0;

	info = registered_fb[(int)con2fb_map[0]];

	if (!info || !info->splash_data)
		goto done;

	splash_update_redraw(vc, info);
	ret = 0;

 done:
	if (!oops_in_progress)
		console_unlock();

	return ret;
}

static void splash_verbose_callback(struct work_struct *ignored)
{
	splash_do_verbose();
}

static DECLARE_WORK(splash_work, splash_verbose_callback);

int splash_verbose(void)
{
	if (!oops_in_progress)
		schedule_work(&splash_work);
	else
		return splash_do_verbose();
	return 0;
}

static void splash_off(struct vc_data *vc, struct fb_info *info)
{
	int rows = info->var.xres / vc->vc_font.width;
	int cols = info->var.yres / vc->vc_font.height;
	SPLASH_DEBUG();

	info->splash_data = 0;
	if (rows != vc->vc_rows || cols != vc->vc_cols)
		vc_resize(vc, rows, cols);
}

/* look for the splash with the matching size and set it as the current */
static int splash_look_for_jpeg(struct vc_data *vc, int width, int height)
{
	struct splash_data *sd, *found = NULL;
	int found_delta_x = INT_MAX, found_delta_y = INT_MAX;

	for (sd = vc->vc_splash_data; sd; sd = sd->next) {
		int delta_x = abs(sd->imgd->splash_width - width) * height;
		int delta_y = abs(sd->imgd->splash_height - height) * width;
		if (!found ||
		    (found_delta_x + found_delta_y > delta_x + delta_y)) {
			found = sd;
			found_delta_x = delta_x;
			found_delta_y = delta_y;
		}
	}

	if (found) {
		SPLASH_DEBUG("bootsplash: "
			     "scalable image found (%dx%d scaled to %dx%d).",
			     found->imgd->splash_width,
			     found->imgd->splash_height,
			     width, height);

		splash_pivot_current(vc, found);

		/* textarea margins are constant independent from image size */
		if (found->imgd->splash_height != height)
			found->splash_vc_text_he = height
				- (found->imgd->splash_height
				   - found->imgd->splash_text_he);
		else
			found->splash_vc_text_he = found->imgd->splash_text_he;
		if (found->imgd->splash_width != width)
			found->splash_vc_text_wi =
				width
				- (found->imgd->splash_width
				   - found->imgd->splash_text_wi);
		else
			found->splash_vc_text_wi = found->imgd->splash_text_wi;

		if (found->imgd->splash_width != width
		    || found->imgd->splash_height != height) {
			box_offsets(found->imgd->splash_boxes,
				    found->imgd->splash_boxcount,
				    width, height,
				    found->imgd->splash_width,
				    found->imgd->splash_height,
				    &found->splash_boxes_xoff,
				    &found->splash_boxes_yoff);
			SPLASH_DEBUG("bootsplash: offsets for boxes: x=%d y=%d",
				     found->splash_boxes_xoff,
				     found->splash_boxes_yoff);

			if (found->imgd->splash_sboxes) {
				box_offsets(found->imgd->splash_sboxes,
					    found->imgd->splash_sboxcount,
					    width, height,
					    found->imgd->splash_width,
					    found->imgd->splash_height,
					    &found->splash_sboxes_xoff,
					    &found->splash_sboxes_yoff);
				SPLASH_DEBUG("bootsplash: "
					     "offsets sboxes: x=%d y=%d",
					     found->splash_sboxes_xoff,
					     found->splash_sboxes_yoff);
			}
		} else {
			found->splash_sboxes_xoff = 0;
			found->splash_sboxes_yoff = 0;
		}
		return 0;
	}
	return -1;
}

static int splash_recolor(struct vc_data *vc, struct fb_info *info)
{
	int color;

	SPLASH_DEBUG();
	if (!vc->vc_splash_data)
		return -1;
	if (!vc->vc_splash_data->splash_state)
		return 0;
	color = vc->vc_splash_data->imgd->splash_color << 4 |
		vc->vc_splash_data->imgd->splash_fg_color;
	if (vc->vc_def_color != color)
		con_remap_def_color(vc, color);
	if (info && fg_console == vc->vc_num)
		splash_update_redraw(vc, info);
	vc->vc_splash_data->color_set = 1;
	return 0;
}

int splash_prepare(struct vc_data *vc, struct fb_info *info)
{
	int err;
	int width, height, depth, octpp, size, sbytes;
	int pic_update = 0;
	struct jpeg_decdata *decdata; /* private decoder data */

	SPLASH_DEBUG("vc_num: %i", vc->vc_num);

#if 0 /* Nouveau fb sets a different ops, so we can't use the condition */
	if (info->fbops->fb_imageblit != cfb_imageblit) {
		printk(KERN_ERR "bootsplash: "
		       "found, but framebuffer can't "
		       "handle it!\n");
		return -1;
	}
#endif

	if (!vc->vc_splash_data || !vc->vc_splash_data->splash_state) {
		splash_off(vc, info);
		return -1;
	}

	width = info->var.xres;
	height = info->var.yres;
	depth = info->var.bits_per_pixel;
	octpp = (depth + 1) >> 3;

	if (depth == 24 || depth < 15) {   /* Other targets might need fixing */
		splash_off(vc, info);
		return -2;
	}
	if (splash_look_for_jpeg(vc, width, height) < 0) {
		printk(KERN_INFO "bootsplash: no matching splash %dx%d\n",
		       width, height);
		splash_off(vc, info);
		return -2;
	}

	sbytes = ((width + 15) & ~15) * octpp;
	size = sbytes * ((height + 15) & ~15);

	if (size != vc->vc_splash_data->pic->splash_pic_size) {
		if (vc->vc_splash_data->pic->ref_cnt > 1) {
			struct splash_pic_data *pic;
			pic = kzalloc(sizeof(struct splash_pic_data),
				      GFP_KERNEL);
			if (!pic)
				return -2;
			vc->vc_splash_data->pic = pic;
		}
		vc->vc_splash_data->pic->ref_cnt = 1;
		vc->vc_splash_data->pic->splash_pic = NULL;
		vc->vc_splash_data->pic->splash_pic_size = 0;
	}
	if (!vc->vc_splash_data->pic->splash_pic) {
		vc->vc_splash_data->pic->splash_pic = vmalloc(size);
		pic_update = 1;
	}
	if (!vc->vc_splash_data->pic->splash_pic) {
		printk(KERN_INFO "bootsplash: not enough memory.\n");
		splash_off(vc, info);
		return -3;
	}

	decdata = vmalloc(sizeof(*decdata));
	if (!decdata) {
		printk(KERN_INFO "bootsplash: not enough memory.\n");
		splash_off(vc, info);
		return -3;
	}

	if (vc->vc_splash_data->imgd->splash_silentjpeg &&
	    vc->vc_splash_data->splash_dosilent) {
		pic_update = 1;
		err = jpeg_get(vc->vc_splash_data->imgd->splash_silentjpeg,
			       vc->vc_splash_data->pic->splash_pic,
			       width, height, depth, decdata);
		if (err) {
			printk(KERN_INFO "bootsplash: "
			       "error while decompressing silent picture: "
			       "%s (%d)\n",
			       jpg_errors[err - 1], err);
			vc->vc_splash_data->splash_dosilent = 0;
		} else {
			if (vc->vc_splash_data->imgd->splash_sboxcount)
				boxit(vc->vc_splash_data->pic->splash_pic,
				      sbytes,
				      vc->vc_splash_data->imgd->splash_sboxes,
				      vc->vc_splash_data->imgd->splash_sboxcount,
				      vc->vc_splash_data->splash_percent,
				      vc->vc_splash_data->splash_sboxes_xoff,
				      vc->vc_splash_data->splash_sboxes_yoff,
				      vc->vc_splash_data->splash_percent < 0 ?
				      1 : 0,
				      octpp);
			splashcopy(info->screen_base,
				   vc->vc_splash_data->pic->splash_pic,
				   info->var.yres,
				   info->var.xres,
				   info->fix.line_length, sbytes,
				   octpp);
		}
	} else
		vc->vc_splash_data->splash_dosilent = 0;

	if (pic_update) {
		err = jpeg_get(vc->vc_splash_data->imgd->splash_jpeg,
			       vc->vc_splash_data->pic->splash_pic,
			       width, height, depth, decdata);
		if (err) {
			printk(KERN_INFO "bootsplash: "
			       "error while decompressing picture: %s (%d) .\n",
			       jpg_errors[err - 1], err);
			splash_off(vc, info);
			return -4;
		}
	}

	vfree(decdata);

	vc->vc_splash_data->pic->splash_pic_size = size;
	vc->vc_splash_data->pic->splash_pic_stride = sbytes;

	if (vc->vc_splash_data->imgd->splash_boxcount)
		boxit(vc->vc_splash_data->pic->splash_pic,
		      sbytes,
		      vc->vc_splash_data->imgd->splash_boxes,
		      vc->vc_splash_data->imgd->splash_boxcount,
		      vc->vc_splash_data->splash_percent,
		      vc->vc_splash_data->splash_boxes_xoff,
		      vc->vc_splash_data->splash_boxes_yoff,
		      0,
		      octpp);
	if (vc->vc_splash_data->splash_state) {
		int cols = vc->vc_splash_data->splash_vc_text_wi
			/ vc->vc_font.width;
		int rows = vc->vc_splash_data->splash_vc_text_he
			/ vc->vc_font.height;

		info->splash_data = vc->vc_splash_data;

		/* vc_resize also calls con_switch which resets yscroll */
		if (rows != vc->vc_rows || cols != vc->vc_cols)
			vc_resize(vc, cols, rows);
		if (!vc->vc_splash_data->color_set)
			splash_recolor(vc, NULL);
	} else {
		splash_off(vc, info);
		return -5;
	}
	return 0;
}


#ifdef CONFIG_PROC_FS

#include <linux/proc_fs.h>

static int splash_read_proc(char *buffer, char **start, off_t offset, int size,
			    int *eof, void *data);
static int splash_write_proc(struct file *file, const char *buffer,
			     unsigned long count, void *data);
static int splash_status(struct vc_data *vc);
static int splash_proc_register(void);

static struct proc_dir_entry *proc_splash;

static int splash_status(struct vc_data *vc)
{
	struct fb_info *info;

	printk(KERN_INFO "bootsplash: status on console %d changed to %s\n",
	       vc->vc_num,
	       vc->vc_splash_data &&
	       vc->vc_splash_data->splash_state ? "on" : "off");

	info = registered_fb[(int) con2fb_map[vc->vc_num]];
	if (!info)
		return 0;

	if (fg_console == vc->vc_num)
		splash_prepare(vc, info);
	if (vc->vc_splash_data && vc->vc_splash_data->splash_state)
		splash_recolor(vc, info);
	else {
		splash_off(vc, info);
		if (vc->vc_def_color != 0x07)
			con_remap_def_color(vc, 0x07);
	}

	return 0;
}

int splash_copy_current_img(int unit_s, int unit_t)
{
	struct fb_info *info;
	struct vc_data *vc_s;
	struct vc_data *vc_t;
	struct splash_data *sd_s;
	struct splash_data *sd_t;
	int size;

	if (unit_s >= MAX_NR_CONSOLES || unit_t >= MAX_NR_CONSOLES)
		return -1;

	vc_s = vc_cons[unit_s].d;
	if (!vc_s) {
		printk(KERN_WARNING "bootsplash: "
		       "copy: source (%i) is invalid.\n", unit_s);
		return -1;
	}
	sd_s = vc_s->vc_splash_data;
	if (!sd_s || !sd_s->imgd) {
		printk(KERN_INFO "bootsplash: "
		       "copy: source_vc (%i) doesn't have valid splash data.\n",
		       unit_s);
		return -1;
	}
	vc_allocate(unit_t);
	vc_t = vc_cons[unit_t].d;
	if (!vc_t) {
		printk(KERN_WARNING "bootsplash: copy: dest (%i) is invalid.\n",
		       unit_t);
		return -1;
	}
	sd_t = kzalloc(sizeof(*sd_t), GFP_KERNEL);
	if (!sd_t)
		return -1;
	vc_t->vc_splash_data = sd_t;

	sd_t->imgd = sd_s->imgd;
	sd_t->imgd->ref_cnt++;

	/* now recreate all the rest */
	sd_t->splash_state = sd_s->splash_state;
	sd_t->splash_percent = sd_s->splash_percent;
	sd_t->splash_dosilent = sd_s->splash_dosilent;
	sd_t->splash_vc_text_wi = sd_s->imgd->splash_text_wi;
	sd_t->splash_vc_text_he = sd_s->imgd->splash_text_he;

	sd_t->splash_boxes_xoff = 0;
	sd_t->splash_boxes_yoff = 0;
	sd_t->splash_sboxes_xoff = 0;
	sd_t->splash_sboxes_yoff = 0;

	info = registered_fb[(int) con2fb_map[vc_t->vc_num]];
	size = (((info->var.xres + 15) & ~15)
		* ((info->var.bits_per_pixel + 1) >> 3))
		* ((info->var.yres + 15) & ~15);
	if (size != vc_s->vc_splash_data->pic->splash_pic_size) {
		sd_t->pic = kzalloc(sizeof(struct splash_pic_data), GFP_KERNEL);
		if (!sd_t->pic)
			return -1;
		sd_t->pic->ref_cnt = 1;
	} else {
		sd_t->pic = sd_s->pic;
		sd_t->pic->ref_cnt++;
	}

	splash_status(vc_t);

	return 0;
}

static int splash_read_proc(char *buffer, char **start, off_t offset, int size,
			int *eof, void *data)
{
	int len;
	int xres, yres;
	struct vc_data *vc = vc_cons[0].d;
	struct fb_info *info = registered_fb[(int)con2fb_map[0]];
	int color = vc->vc_splash_data ?
		vc->vc_splash_data->imgd->splash_color << 4 |
		vc->vc_splash_data->imgd->splash_fg_color : splash_default >> 4;
	int status = vc->vc_splash_data ?
		vc->vc_splash_data->splash_state & 1 : 0;

	if (info) {
		xres = info->var.xres;
		yres = info->var.yres;
	} else
		xres = yres = 0;

	len = sprintf(buffer, "Splash screen v%s (0x%02x, %dx%d%s): %s\n",
		      SPLASH_VERSION, color, xres, yres,
		      (vc->vc_splash_data ?
		       vc->vc_splash_data->splash_dosilent : 0) ? ", silent" :
		      "",
		      status ? "on" : "off");
	if (offset >= len)
		return 0;

	*start = buffer - offset;

	return (size < len - offset ? size : len - offset);
}

void splash_set_percent(struct vc_data *vc, int pe)
{
	struct fb_info *info;
	struct fbcon_ops *ops;
	struct splash_data *vc_splash_data;
	int oldpe;

	SPLASH_DEBUG(" console: %d val: %d\n", vc->vc_num, pe);

	if (pe < -2)
		pe = 0;
	if (pe > 65535)
		pe = 65535;
	pe += pe > 32767;

	vc_splash_data = vc->vc_splash_data;
	if (!vc_splash_data || vc_splash_data->splash_percent == pe)
		return;

	oldpe = vc_splash_data->splash_percent;
	vc_splash_data->splash_percent = pe;
	if (fg_console != vc->vc_num ||
	    !vc_splash_data->splash_state) {
		return;
	}
	info = registered_fb[(int) con2fb_map[vc->vc_num]];
	if (!info)
		return;

	ops = info->fbcon_par;
	if (ops->blank_state)
		return;
	if (!vc_splash_data->imgd->splash_overpaintok
	    || pe == 65536
	    || pe < oldpe) {
		if (splash_hasinter(vc_splash_data->imgd->splash_boxes,
				    vc_splash_data->imgd->splash_boxcount)) {
			splash_status(vc);
		} else
			splash_prepare(vc, info);
	} else {
		int octpp = (info->var.bits_per_pixel + 1) >> 3;
		struct splash_data *splash_data = info->splash_data;

		if (splash_data) {
			if (splash_data->imgd->splash_silentjpeg
			    && splash_data->splash_dosilent)
				boxit(info->screen_base,
				      info->fix.line_length,
				      splash_data->imgd->splash_sboxes,
				      splash_data->imgd->splash_sboxcount,
				      splash_data->splash_percent,
				      splash_data->splash_sboxes_xoff,
				      splash_data->splash_sboxes_yoff,
				      1,
				      octpp);
#if 0
			if (!info->splash_dosilent)
				boxit(info->screen_base,
				      info->fix.line_length,
				      splash_data->imgd->splash_boxes,
				      splash_data->imgd->splash_boxcount,
				      splash_data->splash_percent,
				      splash_data->splash_boxes_xoff,
				      splash_data->splash_boxes_yoff,
				      1,
				      octpp);
#endif
		}
	}
}

static const char *get_unit(const char *buffer, int *unit)
{

	*unit = -1;
	if (buffer[0] >= '0' && buffer[0] <= '9') {
		*unit = buffer[0] - '0';
		buffer++;
		if (buffer[0] >= '0' && buffer[0] <= '9') {
			*unit = *unit * 10 + buffer[0] - '0';
			buffer++;
		}
		if (*buffer == ' ')
			buffer++;
	}
	return buffer;
}

static int splash_write_proc(struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	int new, unit;
	unsigned long uval;
	struct vc_data *vc;
	struct splash_data *vc_splash_data;

	SPLASH_DEBUG();

	if (!buffer || !splash_default)
		return count;

	console_lock();
	unit = 0;
	if (buffer[0] == '@') {
		buffer++;
		buffer = get_unit(buffer, &unit);
		if (unit < 0 || unit >= MAX_NR_CONSOLES || !vc_cons[unit].d) {
			console_unlock();
			return count;
		}
	}
	SPLASH_DEBUG(" unit: %i", unit);
	vc = vc_cons[unit].d;
	vc_splash_data = vc->vc_splash_data;

	if (!strncmp(buffer, "redraw", 6)) {
		SPLASH_DEBUG(" redraw");
		splash_status(vc);
		console_unlock();
		return count;
	}

	if (!strncmp(buffer, "show", 4) || !strncmp(buffer, "hide", 4)) {
		long int pe;

		SPLASH_DEBUG("show/hide");
		if (buffer[4] == ' ' && buffer[5] == 'p')
			pe = 0;
		else if (buffer[4] == '\n')
			pe = 65535;
		else if (strict_strtol(buffer + 5, 0, &pe) == -EINVAL)
			pe = 0;
		if (pe < -2)
			pe = 0;
		if (pe > 65535)
			pe = 65535;
		if (*buffer == 'h')
			pe = 65535 - pe;
		splash_set_percent(vc, pe);
		console_unlock();
		return count;
	}

	if (!strncmp(buffer, "copy", 4)) {
		buffer += 4;
		if (buffer[0] == ' ')
			buffer++;
		buffer = get_unit(buffer, &unit);
		if (unit < 0 || unit >= MAX_NR_CONSOLES) {
			console_unlock();
			return count;
		}
		buffer = get_unit(buffer, &new);
		if (new < 0 || new >= MAX_NR_CONSOLES) {
			console_unlock();
			return count;
		}
		splash_copy_current_img(unit, new);
		console_unlock();
		return count;
	}

	if (!strncmp(buffer, "silent\n", 7)
	    || !strncmp(buffer, "verbose\n", 8)) {
		SPLASH_DEBUG(" silent/verbose");

		if (vc_splash_data &&
		    vc_splash_data->imgd->splash_silentjpeg) {
			if (vc_splash_data->splash_dosilent !=
			    (buffer[0] == 's')) {
				vc_splash_data->splash_dosilent =
					buffer[0] == 's';
				splash_status(vc);
			}
		}
		console_unlock();
		return count;
	}

	if (!strncmp(buffer, "freesilent\n", 11)) {
		SPLASH_DEBUG(" freesilent");

		if (vc_splash_data &&
		    vc_splash_data->imgd->splash_silentjpeg) {
			struct splash_data *sd;
			printk(KERN_INFO "bootsplash: freeing silent jpeg\n");
			for (sd = vc_splash_data; sd; sd = sd->next) {
				sd->imgd->splash_silentjpeg = 0;
				vfree(sd->imgd->splash_sboxes);
				sd->imgd->splash_sboxes = 0;
				sd->imgd->splash_sboxcount = 0;
			}
			if (vc_splash_data->splash_dosilent)
				splash_status(vc);

			vc->vc_splash_data->splash_dosilent = 0;
		}
		console_unlock();
		return count;
	}

	if (!strncmp(buffer, "BOOTSPL", 7)) {
		int up = -1;

		SPLASH_DEBUG(" BOOTSPL");
		unit = splash_getraw((unsigned char *)buffer,
				     (unsigned char *)buffer + count,
				     &up);
		SPLASH_DEBUG(" unit: %i up: %i", unit, up);
		if (unit >= 0) {
			struct fb_info *info;

			vc = vc_cons[unit].d;
			info = registered_fb[(int) con2fb_map[vc->vc_num]];
			if (!info) {
				console_unlock();
				return count;
			}

			if (up == -1) {
				splash_status(vc);
			} else {
				struct splash_data *vc_splash_data
					= vc->vc_splash_data;
				struct splash_data *splash_data
					= info->splash_data;
				struct fbcon_ops *ops = info->fbcon_par;
				int octpp = (info->var.bits_per_pixel + 1) >> 3;
				if (ops->blank_state ||
				    !vc_splash_data ||
				    !splash_data)
					up = 0;
				if ((up & 2) != 0
				    && splash_data->imgd->splash_silentjpeg
				    && splash_data->splash_dosilent) {
					boxit(info->screen_base,
					      info->fix.line_length,
					      splash_data->imgd->splash_sboxes,
					      splash_data->imgd->splash_sboxcount,
					      splash_data->splash_percent,
					      splash_data->splash_sboxes_xoff,
					      splash_data->splash_sboxes_yoff,
					      1,
					      octpp);
				} else if ((up & 1) != 0) {
					boxit(info->screen_base,
					      info->fix.line_length,
					      splash_data->imgd->splash_boxes,
					      splash_data->imgd->splash_boxcount,
					      splash_data->splash_percent,
					      splash_data->splash_boxes_xoff,
					      splash_data->splash_boxes_yoff,
					      1,
					      octpp);
				}
			}
		}
		console_unlock();
		return count;
	}

	if (!vc_splash_data) {
		console_unlock();
		return count;
	}

	if (buffer[0] == 't') {
		vc_splash_data->splash_state ^= 1;
		SPLASH_DEBUG(" t");
		splash_status(vc);
		console_unlock();
		return count;
	}
	if (strict_strtoul(buffer, 0, &uval) == -EINVAL)
		uval = 1;
	if (uval > 1) {
		/* expert user */
		vc_splash_data->imgd->splash_color    = uval >> 8 & 0xff;
		vc_splash_data->imgd->splash_fg_color = uval >> 4 & 0x0f;
	}
	if ((uval & 1) == vc_splash_data->splash_state)
		splash_recolor(vc, NULL);
	else {
		vc_splash_data->splash_state = uval & 1;
		splash_status(vc);
	}
	console_unlock();
	return count;
}

static int splash_proc_register(void)
{
	proc_splash = create_proc_entry("splash", 0, 0);
	if (proc_splash) {
		proc_splash->read_proc = splash_read_proc;
		proc_splash->write_proc = splash_write_proc;
		return 0;
	}
	return 1;
}

#endif	/* CONFIG_PROC_FS */

#define INIT_CONSOLE 0

void splash_init(void)
{
	static bool splash_not_initialized = true;
	struct fb_info *info;
	struct vc_data *vc;
	int isramfs = 1;
	int fd;
	int len;
	int max_len = 1024*1024*2;
	char *mem;

	if (splash_not_initialized == false)
		return;
	vc = vc_cons[INIT_CONSOLE].d;
	info = registered_fb[(int)con2fb_map[INIT_CONSOLE]];
	if (!vc
	    || !info
	    || info->var.bits_per_pixel == 24 /* not tested */
	    || info->var.bits_per_pixel < 15) /* not supported */
		return;
#ifdef CONFIG_PROC_FS
	splash_proc_register();
#endif
	splash_not_initialized = false;
	if (vc->vc_splash_data)
		return;
	fd = sys_open("/bootsplash", O_RDONLY, 0);
	if (fd < 0) {
		isramfs = 0;
		fd = sys_open("/initrd.image", O_RDONLY, 0);
	}
	if (fd < 0)
		return;
	len = (int)sys_lseek(fd, (off_t)0, 2);
	if (len <= 0) {
		sys_close(fd);
		return;
	}
	/* Don't look for more than the last 2MB */
	if (len > max_len) {
		printk(KERN_INFO "bootsplash: "
		       "scanning last %dMB of initrd for signature\n",
		       max_len>>20);
		sys_lseek(fd, (off_t)(len - max_len), 0);
		len = max_len;
	} else {
		sys_lseek(fd, (off_t)0, 0);
	}

	mem = vmalloc(len);
	if (mem) {
		console_lock();
		if ((int)sys_read(fd, mem, len) == len
		    && (splash_getraw((unsigned char *)mem,
				      (unsigned char *)mem + len, (int *)0)
			== INIT_CONSOLE)
		    && vc->vc_splash_data)
			vc->vc_splash_data->splash_state = splash_default & 1;
		console_unlock();
		vfree(mem);
	}
	sys_close(fd);
	if (isramfs)
		sys_unlink("/bootsplash");
	return;
}

#define SPLASH_ALIGN 15

static u32 *do_coefficients(u32 from, u32 to, u32 *shift)
{
	u32 *coefficients;
	u32 left = to;
	int n = 1;
	u32 upper = 31;
	int col_cnt = 0;
	int row_cnt = 0;
	int m;
	u32 rnd = from >> 1;

	if (from > to) {
		left = to;
		rnd = from >> 1;

		while (upper > 0) {
			if ((1 << upper) & from)
				break;
			upper--;
		}
		upper++;

		*shift = 32 - 8 - 1 - upper;

		coefficients = vmalloc(sizeof(u32) * (from / to + 2) * from + 1);
		if (!coefficients)
			return NULL;

		n = 1;
		while (1) {
			u32 sum = left;
			col_cnt = 0;
			m = n++;
			while (sum < from) {
				coefficients[n++] =
					((left << *shift) + rnd) / from;
				col_cnt++;
				left = to;
				sum += left;
			}
			left = sum - from;
			coefficients[n++] =
				(((to - left) << *shift) + rnd) / from;
			col_cnt++;
			coefficients[m] = col_cnt;
			row_cnt++;
			if (!left) {
				coefficients[0] = row_cnt;
				return coefficients;
			}
		}
	} else {
		left = 0;
		rnd = to >> 1;

		while (upper > 0) {
			if ((1 << upper) & to)
				break;
			upper--;
		}
		upper++;

		*shift = 32 - 8 - 1 - upper;

		coefficients = vmalloc(sizeof(u32) * 3 * from + 1);
		if (!coefficients)
			return NULL;

		while (1) {
			u32 diff;
			u32 sum = left;
			col_cnt = 0;
			row_cnt++;
			while (sum < to) {
				col_cnt++;
				sum += from;
			}
			left = sum - to;
			diff = from - left;
			if (!left) {
				coefficients[n] = col_cnt;
				coefficients[0] = row_cnt;
				return coefficients;
			}
			coefficients[n++] = col_cnt - 1;
			coefficients[n++] = ((diff << *shift) + rnd) / from;
			coefficients[n++] = ((left << *shift) + rnd) / from;
		}
	}
}


struct pixel {
	u32 red;
	u32 green;
	u32 blue;
};

#define put_pixel(pix, buf, depth)					\
	switch (depth) {						\
	case 15:							\
		*(u16 *)(buf) = (u16)((pix).red << 10 |                 \
				      (pix).green << 5 | (pix).blue);	\
	(buf) += 2;							\
	break;								\
	case 16:							\
		*(u16 *)(buf) = (u16)((pix).red << 11 |			\
				      (pix).green << 5 | (pix).blue);	\
		(buf) += 2;						\
		break;							\
	case 24:							\
		*(u16 *)(buf) = (u16)((pix).red << 8 | (pix).green);	\
		buf += 2;						\
		*((buf)++) = (pix).blue;				\
		break;							\
	case 32:							\
		*(u32 *)(buf) = (u32)((pix).red << 16 |			\
				      (pix).green << 8 | (pix).blue);	\
		(buf) += 4;						\
		break;							\
	}

#define get_pixel(pix, buf, depth)				       \
	switch (depth) {					       \
	case 15:						       \
		(pix).red = ((*(u16 *)(buf)) >> 10) & 0x1f;	       \
		(pix).green = ((*(u16 *)(buf)) >> 5) & 0x1f;	       \
		(pix).blue = (*(u16 *)(buf)) & 0x1f;		       \
		(buf) += 2;					       \
		break;						       \
	case 16:						       \
		(pix).red = ((*(u16 *)(buf)) >> 11) & 0x1f;	       \
		(pix).green = ((*(u16 *)(buf)) >> 5) & 0x3f;	       \
		(pix).blue = (*(u16 *)(buf)) & 0x1f;		       \
		(buf) += 2;					       \
		break;						       \
	case 24:						       \
		(pix).blue = *(((buf))++);			       \
		(pix).green = *(((buf))++);			       \
		(pix).red = *(((buf))++);			       \
		break;						       \
	case 32:						       \
		(pix).blue = *(((buf))++);			       \
		(pix).green = *(((buf))++);			       \
		(pix).red = *(((buf))++);			       \
		(buf)++;					       \
		break;						       \
	}

static inline void
scale_x_down(int depth, int src_w,
	     unsigned char **src_p, u32 *x_coeff,
	     u32 x_shift,  u32 y_coeff, struct pixel *row_buffer)
{
	u32 curr_x_coeff = 1;
	struct pixel curr_pixel, tmp_pixel;
	u32 x_array_size = x_coeff[0];
	int x_column_num;
	int i;
	int l, m;
	int k = 0;
	u32 rnd = (1 << (x_shift - 1));

	for (i = 0; i < src_w; ) {
		curr_x_coeff = 1;
		get_pixel(tmp_pixel, *src_p, depth);
		i++;
		for (l = 0; l < x_array_size; l++) {
			x_column_num = x_coeff[curr_x_coeff++];
			curr_pixel.red = 0;
			curr_pixel.green = 0;
			curr_pixel.blue = 0;
			for (m = 0; m < x_column_num - 1; m++) {
				curr_pixel.red += tmp_pixel.red
					* x_coeff[curr_x_coeff];
				curr_pixel.green += tmp_pixel.green
					* x_coeff[curr_x_coeff];
				curr_pixel.blue += tmp_pixel.blue
					* x_coeff[curr_x_coeff];
				curr_x_coeff++;
				get_pixel(tmp_pixel, *src_p, depth);
				i++;
			}
			curr_pixel.red += tmp_pixel.red * x_coeff[curr_x_coeff];
			curr_pixel.green += tmp_pixel.green
				* x_coeff[curr_x_coeff];
			curr_pixel.blue += tmp_pixel.blue
				* x_coeff[curr_x_coeff];
			curr_x_coeff++;
			curr_pixel.red = (curr_pixel.red + rnd) >> x_shift;
			curr_pixel.green = (curr_pixel.green + rnd) >> x_shift;
			curr_pixel.blue = (curr_pixel.blue + rnd) >> x_shift;
			row_buffer[k].red += curr_pixel.red * y_coeff;
			row_buffer[k].green += curr_pixel.green * y_coeff;
			row_buffer[k].blue += curr_pixel.blue * y_coeff;
			k++;
		}
	}
}

static inline void
scale_x_up(int depth, int src_w, unsigned char **src_p, u32 *x_coeff,
	   u32 x_shift,  u32 y_coeff, struct pixel *row_buffer)
{
	u32 curr_x_coeff = 1;
	struct pixel curr_pixel, tmp_pixel;
	u32 x_array_size = x_coeff[0];
	int x_column_num;
	int i;
	int l, m;
	int k = 0;
	u32 rnd = (1 << (x_shift - 1));

	for (i = 0; i < src_w;) {
		curr_x_coeff = 1;
		get_pixel(tmp_pixel, *src_p, depth);
		i++;
		for (l = 0; l < x_array_size - 1; l++) {
			x_column_num = x_coeff[curr_x_coeff++];
			for (m = 0; m < x_column_num; m++) {
				row_buffer[k].red += tmp_pixel.red * y_coeff;
				row_buffer[k].green += tmp_pixel.green * y_coeff;
				row_buffer[k].blue += tmp_pixel.blue * y_coeff;
				k++;
			}
			curr_pixel.red = tmp_pixel.red * x_coeff[curr_x_coeff];
			curr_pixel.green = tmp_pixel.green
				* x_coeff[curr_x_coeff];
			curr_pixel.blue = tmp_pixel.blue * x_coeff[curr_x_coeff];
			curr_x_coeff++;
			get_pixel(tmp_pixel, *src_p, depth);
			i++;
			row_buffer[k].red += ((curr_pixel.red
					       + (tmp_pixel.red
						  * x_coeff[curr_x_coeff])
					       + rnd) >> x_shift) * y_coeff;
			row_buffer[k].green += ((curr_pixel.green
						 + (tmp_pixel.green
						    * x_coeff[curr_x_coeff])
						 + rnd) >> x_shift) * y_coeff;
			row_buffer[k].blue += ((curr_pixel.blue
						+ (tmp_pixel.blue
						   * x_coeff[curr_x_coeff])
						+ rnd) >> x_shift) * y_coeff;
			k++;
			curr_x_coeff++;
		}
		for (m = 0; m < x_coeff[curr_x_coeff]; m++) {
			row_buffer[k].red += tmp_pixel.red * y_coeff;
			row_buffer[k].green += tmp_pixel.green * y_coeff;
			row_buffer[k].blue += tmp_pixel.blue * y_coeff;
			k++;
		}
	}
}

static int scale_y_down(unsigned char *src, unsigned char *dst,
			int depth, int src_w, int src_h, int dst_w, int dst_h)
{
	int octpp = (depth + 1) >> 3;
	int src_x_bytes = octpp * ((src_w + SPLASH_ALIGN) & ~SPLASH_ALIGN);
	int dst_x_bytes = octpp * ((dst_w + SPLASH_ALIGN) & ~SPLASH_ALIGN);
	int j;
	struct pixel *row_buffer;
	u32 x_shift, y_shift;
	u32 *x_coeff;
	u32 *y_coeff;
	u32 curr_y_coeff = 1;
	unsigned char *src_p;
	unsigned char *src_p_line = src;
	char *dst_p_line;
	int r, s;
	int y_array_rows;
	int y_column_num;
	int k;
	u32 rnd;
	int xup;

	row_buffer = vmalloc(sizeof(struct pixel)
					     * (dst_w + 1));
	x_coeff = do_coefficients(src_w, dst_w, &x_shift);
	y_coeff = do_coefficients(src_h, dst_h, &y_shift);
	if (!row_buffer || !x_coeff || !y_coeff) {
		vfree(row_buffer);
		vfree(x_coeff);
		vfree(y_coeff);
		return -ENOMEM;
	}
	y_array_rows = y_coeff[0];
	rnd = (1 << (y_shift - 1));
	xup = (src_w <= dst_w) ? 1 : 0;

	dst_p_line = dst;

	for (j = 0; j < src_h;) {
		curr_y_coeff = 1;
		for (r = 0; r < y_array_rows; r++) {
			y_column_num = y_coeff[curr_y_coeff++];
			for (k = 0; k < dst_w + 1; k++) {
				row_buffer[k].red = 0;
				row_buffer[k].green = 0;
				row_buffer[k].blue = 0;
			}
			src_p = src_p_line;
			if (xup)
				scale_x_up(depth,  src_w, &src_p, x_coeff,
					   x_shift, y_coeff[curr_y_coeff],
					   row_buffer);
			else
				scale_x_down(depth,  src_w, &src_p, x_coeff,
					     x_shift, y_coeff[curr_y_coeff],
					     row_buffer);
			curr_y_coeff++;
			for (s = 1; s < y_column_num; s++) {
				src_p = src_p_line = src_p_line + src_x_bytes;
				j++;
				if (xup)
					scale_x_up(depth,  src_w, &src_p,
						   x_coeff, x_shift,
						   y_coeff[curr_y_coeff],
						   row_buffer);
				else
					scale_x_down(depth,  src_w, &src_p,
						     x_coeff, x_shift,
						     y_coeff[curr_y_coeff],
						     row_buffer);
				curr_y_coeff++;
			}
			for (k = 0; k < dst_w; k++) {
				row_buffer[k].red = (row_buffer[k].red + rnd)
					>> y_shift;
				row_buffer[k].green = (row_buffer[k].green
						       + rnd)
					>> y_shift;
				row_buffer[k].blue = (row_buffer[k].blue + rnd)
					>> y_shift;
				put_pixel(row_buffer[k], dst, depth);
			}
			dst = dst_p_line = dst_p_line + dst_x_bytes;
		}
		src_p_line = src_p_line + src_x_bytes;
		j++;
	}
	vfree(row_buffer);
	vfree(x_coeff);
	vfree(y_coeff);
	return 0;
}

static int scale_y_up(unsigned char *src, unsigned char *dst,
		      int depth, int src_w, int src_h, int dst_w, int dst_h)
{
	int octpp = (depth + 1) >> 3;
	int src_x_bytes = octpp * ((src_w + SPLASH_ALIGN) & ~SPLASH_ALIGN);
	int dst_x_bytes = octpp * ((dst_w + SPLASH_ALIGN) & ~SPLASH_ALIGN);
	int j;
	u32 x_shift, y_shift;
	u32 *x_coeff;
	u32 *y_coeff;
	struct pixel *row_buf_list[2];
	struct pixel *row_buffer;
	u32 curr_y_coeff = 1;
	unsigned char *src_p;
	unsigned char *src_p_line = src;
	char *dst_p_line;
	int r, s;
	int y_array_rows;
	int y_column_num;
	int k;
	u32 rnd;
	int bi;
	int xup;
	int writes;

	x_coeff = do_coefficients(src_w, dst_w, &x_shift);
	y_coeff = do_coefficients(src_h, dst_h, &y_shift);
	row_buf_list[0] = vmalloc(2 * sizeof(struct pixel)
						  * (dst_w + 1));
	if (!row_buf_list[0] || !x_coeff || !y_coeff) {
		vfree(row_buf_list[0]);
		vfree(x_coeff);
		vfree(y_coeff);
		return -ENOMEM;
	}
	row_buf_list[1] = row_buf_list[0] + (dst_w + 1);

	y_array_rows = y_coeff[0];
	rnd = (1 << (y_shift - 1));
	bi = 1;
	xup = (src_w <= dst_w) ? 1 : 0;
	writes = 0;

	dst_p_line = dst;
	src_p = src_p_line;

	row_buffer = row_buf_list[0];

	for (j = 0; j < src_h;) {
		memset(row_buf_list[0], 0, (2 * sizeof(struct pixel)
					    * (dst_w + 1)));
		curr_y_coeff = 1;
		if (xup)
			scale_x_up(depth,  src_w, &src_p, x_coeff,
				   x_shift, 1, row_buffer);
		else
			scale_x_down(depth,  src_w, &src_p, x_coeff, x_shift, 1,
				     row_buffer);
		src_p = src_p_line = src_p_line + src_x_bytes;
		j++;
		for (r = 0; r < y_array_rows - 1; r++) {
			struct pixel *old_row_buffer = row_buffer;
			u32 prev_y_coeff_val;

			y_column_num = y_coeff[curr_y_coeff];
			for (s = 0; s < y_column_num; s++) {
				for (k = 0; k < dst_w; k++)
					put_pixel(row_buffer[k], dst, depth);
				dst = dst_p_line = dst_p_line + dst_x_bytes;
				writes++;
			}
			curr_y_coeff++;
			row_buffer = row_buf_list[(bi++) % 2];
			prev_y_coeff_val = y_coeff[curr_y_coeff++];
			if (xup)
				scale_x_up(depth,  src_w, &src_p, x_coeff,
					   x_shift, 1, row_buffer);
			else
				scale_x_down(depth,  src_w, &src_p, x_coeff,
					     x_shift, 1, row_buffer);
			src_p = src_p_line = src_p_line + src_x_bytes;
			j++;
			for (k = 0; k < dst_w; k++) {
				struct pixel pix;
				pix.red = ((old_row_buffer[k].red
					    * prev_y_coeff_val)
					   + (row_buffer[k].red
					      * y_coeff[curr_y_coeff])
					   + rnd) >> y_shift;
				pix.green = ((old_row_buffer[k].green
					      * prev_y_coeff_val)
					     + (row_buffer[k].green
						* y_coeff[curr_y_coeff])
					     + rnd) >> y_shift;
				pix.blue = ((old_row_buffer[k].blue
					     * prev_y_coeff_val)
					    + (row_buffer[k].blue
					       * y_coeff[curr_y_coeff])
					    + rnd) >> y_shift;
				old_row_buffer[k].red = 0;
				old_row_buffer[k].green = 0;
				old_row_buffer[k].blue = 0;
				put_pixel(pix, dst, depth);
			}
			dst = dst_p_line = dst_p_line + dst_x_bytes;
			writes++;
			curr_y_coeff++;
		}
		for (r = 0; r < y_coeff[curr_y_coeff]; r++) {
			for (k = 0; k < dst_w; k++)
				put_pixel(row_buffer[k], dst, depth);

			dst = dst_p_line = dst_p_line + dst_x_bytes;
			writes++;
		}
	}
	vfree(row_buf_list[0]);
	vfree(x_coeff);
	vfree(y_coeff);

	return 0;
}

static int jpeg_get(unsigned char *buf, unsigned char *pic,
		    int width, int height, int depth,
		    struct jpeg_decdata *decdata)
{
	int my_width, my_height;
	int err;

	jpeg_get_size(buf, &my_width, &my_height);

	if (my_height != height || my_width != width) {
		int my_size = ((my_width + 15) & ~15)
		    * ((my_height + 15) & ~15) * ((depth + 1) >> 3);
		unsigned char *mem = vmalloc(my_size);
		if (!mem)
			return 17;
		err = jpeg_decode(buf, mem, ((my_width + 15) & ~15),
				  ((my_height + 15) & ~15), depth, decdata);
		if (err) {
			vfree(mem);
			return err;
		}
		printk(KERN_INFO
		       "bootsplash: scaling image from %dx%d to %dx%d\n",
		       my_width, my_height, width, height);
		if (my_height <= height)
			err = scale_y_up(mem, pic, depth, my_width, my_height,
					 ((width + 15) & ~15),
					 ((height + 15) & ~15));
		else
			err = scale_y_down(mem, pic, depth, my_width, my_height,
					   ((width + 15) & ~15),
					   ((height + 15) & ~15));
		vfree(mem);
		if (err < 0)
			return 17;
	} else {
		err = jpeg_decode(buf, pic, ((width + 15) & ~15),
				  ((height + 15) & ~15), depth, decdata);
		if (err)
			return err;
	}
	return 0;
}
