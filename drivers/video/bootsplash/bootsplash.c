/* 
 *           linux/drivers/video/bootsplash/bootsplash.c - 
 *                 splash screen handling functions.
 *	
 *	(w) 2001-2004 by Volker Poplawski, <volker@poplawski.de>,
 * 		    Stefan Reinauer, <stepan@suse.de>,
 * 		    Steffen Winterfeldt, <snwint@suse.de>,
 *                  Michael Schroeder <mls@suse.de>
 * 		    
 *        Ideas & SuSE screen work by Ken Wimer, <wimer@suse.de>
 *
 *  For more information on this code check http://www.bootsplash.org/
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/vmalloc.h>
#include <linux/unistd.h>
#include <linux/syscalls.h>

#include <asm/irq.h>
#include <asm/system.h>

#include "../console/fbcon.h"
#include "bootsplash.h"
#include "decode-jpg.h"

extern struct fb_ops vesafb_ops;
extern signed char con2fb_map[MAX_NR_CONSOLES];

#define SPLASH_VERSION "3.1.6-2004/03/31"

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
	"depth mismatch"
};

static struct jpeg_decdata *decdata = 0; /* private decoder data */

static int splash_registered = 0;
static int splash_usesilent = 0;	/* shall we display the silentjpeg? */
int splash_default = 0xf01;

static int splash_check_jpeg(unsigned char *jpeg, int width, int height, int depth);

static int __init splash_setup(char *options)
{
	if(!strncmp("silent", options, 6)) {
		printk(KERN_INFO "bootsplash: silent mode.\n");
		splash_usesilent = 1;
		/* skip "silent," */
		if (strlen(options) == 6)
			return 0;
		options += 7;
	}
	if(!strncmp("verbose", options, 7)) {
		printk(KERN_INFO "bootsplash: verbose mode.\n");
		splash_usesilent = 0;
		return 0;
	}
	splash_default = simple_strtoul(options, NULL, 0);
	return 0;
}

__setup("splash=", splash_setup);


static int splash_hasinter(unsigned char *buf, int num)
{
    unsigned char *bufend = buf + num * 12;
    while(buf < bufend) {
	if (buf[1] > 127)		/* inter? */
	    return 1;
	buf += buf[3] > 127 ? 24 : 12;	/* blend? */
    }
    return 0;
}

static int boxextract(unsigned char *buf, unsigned short *dp, unsigned char *cols, int *blendp)
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

