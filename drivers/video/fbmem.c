/*
 *  linux/drivers/video/fbmem.c
 *
 *  Copyright (C) 1994 Martin Schaller
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#include <linux/devfs_fs_kernel.h>

#if defined(__mc68000__) || defined(CONFIG_APUS)
#include <asm/setup.h>
#endif

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/fb.h>
#define INCLUDE_LINUX_LOGO_DATA
#include <asm/linux_logo.h>

#ifdef CONFIG_FRAMEBUFFER_CONSOLE
#include "console/fbcon.h"
#endif
    /*
     *  Frame buffer device initialization and setup routines
     */

extern int acornfb_init(void);
extern int acornfb_setup(char*);
extern int amifb_init(void);
extern int amifb_setup(char*);
extern int anakinfb_init(void);
extern int atafb_init(void);
extern int atafb_setup(char*);
extern int macfb_init(void);
extern int macfb_setup(char*);
extern int cyberfb_init(void);
extern int cyberfb_setup(char*);
extern int pm2fb_init(void);
extern int pm2fb_setup(char*);
extern int pm3fb_init(void);
extern int pm3fb_setup(char*);
extern int clps711xfb_init(void);
extern int cyber2000fb_init(void);
extern int cyber2000fb_setup(char*);
extern int retz3fb_init(void);
extern int retz3fb_setup(char*);
extern int clgenfb_init(void);
extern int clgenfb_setup(char*);
extern int hitfb_init(void);
extern int vfb_init(void);
extern int vfb_setup(char*);
extern int offb_init(void);
extern int atyfb_init(void);
extern int atyfb_setup(char*);
extern int aty128fb_init(void);
extern int aty128fb_setup(char*);
extern int neofb_init(void);
extern int neofb_setup(char*);
extern int igafb_init(void);
extern int igafb_setup(char*);
extern int imsttfb_init(void);
extern int imsttfb_setup(char*);
extern int dnfb_init(void);
extern int tgafb_init(void);
extern int tgafb_setup(char*);
extern int virgefb_init(void);
extern int virgefb_setup(char*);
extern int resolver_video_setup(char*);
extern int s3triofb_init(void);
extern int vesafb_init(void);
extern int vesafb_setup(char*);
extern int vga16fb_init(void);
extern int vga16fb_setup(char*);
extern int hgafb_init(void);
extern int hgafb_setup(char*);
extern int matroxfb_init(void);
extern int matroxfb_setup(char*);
extern int hpfb_init(void);
extern int sbusfb_init(void);
extern int sbusfb_setup(char*);
extern int control_init(void);
extern int control_setup(char*);
extern int platinum_init(void);
extern int platinum_setup(char*);
extern int valkyriefb_init(void);
extern int valkyriefb_setup(char*);
extern int chips_init(void);
extern int g364fb_init(void);
extern int sa1100fb_init(void);
extern int fm2fb_init(void);
extern int fm2fb_setup(char*);
extern int q40fb_init(void);
extern int sun3fb_init(void);
extern int sun3fb_setup(char *);
extern int sgivwfb_init(void);
extern int sgivwfb_setup(char*);
extern int rivafb_init(void);
extern int rivafb_setup(char*);
extern int tdfxfb_init(void);
extern int tdfxfb_setup(char*);
extern int tridentfb_init(void);
extern int tridentfb_setup(char*);
extern int sisfb_init(void);
extern int sisfb_setup(char*);
extern int stifb_init(void);
extern int stifb_setup(char*);
extern int pmagbafb_init(void);
extern int pmagbbfb_init(void);
extern int maxinefb_init(void);
extern int tx3912fb_init(void);
extern int tx3912fb_setup(char*);
extern int radeonfb_init(void);
extern int radeonfb_setup(char*);
extern int e1355fb_init(void);
extern int e1355fb_setup(char*);
extern int pvr2fb_init(void);
extern int pvr2fb_setup(char*);
extern int sstfb_init(void);
extern int sstfb_setup(char*);
extern int i810fb_init(void);
extern int i810fb_setup(char*);