static void boxit(unsigned char *pic, int bytes, unsigned char *buf, int num, int percent, int overpaint)
{
    int x, y, i, p, doblend, r, g, b, a, add;
    unsigned short data1[4];
    unsigned char cols1[16];
    unsigned short data2[4];
    unsigned char cols2[16];
    unsigned char *bufend;
    unsigned short *picp;
    unsigned int stipple[32], sti, stin, stinn, stixs, stixe, stiys, stiye;
    int xs, xe, ys, ye, xo, yo;

    if (num == 0)
	return;
    bufend = buf + num * 12;
    stipple[0] = 0xffffffff;
    stin = 1;
    stinn = 0;
    stixs = stixe = 0;
    stiys = stiye = 0;
    while(buf < bufend) {
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
	    stipple[stinn++] = (cols1[ 0] << 24) | (cols1[ 1] << 16) | (cols1[ 2] << 8) | cols1[ 3] ;
	    stipple[stinn++] = (cols1[ 4] << 24) | (cols1[ 5] << 16) | (cols1[ 6] << 8) | cols1[ 7] ;
	    stipple[stinn++] = (cols1[ 8] << 24) | (cols1[ 9] << 16) | (cols1[10] << 8) | cols1[11] ;
	    stipple[stinn++] = (cols1[12] << 24) | (cols1[13] << 16) | (cols1[14] << 8) | cols1[15] ;
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
	    data1[0] = ~data1[0];
	    for (i = 0; i < 4; i++)
		data1[i] = (data1[i] * (65536 - percent) + data2[i] * percent) >> 16;
	    for (i = 0; i < 16; i++)
		cols1[i] = (cols1[i] * (65536 - percent) + cols2[i] * percent) >> 16;
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
		if ((p = data1[3] - data1[1]) != 0)
		    p = ((y - data1[1]) << 16) / p;
		for (i = 0; i < 8; i++)
		    cols2[i + 8] = (cols1[i] * (65536 - p) + cols1[i + 8] * p) >> 16;
	    }
	    add = (xs & 1);
	    add ^= (add ^ y) & 1 ? 1 : 3;		/* 2x2 ordered dithering */
	    picp = (unsigned short *)(pic + xs * 2 + y * bytes);
	    for (x = xs; x <= xe; x++) {
		if (!(sti & 0x80000000)) {
		    sti <<= 1;
		    picp++;
		    add ^= 3;
		    continue;
		}
		sti = (sti << 1) | 1;
		if (doblend) {
		    if ((p = data1[2] - data1[0]) != 0)
			p = ((x - data1[0]) << 16) / p;
		    for (i = 0; i < 4; i++)
			cols2[i] = (cols2[i + 8] * (65536 - p) + cols2[i + 12] * p) >> 16;
		    a = cols2[3];
		}
		r = cols2[0];
		g = cols2[1];
		b = cols2[2];
		if (a != 255) {
		    i = *picp;
		    r = ((i >> 8 & 0xf8) * (255 - a) + r * a) / 255;
		    g = ((i >> 3 & 0xfc) * (255 - a) + g * a) / 255;
		    b = ((i << 3 & 0xf8) * (255 - a) + b * a) / 255;
		}
  #define CLAMP(x) ((x) >= 256 ? 255 : (x))
		i = ((CLAMP(r + add*2+1) & 0xf8) <<  8) |
		    ((CLAMP(g + add    ) & 0xfc) <<  3) |
		    ((CLAMP(b + add*2+1)       ) >>  3);
		*picp++ = i;
		add ^= 3;
	    }
	}
    }
}

static int splash_check_jpeg(unsigned char *jpeg, int width, int height, int depth)
{
    int size, err;
    unsigned char *mem;

    size = ((width + 15) & ~15) * ((height + 15) & ~15) * (depth >> 3);
    mem = vmalloc(size);
    if (!mem) {
	printk(KERN_INFO "bootsplash: no memory for decoded picture.\n");
	return -1;
    }
    if (!decdata)
	decdata = vmalloc(sizeof(*decdata));
    if ((err = jpeg_decode(jpeg, mem, ((width + 15) & ~15), ((height + 15) & ~15), depth, decdata)))
	  printk(KERN_INFO "bootsplash: error while decompressing picture: %s (%d)\n",jpg_errors[err - 1], err);
    vfree(mem);
    return err ? -1 : 0;
}

static void splash_free(struct vc_data *vc, struct fb_info *info)
{
    if (!vc->vc_splash_data)
	return;
    if (info->silent_screen_base)
	    info->screen_base = info->silent_screen_base;
    info->silent_screen_base = 0;
    if (vc->vc_splash_data->splash_silentjpeg)
	    vfree(vc->vc_splash_data->splash_sboxes);
    vfree(vc->vc_splash_data);
    vc->vc_splash_data = 0;
    info->splash_data = 0;
}