static struct {
	const char *name;
	int (*init)(void);
	int (*setup)(char*);
} fb_drivers[] __initdata = {

#ifdef CONFIG_FB_SBUS
	/*
	 * Sbusfb must be initialized _before_ other frame buffer devices that
	 * use PCI probing
	 */
	{ "sbus", sbusfb_init, sbusfb_setup },
#endif
	/*
	 * Chipset specific drivers that use resource management
	 */
#ifdef CONFIG_FB_RETINAZ3
	{ "retz3", retz3fb_init, retz3fb_setup },
#endif
#ifdef CONFIG_FB_AMIGA
	{ "amifb", amifb_init, amifb_setup },
#endif
#ifdef CONFIG_FB_ANAKIN
	{ "anakinfb", anakinfb_init, NULL },
#endif
#ifdef CONFIG_FB_CLPS711X
	{ "clps711xfb", clps711xfb_init, NULL },
#endif
#ifdef CONFIG_FB_CYBER
	{ "cyber", cyberfb_init, cyberfb_setup },
#endif
#ifdef CONFIG_FB_CYBER2000
	{ "cyber2000", cyber2000fb_init, cyber2000fb_setup },
#endif
#ifdef CONFIG_FB_PM2
	{ "pm2fb", pm2fb_init, pm2fb_setup },
#endif
#ifdef CONFIG_FB_PM3
	{ "pm3fb", pm3fb_init, pm3fb_setup },
#endif           
#ifdef CONFIG_FB_CLGEN
	{ "clgen", clgenfb_init, clgenfb_setup },
#endif
#ifdef CONFIG_FB_ATY
	{ "atyfb", atyfb_init, atyfb_setup },
#endif
#ifdef CONFIG_FB_MATROX
	{ "matrox", matroxfb_init, matroxfb_setup },
#endif
#ifdef CONFIG_FB_ATY128
	{ "aty128fb", aty128fb_init, aty128fb_setup },
#endif
#ifdef CONFIG_FB_NEOMAGIC
	{ "neo", neofb_init, neofb_setup },
#endif
#ifdef CONFIG_FB_VIRGE
	{ "virge", virgefb_init, virgefb_setup },
#endif
#ifdef CONFIG_FB_RIVA
	{ "riva", rivafb_init, rivafb_setup },
#endif
#ifdef CONFIG_FB_RADEON
	{ "radeon", radeonfb_init, radeonfb_setup },
#endif
#ifdef CONFIG_FB_CONTROL
	{ "controlfb", control_init, control_setup },
#endif
#ifdef CONFIG_FB_PLATINUM
	{ "platinumfb", platinum_init, platinum_setup },
#endif
#ifdef CONFIG_FB_VALKYRIE
	{ "valkyriefb", valkyriefb_init, valkyriefb_setup },
#endif
#ifdef CONFIG_FB_CT65550
	{ "chipsfb", chips_init, NULL },
#endif
#ifdef CONFIG_FB_IMSTT
	{ "imsttfb", imsttfb_init, imsttfb_setup },
#endif
#ifdef CONFIG_FB_S3TRIO
	{ "s3trio", s3triofb_init, NULL },
#endif 
#ifdef CONFIG_FB_FM2
	{ "fm2fb", fm2fb_init, fm2fb_setup },
#endif 
#ifdef CONFIG_FB_SIS
	{ "sisfb", sisfb_init, sisfb_setup },
#endif
#ifdef CONFIG_FB_TRIDENT
	{ "trident", tridentfb_init, tridentfb_setup },
#endif
#ifdef CONFIG_FB_I810
	{ "i810fb", i810fb_init, i810fb_setup },
#endif	
#ifdef CONFIG_FB_STI
	{ "stifb", stifb_init, stifb_setup },
#endif

	/*
	 * Generic drivers that are used as fallbacks
	 * 
	 * These depend on resource management and must be initialized
	 * _after_ all other frame buffer devices that use resource
	 * management!
	 */

#ifdef CONFIG_FB_OF
	{ "offb", offb_init, NULL },
#endif
#ifdef CONFIG_FB_VESA
	{ "vesa", vesafb_init, vesafb_setup },
#endif 

	/*
	 * Chipset specific drivers that don't use resource management (yet)
	 */

#ifdef CONFIG_FB_3DFX
	{ "tdfx", tdfxfb_init, tdfxfb_setup },
#endif
#ifdef CONFIG_FB_SGIVW
	{ "sgivw", sgivwfb_init, sgivwfb_setup },
#endif
#ifdef CONFIG_FB_ACORN
	{ "acorn", acornfb_init, acornfb_setup },
#endif
#ifdef CONFIG_FB_ATARI
	{ "atafb", atafb_init, atafb_setup },
#endif
#ifdef CONFIG_FB_MAC
	{ "macfb", macfb_init, macfb_setup },
#endif
#ifdef CONFIG_FB_HGA
	{ "hga", hgafb_init, hgafb_setup },
#endif 
#ifdef CONFIG_FB_IGA
	{ "igafb", igafb_init, igafb_setup },
#endif
#ifdef CONFIG_APOLLO
	{ "apollo", dnfb_init, NULL },
#endif
#ifdef CONFIG_FB_Q40
	{ "q40fb", q40fb_init, NULL },
#endif
#ifdef CONFIG_FB_TGA
	{ "tga", tgafb_init, tgafb_setup },
#endif
#ifdef CONFIG_FB_HP300
	{ "hpfb", hpfb_init, NULL },
#endif 
#ifdef CONFIG_FB_G364
	{ "g364", g364fb_init, NULL },
#endif
#ifdef CONFIG_FB_SA1100
	{ "sa1100", sa1100fb_init, NULL },
#endif
#ifdef CONFIG_FB_SUN3
	{ "sun3", sun3fb_init, sun3fb_setup },
#endif
#ifdef CONFIG_FB_HIT
	{ "hitfb", hitfb_init, NULL },
#endif
#ifdef CONFIG_FB_TX3912
	{ "tx3912", tx3912fb_init, tx3912fb_setup },
#endif
#ifdef CONFIG_FB_E1355
	{ "e1355fb", e1355fb_init, e1355fb_setup },
#endif
#ifdef CONFIG_FB_PVR2
	{ "pvr2", pvr2fb_init, pvr2fb_setup },
#endif
#ifdef CONFIG_FB_PMAG_BA
	{ "pmagbafb", pmagbafb_init, NULL },
#endif          
#ifdef CONFIG_FB_PMAGB_B
	{ "pmagbbfb", pmagbbfb_init, NULL },
#endif
#ifdef CONFIG_FB_MAXINE
	{ "maxinefb", maxinefb_init, NULL },
#endif            
#ifdef CONFIG_FB_VOODOO1
	{ "sst", sstfb_init, sstfb_setup },
#endif
	/*
	 * Generic drivers that don't use resource management (yet)
	 */

#ifdef CONFIG_FB_VGA16
	{ "vga16", vga16fb_init, vga16fb_setup },
#endif 

#ifdef CONFIG_GSP_RESOLVER
	/* Not a real frame buffer device... */
	{ "resolver", NULL, resolver_video_setup },
#endif

#ifdef CONFIG_FB_VIRTUAL
	/*
	 * Vfb must be last to avoid that it becomes your primary display if
	 * other display devices are present
	 */
	{ "vfb", vfb_init, vfb_setup },
#endif
};