static int splash_mkpenguin(struct splash_data *data, int pxo, int pyo, int pwi, int phe, int pr, int pg, int pb)
{
    unsigned char *buf;
    int i;

    if (pwi ==0 || phe == 0)
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
    return off == -1 ? 0 :
           pos[off] | pos[off + 1] << 8 | pos[off + 2] << 16 | pos[off + 3] << 24;
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
    int i, len;
    const int *offsets;
    struct vc_data *vc;
    struct fb_info *info;
    struct splash_data *sd;

    if (update)
	*update = -1;

    if (!update || start[7] < '2' || start[7] > '3' || splash_geti(start, 12) != (int)0xffffffff)
	printk(KERN_INFO "bootsplash %s: looking for picture...", SPLASH_VERSION);

    for (ndata = start; ndata < end; ndata++) {
	if (ndata[0] != 'B' || ndata[1] != 'O' || ndata[2] != 'O' || ndata[3] != 'T')
	    continue;
	if (ndata[4] != 'S' || ndata[5] != 'P' || ndata[6] != 'L' || ndata[7] < '1' || ndata[7] > '3')
	    continue;
	version = ndata[7] - '0';
	offsets = splash_offsets[version - 1];
	len = SPLASH_OFF_LEN;
	unit = splash_getb(ndata, SPLASH_OFF_UNIT);
	if (unit >= MAX_NR_CONSOLES)
	    continue;
	if (unit) {
		vc_allocate(unit);
	}
	vc = vc_cons[unit].d;
	info = registered_fb[(int)con2fb_map[unit]];
	width = info->var.xres;
	height = info->var.yres;
	splash_size = splash_geti(ndata, SPLASH_OFF_SIZE);
	if (splash_size == (int)0xffffffff && version > 1) {
	    if ((sd = vc->vc_splash_data) != 0) {
		int up = 0;
		i = splash_getb(ndata, SPLASH_OFF_STATE);
		if (i != 255) {
		    sd->splash_state = i;
		    up = -1;
		}
		i = splash_getb(ndata, SPLASH_OFF_FGCOL);
		if (i != 255) {
		    sd->splash_fg_color = i;
		    up = -1;
		}
		i = splash_getb(ndata, SPLASH_OFF_COL);
		if (i != 255) {
		    sd->splash_color = i;
		    up = -1;
		}
		boxcnt = sboxcnt = 0;
		if (ndata + len <= end) {
		    boxcnt = splash_gets(ndata, SPLASH_OFF_BOXCNT);
		    sboxcnt = splash_gets(ndata, SPLASH_OFF_SBOXCNT);
		}
		if (boxcnt) {
		    i = splash_gets(ndata, len);
		    if (boxcnt + i <= sd->splash_boxcount && ndata + len + 2 + boxcnt * 12 <= end) {
			memcpy(sd->splash_boxes + i * 12, ndata + len + 2, boxcnt * 12);
			up |= 1;
		    }
		    len += boxcnt * 12 + 2;
		}
		if (sboxcnt) {
		    i = splash_gets(ndata, len);
		    if (sboxcnt + i <= sd->splash_sboxcount && ndata + len + 2 + sboxcnt * 12 <= end) {
			memcpy(sd->splash_sboxes + i * 12, ndata + len + 2, sboxcnt * 12);
			up |= 2;
		    }
		}
		if (update)
		    *update = up;
	    }
	    return unit;
	}
	if (splash_size == 0) {
	    printk(KERN_INFO"...found, freeing memory.\n");
	    if (vc->vc_splash_data)
		splash_free(vc, info);
	    return unit;
	}
	boxcnt = splash_gets(ndata, SPLASH_OFF_BOXCNT);
	palcnt = 3 * splash_getb(ndata, SPLASH_OFF_PALCNT);
	if (ndata + len + splash_size > end) {
	    printk(KERN_INFO "...found, but truncated!\n");
	    return -1;
	}
	if (!jpeg_check_size(ndata + len + boxcnt * 12 + palcnt, width, height)) {
	    ndata += len + splash_size - 1;
	    continue;
	}
	if (splash_check_jpeg(ndata + len + boxcnt * 12 + palcnt, width, height, info->var.bits_per_pixel))
	    return -1;
	silentsize = splash_geti(ndata, SPLASH_OFF_SSIZE);
	if (silentsize)
	    printk(KERN_INFO" silentjpeg size %d bytes,", silentsize);
	if (silentsize >= splash_size) {
	    printk(KERN_INFO " bigger than splashsize!\n");
	    return -1;
	}
	splash_size -= silentsize;
	if (!splash_usesilent)
	    silentsize = 0;
	else if (height * 2 * info->fix.line_length > info->fix.smem_len) {
	    printk(KERN_INFO " does not fit into framebuffer.\n");
	    silentsize = 0;
	}
	sboxcnt = splash_gets(ndata, SPLASH_OFF_SBOXCNT);
	if (silentsize) {
	    unsigned char *simage = ndata + len + splash_size + 12 * sboxcnt;
	    if (!jpeg_check_size(simage, width, height) ||
		splash_check_jpeg(simage, width, height, info->var.bits_per_pixel)) {
		    printk(KERN_INFO " error in silent jpeg.\n");
		    silentsize = 0;
		}
	}
	if (vc->vc_splash_data)
	    splash_free(vc, info);
	vc->vc_splash_data = sd = vmalloc(sizeof(*sd) + splash_size + (version < 3 ? 2 * 12 : 0));
	if (!sd)
	    break;
	sd->splash_silentjpeg = 0;
	sd->splash_sboxes = 0;
	sd->splash_sboxcount = 0;
	if (silentsize) {
	    sd->splash_silentjpeg = vmalloc(silentsize);
	    if (sd->splash_silentjpeg) {
		memcpy(sd->splash_silentjpeg, ndata + len + splash_size, silentsize);
		sd->splash_sboxes = vc->vc_splash_data->splash_silentjpeg;
		sd->splash_silentjpeg += 12 * sboxcnt;
		sd->splash_sboxcount = sboxcnt;
	    }
	}
	sd->splash_state = splash_getb(ndata, SPLASH_OFF_STATE);
	sd->splash_fg_color = splash_getb(ndata, SPLASH_OFF_FGCOL);
	sd->splash_color = splash_getb(ndata, SPLASH_OFF_COL);
	sd->splash_overpaintok = splash_getb(ndata, SPLASH_OFF_OVEROK);
	sd->splash_text_xo = splash_gets(ndata, SPLASH_OFF_XO);
	sd->splash_text_yo = splash_gets(ndata, SPLASH_OFF_YO);
	sd->splash_text_wi = splash_gets(ndata, SPLASH_OFF_WI);
	sd->splash_text_he = splash_gets(ndata, SPLASH_OFF_HE);
	sd->splash_percent = splash_gets(ndata, SPLASH_OFF_PERCENT);
	if (version == 1) {
	    sd->splash_text_xo *= 8;
	    sd->splash_text_wi *= 8;
	    sd->splash_text_yo *= 16;
	    sd->splash_text_he *= 16;
	    sd->splash_color    = (splash_default >> 8) & 0x0f;
	    sd->splash_fg_color = (splash_default >> 4) & 0x0f;
	    sd->splash_state    = splash_default & 1;
	}
	if (sd->splash_text_xo + sd->splash_text_wi > width || sd->splash_text_yo + sd->splash_text_he > height) {
	    splash_free(vc, info);
	    printk(KERN_INFO " found, but has oversized text area!\n");
	    return -1;
	}
	if (!vc_cons[unit].d || info->fbops != &vesafb_ops) {
	    splash_free(vc, info);
	    printk(KERN_INFO " found, but framebuffer can't handle it!\n");
	    return -1;
	}
	printk(KERN_INFO "...found (%dx%d, %d bytes, v%d).\n", width, height, splash_size, version);
	if (version == 1) {
	    printk(KERN_WARNING "bootsplash: Using deprecated v1 header. Updating your splash utility recommended.\n");
	    printk(KERN_INFO    "bootsplash: Find the latest version at http://www.bootsplash.org/\n");
	}

	/* fake penguin box for older formats */
	if (version == 1)
	    boxcnt = splash_mkpenguin(sd, sd->splash_text_xo + 10, sd->splash_text_yo + 10, sd->splash_text_wi - 20, sd->splash_text_he - 20, 0xf0, 0xf0, 0xf0);
	else if (version == 2)
	    boxcnt = splash_mkpenguin(sd, splash_gets(ndata, 24), splash_gets(ndata, 26), splash_gets(ndata, 28), splash_gets(ndata, 30), splash_getb(ndata, 32), splash_getb(ndata, 33), splash_getb(ndata, 34));

	memcpy((char *)sd + sizeof(*sd) + (version < 3 ? boxcnt * 12 : 0), ndata + len, splash_size);
	sd->splash_boxcount = boxcnt;
	sd->splash_boxes = (unsigned char *)sd + sizeof(*sd);
	sd->splash_palette = sd->splash_boxes + boxcnt * 12;
	sd->splash_jpeg = sd->splash_palette + palcnt;
	sd->splash_palcnt = palcnt / 3;
	sd->splash_dosilent = sd->splash_silentjpeg != 0;
	return unit;
    }
    printk(KERN_INFO "...no good signature found.\n");
    return -1;
}