#define NUM_FB_DRIVERS	(sizeof(fb_drivers)/sizeof(*fb_drivers))

extern const char *global_mode_option;

static initcall_t pref_init_funcs[FB_MAX];
static int num_pref_init_funcs __initdata = 0;
struct fb_info *registered_fb[FB_MAX];
int num_registered_fb;

#ifdef CONFIG_FB_OF
static int ofonly __initdata = 0;
#endif

#define LOGO_H		80
#define LOGO_W		80

static inline unsigned safe_shift(unsigned d, int n)
{
	return n < 0 ? d >> -n : d << n;
}

static void __init fb_set_logocmap(struct fb_info *info)
{
	struct fb_cmap palette_cmap;
	u16 palette_green[16];
	u16 palette_blue[16];
	u16 palette_red[16];
	int i, j, n;

	palette_cmap.start = 0;
	palette_cmap.len = 16;
	palette_cmap.red = palette_red;
	palette_cmap.green = palette_green;
	palette_cmap.blue = palette_blue;
	palette_cmap.transp = NULL;

	for (i = 0; i < LINUX_LOGO_COLORS; i += n) {
		n = LINUX_LOGO_COLORS - i;
		/* palette_cmap provides space for only 16 colors at once */
		if (n > 16)
			n = 16;
		palette_cmap.start = 32 + i;
		palette_cmap.len = n;
		for (j = 0; j < n; ++j) {
			palette_cmap.red[j] =
				(linux_logo_red[i + j] << 8) |
				 linux_logo_red[i + j];
			palette_cmap.green[j] =
				(linux_logo_green[i + j] << 8) |
				 linux_logo_green[i + j];
			palette_cmap.blue[j] =
				(linux_logo_blue[i + j] << 8) |
				 linux_logo_blue[i + j];
		}
		fb_set_cmap(&palette_cmap, 1, info);
	}
}

static void  __init fb_set_logo_truepalette(struct fb_info *info, u32 *palette)
{
	unsigned char mask[9] = { 0,0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff };
	unsigned char redmask, greenmask, bluemask;
	int redshift, greenshift, blueshift;
	int i;

	/*
	 * We have to create a temporary palette since console palette is only
	 * 16 colors long.
	 */
	/* Bug: Doesn't obey msb_right ... (who needs that?) */
	redmask   = mask[info->var.red.length   < 8 ? info->var.red.length   : 8];
	greenmask = mask[info->var.green.length < 8 ? info->var.green.length : 8];
	bluemask  = mask[info->var.blue.length  < 8 ? info->var.blue.length  : 8];
	redshift   = info->var.red.offset   - (8 - info->var.red.length);
	greenshift = info->var.green.offset - (8 - info->var.green.length);
	blueshift  = info->var.blue.offset  - (8 - info->var.blue.length);

	for ( i = 0; i < LINUX_LOGO_COLORS; i++) {
		palette[i+32] = (safe_shift((linux_logo_red[i]   & redmask), redshift) |
				 safe_shift((linux_logo_green[i] & greenmask), greenshift) |
				 safe_shift((linux_logo_blue[i]  & bluemask), blueshift));
	}
}

static void __init fb_set_logo_directpalette(struct fb_info *info, u32 *palette)
{
	int redshift, greenshift, blueshift;
	int i;

	redshift = info->var.red.offset;
	greenshift = info->var.green.offset;
	blueshift = info->var.blue.offset;

	for (i = 32; i < LINUX_LOGO_COLORS; i++)
		palette[i] = i << redshift | i << greenshift | i << blueshift;
}

static void __init fb_set_logo(struct fb_info *info, u8 *logo, int needs_logo)
{
	int i, j;

	switch (needs_logo) {
	case 4:
		for (i = 0; i < (LOGO_W * LOGO_H)/2; i++) {
			logo[i*2] = linux_logo16[i] >> 4;
			logo[(i*2)+1] = linux_logo16[i] & 0xf;
		}
		break;
	case 1:
	case ~1:
	default:
		for (i = 0; i < (LOGO_W * LOGO_H)/8; i++)
			for (j = 0; j < 8; j++)
				logo[i*8 + j] = (linux_logo_bw[i] &  (7 - j)) ?
						((needs_logo == 1) ? 1 : 0) :
						((needs_logo == 1) ? 0 : 1);
			break;
	}
}

/*
 * Three (3) kinds of logo maps exist.  linux_logo (>16 colors), linux_logo_16
 * (16 colors) and linux_logo_bw (2 colors).  Depending on the visual format and
 * color depth of the framebuffer, the DAC, the pseudo_palette, and the logo data
 * will be adjusted accordingly.
 *
 * Case 1 - linux_logo:
 * Color exceeds the number of console colors (16), thus we set the hardware DAC
 * using fb_set_cmap() appropriately.  The "needs_cmapreset"  flag will be set.
 *
 * For visuals that require color info from the pseudo_palette, we also construct
 * one for temporary use. The "needs_directpalette" or "needs_truepalette" flags
 * will be set.
 *
 * Case 2 - linux_logo_16:
 * The number of colors just matches the console colors, thus there is no need
 * to set the DAC or the pseudo_palette.  However, the bitmap is packed, ie,
 * each byte contains color information for two pixels (upper and lower nibble).
 * To be consistent with fb_imageblit() usage, we therefore separate the two
 * nibbles into separate bytes. The "needs_logo" flag will be set to 4.
 *
 * Case 3 - linux_logo_bw:
 * This is similar with Case 2.  Each byte contains information for 8 pixels.
 * We isolate each bit and expand each into a byte. The "needs_logo" flag will
 * be set to 1.
 */