int splash_verbose(void) 
{
    struct vc_data *vc;
    struct fb_info *info;

    if (!splash_usesilent)
        return 0;

    vc = vc_cons[0].d;

    if (!vc || !vc->vc_splash_data || !vc->vc_splash_data->splash_state)
	return 0;
    if (fg_console != vc->vc_num)
	return 0;
    if (!vc->vc_splash_data->splash_silentjpeg || !vc->vc_splash_data->splash_dosilent)
	return 0;
    vc->vc_splash_data->splash_dosilent = 0;
    info = registered_fb[(int)con2fb_map[0]];
    if (!info->silent_screen_base)
	return 0;
    splashcopy(info->silent_screen_base, info->screen_base, info->var.yres, info->var.xres, info->fix.line_length, info->fix.line_length);
    info->screen_base = info->silent_screen_base;
    info->silent_screen_base = 0;
    return 1;
}

static void splash_off(struct fb_info *info)
{
	if (info->silent_screen_base)
		info->screen_base = info->silent_screen_base;
	info->silent_screen_base = 0;
	info->splash_data = 0;
	if (info->splash_pic)
		vfree(info->splash_pic);
	info->splash_pic = 0;
	info->splash_pic_size = 0;
}

int splash_prepare(struct vc_data *vc, struct fb_info *info)
{
	int err;
        int width, height, depth, size, sbytes;

	if (!vc->vc_splash_data || !vc->vc_splash_data->splash_state) {
		if (decdata)
			vfree(decdata);
		decdata = 0;
		splash_off(info);
		return -1;
	}

        width = info->var.xres;
        height = info->var.yres;
        depth = info->var.bits_per_pixel;
	if (depth != 16) {	/* Other targets might need fixing */
		splash_off(info);
		return -2;
	}

	sbytes = ((width + 15) & ~15) * (depth >> 3);
	size = sbytes * ((height + 15) & ~15);
	if (size != info->splash_pic_size)
		splash_off(info);
	if (!info->splash_pic)
		info->splash_pic = vmalloc(size);

	if (!info->splash_pic) {
		printk(KERN_INFO "bootsplash: not enough memory.\n");
		splash_off(info);
		return -3;
	}

	if (!decdata)
		decdata = vmalloc(sizeof(*decdata));

	if (vc->vc_splash_data->splash_silentjpeg && vc->vc_splash_data->splash_dosilent) {
		/* fill area after framebuffer with other jpeg */
		if ((err = jpeg_decode(vc->vc_splash_data->splash_silentjpeg, info->splash_pic, 
			 ((width + 15) & ~15), ((height + 15) & ~15), depth, decdata))) {
			printk(KERN_INFO "bootsplash: error while decompressing silent picture: %s (%d)\n", jpg_errors[err - 1], err);
			if (info->silent_screen_base)
				info->screen_base = info->silent_screen_base;
			vc->vc_splash_data->splash_dosilent = 0;
		} else {
			if (vc->vc_splash_data->splash_sboxcount)
				boxit(info->splash_pic, sbytes, vc->vc_splash_data->splash_sboxes, 
					vc->vc_splash_data->splash_sboxcount, vc->vc_splash_data->splash_percent, 0);

			if (!info->silent_screen_base)
				info->silent_screen_base = info->screen_base;
			splashcopy(info->silent_screen_base, info->splash_pic, info->var.yres, info->var.xres, info->fix.line_length, sbytes);
			info->screen_base = info->silent_screen_base + info->fix.line_length * info->var.yres;
		}
	} else if (info->silent_screen_base)
		info->screen_base = info->silent_screen_base;

	if ((err = jpeg_decode(vc->vc_splash_data->splash_jpeg, info->splash_pic, 
		 ((width + 15) & ~15), ((height + 15) & ~15), depth, decdata))) {
		printk(KERN_INFO "bootsplash: error while decompressing picture: %s (%d) .\n", jpg_errors[err - 1], err);
		splash_off(info);
		return -4;
	}
	info->splash_pic_size = size;
	info->splash_bytes = sbytes;
	if (vc->vc_splash_data->splash_boxcount)
		boxit(info->splash_pic, sbytes, vc->vc_splash_data->splash_boxes, vc->vc_splash_data->splash_boxcount, vc->vc_splash_data->splash_percent, 0);
	if (vc->vc_splash_data->splash_state)
		info->splash_data = vc->vc_splash_data;
	else
		splash_off(info);
	return 0;
}