int fb_show_logo(struct fb_info *info)
{
	unsigned char *fb = info->screen_base, *logo_new = NULL;
	u32 *palette = NULL, *saved_palette = NULL;
	int needs_directpalette = 0;
	int needs_truepalette = 0;
	int needs_cmapreset = 0;
	struct fb_image image;
	int needs_logo = 0;
	int done = 0, x;

	/* Return if the frame buffer is not mapped */
	if (!fb || !info->fbops->fb_imageblit)
		return 0;

	image.depth = info->var.bits_per_pixel;

	/* reasonable default */
	if (image.depth >= 8)
		image.data = linux_logo;
	else if (image.depth >= 4)
		image.data = linux_logo16;
	else
		image.data = linux_logo_bw;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		needs_truepalette = 1;
		if (image.depth >= 4 && image.depth <= 8)
			needs_logo = 4;
		else if (image.depth < 4)
			needs_logo = 1;
		break;
	case FB_VISUAL_DIRECTCOLOR:
		if (image.depth >= 24) {
			needs_directpalette = 1;
			needs_cmapreset = 1;
		}
		/* 16 colors */
		else if (image.depth >= 16)
			needs_logo = 4;
		/* 2 colors */
		else
			needs_logo = 1;
		break;
	case FB_VISUAL_MONO01:
		/* reversed 0 = fg, 1 = bg */
		needs_logo = ~1;
		break;
	case FB_VISUAL_MONO10:
		needs_logo = 1;
		break;
	case FB_VISUAL_PSEUDOCOLOR:
	default:
		if (image.depth >= 8)
			needs_cmapreset = 1;
		/* fall through */
	case FB_VISUAL_STATIC_PSEUDOCOLOR:
		/* 16 colors */
		if (image.depth >= 4 && image.depth < 8)
			needs_logo = 4;
		/* 2 colors */
		else if (image.depth < 4)
			needs_logo = 1;
		break;
	}

	if (needs_cmapreset)
		fb_set_logocmap(info);

	if (needs_truepalette || needs_directpalette) {
		palette = kmalloc(256 * 4, GFP_KERNEL);
		if (palette == NULL)
			return 1;

		if (needs_truepalette)
			fb_set_logo_truepalette(info, palette);
		else
			fb_set_logo_directpalette(info, palette);

		saved_palette = info->pseudo_palette;
		info->pseudo_palette = palette;
	}

	if (needs_logo) {
		logo_new = kmalloc(LOGO_W * LOGO_H, GFP_KERNEL);
		if (logo_new == NULL) {
			if (palette)
				kfree(palette);
			if (saved_palette)
				info->pseudo_palette = saved_palette;
			return 1;
		}

		image.data = logo_new;
		fb_set_logo(info, logo_new, needs_logo);
	}

	image.width = LOGO_W;
	image.height = LOGO_H;
	image.dy = 0;

	for (x = 0; x < num_online_cpus() * (LOGO_W + 8) &&
	     x < info->var.xres - (LOGO_W + 8); x += (LOGO_W + 8)) {
		image.dx = x;
		info->fbops->fb_imageblit(info, &image);
		done = 1;
	}

	if (palette != NULL)
		kfree(palette);
	if (saved_palette != NULL)
		info->pseudo_palette = saved_palette;
	if (logo_new != NULL)
		kfree(logo_new);
	return 0;
}

static int fbmem_read_proc(char *buf, char **start, off_t offset,
			   int len, int *eof, void *private)
{
	struct fb_info **fi;
	int clen;

	clen = 0;
	for (fi = registered_fb; fi < &registered_fb[FB_MAX] && len < 4000; fi++)
		if (*fi)
			clen += sprintf(buf + clen, "%d %s\n",
				        minor((*fi)->node),
				        (*fi)->fix.id);
	*start = buf + offset;
	if (clen > offset)
		clen -= offset;
	else
		clen = 0;
	return clen < len ? clen : len;
}

static ssize_t
fb_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = minor(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];

	if (!info || ! info->screen_base)
		return -ENODEV;

	if (info->fbops->fb_read)
		return info->fbops->fb_read(file, buf, count, ppos);
	
	if (p >= info->fix.smem_len)
	    return 0;
	if (count >= info->fix.smem_len)
	    count = info->fix.smem_len;
	if (count + p > info->fix.smem_len)
		count = info->fix.smem_len - p;
	if (count) {
	    char *base_addr;

	    base_addr = info->screen_base;
	    count -= copy_to_user(buf, base_addr+p, count);
	    if (!count)
		return -EFAULT;
	    *ppos += count;
	}
	return count;
}

static ssize_t
fb_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = minor(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	int err;

	if (!info || !info->screen_base)
		return -ENODEV;

	if (info->fbops->fb_write)
		return info->fbops->fb_write(file, buf, count, ppos);
	
	if (p > info->fix.smem_len)
	    return -ENOSPC;
	if (count >= info->fix.smem_len)
	    count = info->fix.smem_len;
	err = 0;
	if (count + p > info->fix.smem_len) {
	    count = info->fix.smem_len - p;
	    err = -ENOSPC;
	}
	if (count) {
	    char *base_addr;

	    base_addr = info->screen_base;
	    count -= copy_from_user(base_addr+p, buf, count);
	    *ppos += count;
	    err = -EFAULT;
	}
	if (count)
		return count;
	return err;
}

#ifdef CONFIG_KMOD
static void try_to_load(int fb)
{
	char modname[16];

	sprintf(modname, "fb%d", fb);
	request_module(modname);
}
#endif /* CONFIG_KMOD */

int
fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
        int xoffset = var->xoffset;
        int yoffset = var->yoffset;
        int err;

        if (xoffset < 0 || yoffset < 0 || !info->fbops->fb_pan_display ||
            xoffset + info->var.xres > info->var.xres_virtual ||
            yoffset + info->var.yres > info->var.yres_virtual)
                return -EINVAL;
	if ((err = info->fbops->fb_pan_display(var, info)))
		return err;
        info->var.xoffset = var->xoffset;
        info->var.yoffset = var->yoffset;
        if (var->vmode & FB_VMODE_YWRAP)
                info->var.vmode |= FB_VMODE_YWRAP;
        else
                info->var.vmode &= ~FB_VMODE_YWRAP;
        return 0;
}

int
fb_set_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int err;

	if (memcmp(&info->var, var, sizeof(struct fb_var_screeninfo))) {
		if (!info->fbops->fb_check_var) {
			*var = info->var;
			return 0;
		}

		if ((err = info->fbops->fb_check_var(var, info)))
			return err;

		if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
			info->var = *var;

			if (info->fbops->fb_set_par)
				info->fbops->fb_set_par(info);

			fb_pan_display(&info->var, info);

			fb_set_cmap(&info->cmap, 1, info);
		}
	}
	return 0;
}

int
fb_blank(int blank, struct fb_info *info)
{	
	/* ??? Varible sized stack allocation.  */
	u16 black[info->cmap.len];
	struct fb_cmap cmap;
	
	if (info->fbops->fb_blank && !info->fbops->fb_blank(blank, info))
		return 0;
	if (blank) { 
		memset(black, 0, info->cmap.len * sizeof(u16));
		cmap.red = cmap.green = cmap.blue = black;
		cmap.transp = info->cmap.transp ? black : NULL;
		cmap.start = info->cmap.start;
		cmap.len = info->cmap.len;
	} else
		cmap = info->cmap;
	return fb_set_cmap(&cmap, 1, info);
}

static int 
fb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	 unsigned long arg)
{
	int fbidx = minor(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
#ifdef CONFIG_FRAMEBUFFER_CONSOLE
	struct fb_con2fbmap con2fb;
#endif
	struct fb_cmap cmap;
	int i;
	
	if (! fb)
		return -ENODEV;
	switch (cmd) {
	case FBIOGET_VSCREENINFO:
		return copy_to_user((void *) arg, &info->var,
				    sizeof(var)) ? -EFAULT : 0;
	case FBIOPUT_VSCREENINFO:
		if (copy_from_user(&var, (void *) arg, sizeof(var)))
			return -EFAULT;
		i = fb_set_var(&var, info);
		if (i) return i;
		if (copy_to_user((void *) arg, &var, sizeof(var)))
			return -EFAULT;
		return 0;
	case FBIOGET_FSCREENINFO:
		return copy_to_user((void *) arg, &info->fix, sizeof(fix)) ? -EFAULT : 0;
	case FBIOPUTCMAP:
		if (copy_from_user(&cmap, (void *) arg, sizeof(cmap)))
			return -EFAULT;
		return (fb_set_cmap(&cmap, 0, info));
	case FBIOGETCMAP:
		if (copy_from_user(&cmap, (void *) arg, sizeof(cmap)))
			return -EFAULT;
		fb_copy_cmap(&info->cmap, &cmap, 0);
		return 0;
	case FBIOPAN_DISPLAY:
		if (copy_from_user(&var, (void *) arg, sizeof(var)))
			return -EFAULT;
		if ((i = fb_pan_display(&var, info)))
			return i;
		if (copy_to_user((void *) arg, &var, sizeof(var)))
			return -EFAULT;
		return i;
#ifdef CONFIG_FRAMEBUFFER_CONSOLE
	case FBIOGET_CON2FBMAP:
		if (copy_from_user(&con2fb, (void *)arg, sizeof(con2fb)))
			return -EFAULT;
		if (con2fb.console < 1 || con2fb.console > MAX_NR_CONSOLES)
		    return -EINVAL;
		con2fb.framebuffer = con2fb_map[con2fb.console-1];
		return copy_to_user((void *)arg, &con2fb,
				    sizeof(con2fb)) ? -EFAULT : 0;
	case FBIOPUT_CON2FBMAP:
		if (copy_from_user(&con2fb, (void *)arg, sizeof(con2fb)))
			return - EFAULT;
		if (con2fb.console < 0 || con2fb.console > MAX_NR_CONSOLES)
		    return -EINVAL;
		if (con2fb.framebuffer < 0 || con2fb.framebuffer >= FB_MAX)
		    return -EINVAL;
#ifdef CONFIG_KMOD
		if (!registered_fb[con2fb.framebuffer])
		    try_to_load(con2fb.framebuffer);
#endif /* CONFIG_KMOD */
		if (!registered_fb[con2fb.framebuffer])
		    return -EINVAL;
		if (con2fb.console != 0)
		    set_con2fb_map(con2fb.console-1, con2fb.framebuffer);
		else
		    /* set them all */
		    for (i = 0; i < MAX_NR_CONSOLES; i++)
			set_con2fb_map(i, con2fb.framebuffer);
		return 0;
#endif	/* CONFIG_FRAMEBUFFER_CONSOLE */
	case FBIOBLANK:
		return fb_blank(arg, info);
	default:
		if (fb->fb_ioctl == NULL)
			return -EINVAL;
		return fb->fb_ioctl(inode, file, cmd, arg, info);
	}
}

static int 
fb_mmap(struct file *file, struct vm_area_struct * vma)
{
	int fbidx = minor(file->f_dentry->d_inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	unsigned long off;
#if !defined(__sparc__) || defined(__sparc_v9__)
	unsigned long start;
	u32 len;
#endif

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;
	off = vma->vm_pgoff << PAGE_SHIFT;
	if (!fb)
		return -ENODEV;
	if (fb->fb_mmap) {
		int res;
		lock_kernel();
		res = fb->fb_mmap(info, file, vma);
		unlock_kernel();
		return res;
	}

#if defined(__sparc__) && !defined(__sparc_v9__)
	/* Should never get here, all fb drivers should have their own
	   mmap routines */
	return -EINVAL;
#else
	/* !sparc32... */
	lock_kernel();

	/* frame buffer memory */
	start = info->fix.smem_start;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.smem_len);
	if (off >= len) {
		/* memory mapped io */
		off -= len;
		if (info->var.accel_flags) {
			unlock_kernel();
			return -EINVAL;
		}
		start = info->fix.mmio_start;
		len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.mmio_len);
	}
	unlock_kernel();
	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;
	/* This is an IO map - tell maydump to skip this VMA */
	vma->vm_flags |= VM_IO;
#if defined(__sparc_v9__)
	vma->vm_flags |= (VM_SHM | VM_LOCKED);
	if (io_remap_page_range(vma, vma->vm_start, off,
				vma->vm_end - vma->vm_start, vma->vm_page_prot, 0))
		return -EAGAIN;
#else
#if defined(__mc68000__)
#if defined(CONFIG_SUN3)
	pgprot_val(vma->vm_page_prot) |= SUN3_PAGE_NOCACHE;
#elif defined(CONFIG_MMU)
	if (CPU_IS_020_OR_030)
		pgprot_val(vma->vm_page_prot) |= _PAGE_NOCACHE030;
	if (CPU_IS_040_OR_060) {
		pgprot_val(vma->vm_page_prot) &= _CACHEMASK040;
		/* Use no-cache mode, serialized */
		pgprot_val(vma->vm_page_prot) |= _PAGE_NOCACHE_S;
	}
#endif
#elif defined(__powerpc__)
	pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE|_PAGE_GUARDED;
#elif defined(__alpha__)
	/* Caching is off in the I/O space quadrant by design.  */
#elif defined(__i386__) || defined(__x86_64__)
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) |= _PAGE_PCD;
#elif defined(__mips__)
	pgprot_val(vma->vm_page_prot) &= ~_CACHE_MASK;
	pgprot_val(vma->vm_page_prot) |= _CACHE_UNCACHED;