#ifdef CONFIG_PROC_FS

#include <linux/proc_fs.h>

static int splash_read_proc(char *buffer, char **start, off_t offset, int size,
			int *eof, void *data);
static int splash_write_proc(struct file *file, const char *buffer,
			unsigned long count, void *data);
static int splash_status(struct vc_data *vc);
static int splash_recolor(struct vc_data *vc);
static int splash_proc_register(void);

static struct proc_dir_entry *proc_splash;

static int splash_recolor(struct vc_data *vc)
{
	if (!vc->vc_splash_data)
	    return -1;
	if (!vc->vc_splash_data->splash_state)
	    return 0;
	con_remap_def_color(vc->vc_num, vc->vc_splash_data->splash_color << 4 | vc->vc_splash_data->splash_fg_color);
	if (fg_console == vc->vc_num) {
		update_region(fg_console,
			      vc->vc_origin + vc->vc_size_row * vc->vc_top,
			      vc->vc_size_row * (vc->vc_bottom - vc->vc_top) / 2);
	}
	return 0;
}

static int splash_status(struct vc_data *vc)
{
	struct fb_info *info;
	printk(KERN_INFO "bootsplash: status on console %d changed to %s\n", vc->vc_num, vc->vc_splash_data && vc->vc_splash_data->splash_state ? "on" : "off");

	info = registered_fb[(int) con2fb_map[vc->vc_num]];
	if (fg_console == vc->vc_num)
		splash_prepare(vc, info);
	if (vc->vc_splash_data && vc->vc_splash_data->splash_state) {
		con_remap_def_color(vc->vc_num, vc->vc_splash_data->splash_color << 4 | vc->vc_splash_data->splash_fg_color);
		/* vc_resize also calls con_switch which resets yscroll */
		vc_resize(vc->vc_num, vc->vc_splash_data->splash_text_wi / vc->vc_font.width, vc->vc_splash_data->splash_text_he / vc->vc_font.height);
		if (fg_console == vc->vc_num) {
			update_region(fg_console,
				      vc->vc_origin + vc->vc_size_row * vc->vc_top,
				      vc->vc_size_row * (vc->vc_bottom - vc->vc_top) / 2);
			splash_clear_margins(vc->vc_splash_data, vc, info, 0);
		}
	} else {
	  	/* Switch bootsplash off */
		con_remap_def_color(vc->vc_num, 0x07);
		vc_resize(vc->vc_num, info->var.xres / vc->vc_font.width, info->var.yres / vc->vc_font.height);
	}
	return 0;
}

static int splash_read_proc(char *buffer, char **start, off_t offset, int size,
			int *eof, void *data)
{
	int len = 0;
	off_t begin = 0;
	struct vc_data *vc = vc_cons[0].d;
	struct fb_info *info = registered_fb[(int)con2fb_map[0]];
	int color = vc->vc_splash_data ? vc->vc_splash_data->splash_color << 4 |
			vc->vc_splash_data->splash_fg_color : splash_default >> 4;
	int status = vc->vc_splash_data ? vc->vc_splash_data->splash_state & 1 : 0;
	len += sprintf(buffer + len, "Splash screen v%s (0x%02x, %dx%d%s): %s\n",
		        SPLASH_VERSION, color, info->var.xres, info->var.yres,
			(vc->vc_splash_data ?  vc->vc_splash_data->splash_dosilent : 0)? ", silent" : "",
					status ? "on" : "off");
	if (offset >= begin + len)
		return 0;

	*start = buffer + (begin - offset);

	return (size < begin + len - offset ? size : begin + len - offset);
}

static int splash_write_proc(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
        int new, unit;
	struct vc_data *vc;
	
	if (!buffer || !splash_default)
		return count;

	acquire_console_sem();
	if (!strncmp(buffer, "show", 4) || !strncmp(buffer, "hide", 4)) {
		int pe, oldpe;

		vc = vc_cons[0].d;
		if (buffer[4] == ' ' && buffer[5] == 'p')
			pe = 0;
		else if (buffer[4] == '\n')
			pe = 65535;
		else
			pe = simple_strtoul(buffer + 5, NULL, 0);
		if (pe < 0)
			pe = 0;
		if (pe > 65535)
			pe = 65535;
		if (*buffer == 'h')
			pe = 65535 - pe;
		pe += pe > 32767;
		if (vc->vc_splash_data && vc->vc_splash_data->splash_percent != pe) {
			struct fb_info *info;

			oldpe = vc->vc_splash_data->splash_percent;
			vc->vc_splash_data->splash_percent = pe;
			if (fg_console != 0 || !vc->vc_splash_data->splash_state) {
				release_console_sem();
				return count;
			}
			info = registered_fb[(int) con2fb_map[vc->vc_num]];
			if (!vc->vc_splash_data->splash_overpaintok || pe == 65536 || pe < oldpe) {
				if (splash_hasinter(vc->vc_splash_data->splash_boxes, vc->vc_splash_data->splash_boxcount))
					splash_status(vc);
				else
					splash_prepare(vc, info);
			} else {
				if (vc->vc_splash_data->splash_silentjpeg && vc->vc_splash_data->splash_dosilent && info->silent_screen_base)
					boxit(info->silent_screen_base, info->fix.line_length, vc->vc_splash_data->splash_sboxes, vc->vc_splash_data->splash_sboxcount, vc->vc_splash_data->splash_percent, 1);
				boxit(info->screen_base, info->fix.line_length, vc->vc_splash_data->splash_boxes, vc->vc_splash_data->splash_boxcount, vc->vc_splash_data->splash_percent, 1);
			}
		}
		release_console_sem();
		return count;
	}
	if (!strncmp(buffer,"silent\n",7) || !strncmp(buffer,"verbose\n",8)) {
		vc = vc_cons[0].d;
		if (vc->vc_splash_data && vc->vc_splash_data->splash_silentjpeg) {
		    if (vc->vc_splash_data->splash_dosilent != (buffer[0] == 's')) {
			vc->vc_splash_data->splash_dosilent = buffer[0] == 's';
			splash_status(vc);
		    }
		}
		release_console_sem();
		return count;
	}
	if (!strncmp(buffer,"freesilent\n",11)) {
		vc = vc_cons[0].d;
		if (vc->vc_splash_data && vc->vc_splash_data->splash_silentjpeg) {
		    printk(KERN_INFO "bootsplash: freeing silent jpeg\n");
		    vc->vc_splash_data->splash_silentjpeg = 0;
		    vfree(vc->vc_splash_data->splash_sboxes);
		    vc->vc_splash_data->splash_sboxes = 0;
		    vc->vc_splash_data->splash_sboxcount = 0;
		    if (vc->vc_splash_data->splash_dosilent)
			splash_status(vc);
		    vc->vc_splash_data->splash_dosilent = 0;
		}
		release_console_sem();
		return count;
	}

	if (!strncmp(buffer, "BOOTSPL", 7)) {
	    int up = -1;
	    unit = splash_getraw((unsigned char *)buffer, (unsigned char *)buffer + count, &up);
	    if (unit >= 0) {
		vc = vc_cons[unit].d;
		if (up == -1)
		    splash_status(vc);
		else {
		    struct fb_info *info = registered_fb[(int) con2fb_map[vc->vc_num]];
		    if ((up & 2) != 0 && vc->vc_splash_data->splash_silentjpeg && vc->vc_splash_data->splash_dosilent && info->silent_screen_base)
			boxit(info->silent_screen_base, info->fix.line_length, vc->vc_splash_data->splash_sboxes, vc->vc_splash_data->splash_sboxcount, vc->vc_splash_data->splash_percent, 1);
		    if ((up & 1) != 0)
			    boxit(info->screen_base, info->fix.line_length, vc->vc_splash_data->splash_boxes, vc->vc_splash_data->splash_boxcount, vc->vc_splash_data->splash_percent, 1);
		}
	    }
	    release_console_sem();
	    return count;
	}
	vc = vc_cons[0].d;
	if (!vc->vc_splash_data) {
		release_console_sem();
		return count;
	}
	if (buffer[0] == 't') {
	        vc->vc_splash_data->splash_state ^= 1;
		splash_status(vc);
		release_console_sem();
		return count;
	}
	new = simple_strtoul(buffer, NULL, 0);
	if (new > 1) {
		/* expert user */
		vc->vc_splash_data->splash_color    = new >> 8 & 0xff;
		vc->vc_splash_data->splash_fg_color = new >> 4 & 0x0f;
	}
	if ((new & 1) == vc->vc_splash_data->splash_state)
		splash_recolor(vc);
	else {
		vc->vc_splash_data->splash_state = new & 1;
		splash_status(vc);
	}
	release_console_sem();
	return count;
}