#elif defined(__arm__)
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#elif defined(__sh__)
	pgprot_val(vma->vm_page_prot) &= ~_PAGE_CACHABLE;
#elif defined(__hppa__)
	pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE;
#elif defined(__ia64__)
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
#else
#warning What do we have to do here??
#endif
	if (io_remap_page_range(vma, vma->vm_start, off,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
#endif /* !__sparc_v9__ */
	return 0;
#endif /* !sparc32 */
}

static int
fb_open(struct inode *inode, struct file *file)
{
	int fbidx = minor(inode->i_rdev);
	struct fb_info *info;
	int res = 0;

#ifdef CONFIG_KMOD
	if (!(info = registered_fb[fbidx]))
		try_to_load(fbidx);
#endif /* CONFIG_KMOD */
	if (!(info = registered_fb[fbidx]))
		return -ENODEV;
	if (!try_module_get(info->fbops->owner))
		return -ENODEV;
	if (info->fbops->fb_open) {
		res = info->fbops->fb_open(info,1);
		if (res)
			module_put(info->fbops->owner);
	}
	return res;
}

static int 
fb_release(struct inode *inode, struct file *file)
{
	int fbidx = minor(inode->i_rdev);
	struct fb_info *info;

	lock_kernel();
	info = registered_fb[fbidx];
	if (info->fbops->fb_release)
		info->fbops->fb_release(info,1);
	module_put(info->fbops->owner);
	unlock_kernel();
	return 0;
}

static struct file_operations fb_fops = {
	.owner =	THIS_MODULE,
	.read =		fb_read,
	.write =	fb_write,
	.ioctl =	fb_ioctl,
	.mmap =		fb_mmap,
	.open =		fb_open,
	.release =	fb_release,
#ifdef HAVE_ARCH_FB_UNMAPPED_AREA
	.get_unmapped_area = get_fb_unmapped_area,
#endif
};

/**
 *	register_framebuffer - registers a frame buffer device
 *	@fb_info: frame buffer info structure
 *
 *	Registers a frame buffer device @fb_info.
 *
 *	Returns negative errno on error, or zero for success.
 *
 */

int
register_framebuffer(struct fb_info *fb_info)
{
	char name_buf[12];
	int i;

	if (num_registered_fb == FB_MAX)
		return -ENXIO;
	num_registered_fb++;
	for (i = 0 ; i < FB_MAX; i++)
		if (!registered_fb[i])
			break;
	fb_info->node = mk_kdev(FB_MAJOR, i);
	registered_fb[i] = fb_info;
	sprintf(name_buf, "fb/%d", i);
	devfs_register(NULL, name_buf, DEVFS_FL_DEFAULT,
			FB_MAJOR, i, S_IFCHR | S_IRUGO | S_IWUGO,
			&fb_fops, NULL);
	return 0;
}


/**
 *	unregister_framebuffer - releases a frame buffer device
 *	@fb_info: frame buffer info structure
 *
 *	Unregisters a frame buffer device @fb_info.
 *
 *	Returns negative errno on error, or zero for success.
 *
 */

int
unregister_framebuffer(struct fb_info *fb_info)
{
	int i;

	i = minor(fb_info->node);
	if (!registered_fb[i])
		return -EINVAL;
	devfs_remove("fb/%d", i);
	registered_fb[i]=NULL;
	num_registered_fb--;
	return 0;
}


/**
 *	fbmem_init - init frame buffer subsystem
 *
 *	Initialize the frame buffer subsystem.
 *
 *	NOTE: This function is _only_ to be called by drivers/char/mem.c.
 *
 */

void __init 
fbmem_init(void)
{
	int i;

	create_proc_read_entry("fb", 0, 0, fbmem_read_proc, NULL);

	devfs_mk_dir(NULL, "fb", NULL);
	if (register_chrdev(FB_MAJOR,"fb",&fb_fops))
		printk("unable to get major %d for fb devs\n", FB_MAJOR);

#ifdef CONFIG_FB_OF
	if (ofonly) {
		offb_init();
		return;
	}
#endif

	/*
	 *  Probe for all builtin frame buffer devices
	 */
	for (i = 0; i < num_pref_init_funcs; i++)
		pref_init_funcs[i]();

	for (i = 0; i < NUM_FB_DRIVERS; i++)
		if (fb_drivers[i].init)
			fb_drivers[i].init();
}


/**
 *	video_setup - process command line options
 *	@options: string of options
 *
 *	Process command line options for frame buffer subsystem.
 *
 *	NOTE: This function is a __setup and __init function.
 *
 *	Returns zero.
 *
 */

int __init video_setup(char *options)
{
	int i, j;

	if (!options || !*options)
		return 0;
	   
#ifdef CONFIG_FB_OF
	if (!strcmp(options, "ofonly")) {
		ofonly = 1;
		return 0;
	}
#endif

	if (num_pref_init_funcs == FB_MAX)
		return 0;

	for (i = 0; i < NUM_FB_DRIVERS; i++) {
		j = strlen(fb_drivers[i].name);
		if (!strncmp(options, fb_drivers[i].name, j) &&
			options[j] == ':') {
			if (!strcmp(options+j+1, "off"))
				fb_drivers[i].init = NULL;
			else {
				if (fb_drivers[i].init) {
					pref_init_funcs[num_pref_init_funcs++] =
						fb_drivers[i].init;
					fb_drivers[i].init = NULL;
				}
				if (fb_drivers[i].setup)
					fb_drivers[i].setup(options+j+1);
			}
			return 0;
		}
	}

	/*
	 * If we get here no fb was specified.
	 * We consider the argument to be a global video mode option.
	 */
	global_mode_option = options;
	return 0;
}

__setup("video=", video_setup);

    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(register_framebuffer);
EXPORT_SYMBOL(unregister_framebuffer);
EXPORT_SYMBOL(num_registered_fb);
EXPORT_SYMBOL(registered_fb);
EXPORT_SYMBOL(fb_show_logo);
EXPORT_SYMBOL(fb_set_var);
EXPORT_SYMBOL(fb_blank);
EXPORT_SYMBOL(fb_pan_display);

MODULE_LICENSE("GPL");