static int splash_proc_register(void)
{
	if ((proc_splash = create_proc_entry("splash", 0, 0))) {
		proc_splash->read_proc = splash_read_proc;
		proc_splash->write_proc = splash_write_proc;
		return 0;
	}
	return 1;
}

# if 0
static int splash_proc_unregister(void)
{
	if (proc_splash)
		remove_proc_entry("splash", 0);
	return 0;
}
# endif
#endif	/* CONFIG_PROC_FS */

void splash_init(void)
{
	struct fb_info *info;
	struct vc_data *vc;
	int isramfs = 1;
	int fd;
	int len;
	int max_len = 1024*1024*2;
	char *mem;

	if (splash_registered)
		return;
	vc = vc_cons[0].d;
	info = registered_fb[0];
	if (!vc || !info || info->var.bits_per_pixel != 16)
		return;
#ifdef CONFIG_PROC_FS
	splash_proc_register();
#endif
	splash_registered = 1;
	if (vc->vc_splash_data)
		return;
	if ((fd = sys_open("/bootsplash", O_RDONLY, 0)) < 0) {
		isramfs = 0;
		fd = sys_open("/initrd.image", O_RDONLY, 0);
	}
	if (fd < 0)
		return;
	if ((len = (int)sys_lseek(fd, (off_t)0, 2)) <= 0) {
		sys_close(fd);
		return;
	}
	/* Don't look for more than the last 2MB */
	if (len > max_len) {
		printk( KERN_INFO "bootsplash: scanning last %dMB of initrd for signature\n",
				max_len>>20);
		sys_lseek(fd, (off_t)(len - max_len), 0);
		len = max_len;
	} else {
		sys_lseek(fd, (off_t)0, 0);
	}

	mem = vmalloc(len);
	if (mem) {
		acquire_console_sem();
		if ((int)sys_read(fd, mem, len) == len && splash_getraw((unsigned char *)mem, (unsigned char *)mem + len, (int *)0) == 0 && vc->vc_splash_data)
			vc->vc_splash_data->splash_state = splash_default & 1;
		release_console_sem();
		vfree(mem);
	}
	sys_close(fd);
	if (isramfs)
		sys_unlink("/bootsplash");
	return;
}

