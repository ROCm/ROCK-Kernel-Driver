/*
 *  controlfb.c -- frame buffer device for the PowerMac 'control' display
 *
 *  Created 12 July 1998 by Dan Jacobowitz <dan@debian.org>
 *  Copyright (C) 1998 Dan Jacobowitz
 *
 *  Frame buffer structure from:
 *    drivers/video/chipsfb.c -- frame buffer device for
 *    Chips & Technologies 65550 chip.
 *
 *    Copyright (C) 1998 Paul Mackerras
 *
 *    This file is derived from the Powermac "chips" driver:
 *    Copyright (C) 1997 Fabio Riccardi.
 *    And from the frame buffer device for Open Firmware-initialized devices:
 *    Copyright (C) 1997 Geert Uytterhoeven.
 *
 *  Hardware information from:
 *    control.c: Console support for PowerMac "control" display adaptor.
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/selection.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif
#include <linux/adb.h>
#include <linux/cuda.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pgtable.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>
#include <video/macmodes.h>

#include "controlfb.h"

struct fb_par_control {
	int	vmode, cmode;
	int	xres, yres;
	int	vxres, vyres;
	int	xoffset, yoffset;
};

#define DIRTY(z) ((x)->z != (y)->z)
static inline int PAR_EQUAL(struct fb_par_control *x, struct fb_par_control *y)
{
	return (!DIRTY(vmode) && !DIRTY(cmode) && !DIRTY(xres)
		&& !DIRTY(yres) && !DIRTY(vxres) && !DIRTY(vyres)
		&& !DIRTY(xoffset) && !DIRTY(yoffset));
}
static inline int VAR_MATCH(struct fb_var_screeninfo *x, struct fb_var_screeninfo *y)
{
	return (!DIRTY(bits_per_pixel) && !DIRTY(xres)
		&& !DIRTY(yres) && !DIRTY(xres_virtual)
		&& !DIRTY(yres_virtual));
}

struct fb_info_control {
	struct fb_info			info;
/*	struct fb_fix_screeninfo	fix;
	struct fb_var_screeninfo	var;*/
	struct display			display;
	struct fb_par_control		par;
	struct {
		__u8 red, green, blue;
	}			palette[256];
	
	struct cmap_regs	*cmap_regs;
	unsigned long		cmap_regs_phys;
	
	struct control_regs	*control_regs;
	unsigned long		control_regs_phys;
	
	__u8			*frame_buffer;
	unsigned long		frame_buffer_phys;
	
	int			sense, control_use_bank2;
	unsigned long		total_vram;
	union {
#ifdef FBCON_HAS_CFB16
		u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB32
		u32 cfb32[16];
#endif
	} fbcon_cmap;
};

/******************** Prototypes for exported functions ********************/
static int control_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int control_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int control_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int control_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info);
static int control_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int control_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int control_mmap(struct fb_info *info, struct file *file,
                         struct vm_area_struct *vma);


static int controlfb_getcolreg(u_int regno, u_int *red, u_int *green,
			     u_int *blue, u_int *transp, struct fb_info *info);
static int controlfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info);

/******************** Prototypes for internal functions ********************/
static void control_par_to_fix(struct fb_par_control *par, struct fb_fix_screeninfo *fix,
	struct fb_info_control *p);
static void do_install_cmap(int con, struct fb_info *info);
static void control_set_dispsw(struct display *disp, int cmode, struct fb_info_control *p);

/************************* Internal variables *****************************/
static int currcon = 0;
static int par_set = 0;
static char fontname[40] __initdata = { 0 };
static int default_vmode = VMODE_NVRAM;
static int default_cmode = CMODE_NVRAM;

/*
 * Exported functions
 */
int control_init(void);
void control_setup(char *);

static void control_of_init(struct device_node *dp);
static int read_control_sense(struct fb_info_control *p);
static inline int control_vram_reqd(int video_mode, int color_mode);
static void set_control_clock(unsigned char *params);
static void control_set_hardware(struct fb_info_control *p, struct fb_par_control *par);
static inline void control_par_to_var(struct fb_par_control *par, struct fb_var_screeninfo *var);
static int control_var_to_par(struct fb_var_screeninfo *var,
	struct fb_par_control *par, const struct fb_info *fb_info);

static void control_init_info(struct fb_info *info, struct fb_info_control *p);
static void control_par_to_display(struct fb_par_control *par,
  struct display *disp, struct fb_fix_screeninfo *fix, struct fb_info_control *p);

static int controlfb_updatevar(int con, struct fb_info *info);

static struct fb_ops controlfb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	control_get_fix,
	fb_get_var:	control_get_var,
	fb_set_var:	control_set_var,
	fb_get_cmap:	control_get_cmap,
	fb_set_cmap:	control_set_cmap,
	fb_pan_display:	control_pan_display,
	fb_mmap:	control_mmap,
};



/********************  The functions for controlfb_ops ********************/

#ifdef MODULE
int init_module(void)
{
	struct device_node *dp;

	printk("Loading...\n");
	dp = find_devices("control");
	if (dp != 0)
		control_of_init(dp);
	else
		printk("Failed.\n");
	printk("Done.\n");
}

void cleanup_module(void)
{
	/* FIXME: clean up and release regions */
}
#endif

/*********** Providing our information to the user ************/

static int control_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;

	if(!par_set)
		printk(KERN_ERR "control_get_fix called with unset par!\n");
	if(con == -1) {
		control_par_to_fix(&p->par, fix, p);
	} else {
		struct fb_par_control par;
		
		control_var_to_par(&fb_display[con].var, &par, info);
		control_par_to_fix(&par, fix, p);
	}
	return 0;
}

static int control_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;

	if(!par_set)
		printk(KERN_ERR "control_get_var called with unset par!\n");
	if(con == -1) {
		control_par_to_var(&p->par, var);
	} else {
		*var = fb_display[con].var;
	}
	return 0;
}

/* Sets everything according to var */
/* No longer safe for use in console switching */
static int control_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;
	struct display *disp;
	struct fb_par_control par;
	int depthchange, err;
	int activate = var->activate;

	disp = (con >= 0) ? &fb_display[con] : info->disp;

	if((err = control_var_to_par(var, &par, info))) {
		printk (KERN_ERR "control_set_var: error calling control_var_to_par: %d.\n", err);
		return err;
	}
	
	control_par_to_var(&par, var);
	
	if ((activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW)
		return 0;

/* I know, we want to use fb_display[con], but grab certain info from p->var instead. */
/* [above no longer true] */
	depthchange = (disp->var.bits_per_pixel != var->bits_per_pixel);
	if(!VAR_MATCH(&disp->var, var)) {
		struct fb_fix_screeninfo	fix;
		control_par_to_fix(&par, &fix, p);
		control_par_to_display(&par, disp, &fix, p);
		if(info->changevar)
			(*info->changevar)(con);
	} else
		disp->var = *var;
	/*p->disp = *disp;*/

	
	if(con == currcon || con == -1) {
		control_set_hardware(p, &par);
	}
	if(depthchange) {
		if((err = fb_alloc_cmap(&disp->cmap, 0, 0)))
			return err;
		do_install_cmap(con, info);
	}
	return 0;
}

static int control_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *)info;
	struct fb_par_control *par = &p->par;
	
	if (var->xoffset != 0 || var->yoffset+var->yres > var->yres_virtual)
		return -EINVAL;
	fb_display[con].var.yoffset =  par->yoffset = var->yoffset;
	if(con == currcon)
		out_le32(&p->control_regs->start_addr.r,
		    par->yoffset * (par->vxres << par->cmode));
	return 0;
}

static int control_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	if (con == currcon)		/* current console? */
		return fb_get_cmap(cmap, kspc, controlfb_getcolreg, info);
	if (fb_display[con].cmap.len)	/* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0: 2);
	else {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
	}
	return 0;
}

static int control_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	struct display *disp = &fb_display[con];
	int err;

	if (disp->cmap.len == 0) {
		int size = disp->var.bits_per_pixel == 16 ? 32 : 256;
		err = fb_alloc_cmap(&disp->cmap, size, 0);
		if (err)
			return err;
	}
	if (con == currcon)
		return fb_set_cmap(cmap, kspc, controlfb_setcolreg, info);
	fb_copy_cmap(cmap, &disp->cmap, kspc ? 0 : 1);
	return 0;
}

/* Private mmap since we want to have a different caching on the framebuffer
 * for controlfb.
 * Note there's no locking in here; it's done in fb_mmap() in fbmem.c.
 */
static int control_mmap(struct fb_info *info, struct file *file,
                       struct vm_area_struct *vma)
{
       struct fb_ops *fb = info->fbops;
       struct fb_fix_screeninfo fix;
       struct fb_var_screeninfo var;
       unsigned long off, start;
       u32 len;

       fb->fb_get_fix(&fix, PROC_CONSOLE(info), info);
       off = vma->vm_pgoff << PAGE_SHIFT;

       /* frame buffer memory */
       start = fix.smem_start;
       len = PAGE_ALIGN((start & ~PAGE_MASK)+fix.smem_len);
       if (off >= len) {
               /* memory mapped io */
               off -= len;
               fb->fb_get_var(&var, PROC_CONSOLE(info), info);
               if (var.accel_flags)
                       return -EINVAL;
               start = fix.mmio_start;
               len = PAGE_ALIGN((start & ~PAGE_MASK)+fix.mmio_len);
               pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE|_PAGE_GUARDED;
       } else {
               /* framebuffer */
               pgprot_val(vma->vm_page_prot) |= _PAGE_WRITETHRU;
       }
       start &= PAGE_MASK;
       vma->vm_pgoff = off >> PAGE_SHIFT;
       if (io_remap_page_range(vma->vm_start, off,
           vma->vm_end - vma->vm_start, vma->vm_page_prot))
               return -EAGAIN;

       return 0;
}


/********************  End of controlfb_ops implementation  ********************/
/* (new one that is) */


static int controlfb_switch(int con, struct fb_info *info)
{
	struct fb_info_control	*p = (struct fb_info_control *)info;
	struct fb_par_control	par;
	int oldcon = currcon;

	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, 1, controlfb_getcolreg,
			    info);
	currcon = con;

	control_var_to_par(&fb_display[con].var, &par, info);
	control_set_hardware(p, &par);
	control_set_dispsw(&fb_display[con], par.cmode, p);

	if(fb_display[oldcon].var.yoffset != fb_display[con].var.yoffset)
		controlfb_updatevar(con, info);

	do_install_cmap(con, info);
	return 1;
}

static int controlfb_updatevar(int con, struct fb_info *info)
{
	struct fb_info_control	*p = (struct fb_info_control *)info;

	if(con != currcon)
		return 0;
	/* imsttfb blanks the unused bottom of the screen here...hmm. */
	out_le32(&p->control_regs->start_addr.r,
	    fb_display[con].var.yoffset * fb_display[con].line_length);
	
	return 0;
}

static void controlfb_blank(int blank_mode, struct fb_info *info)
{
/*
 *  Blank the screen if blank_mode != 0, else unblank. If blank == NULL
 *  then the caller blanks by setting the CLUT (Color Look Up Table) to all
 *  black. Return 0 if blanking succeeded, != 0 if un-/blanking failed due
 *  to e.g. a video mode which doesn't support it. Implements VESA suspend
 *  and powerdown modes on hardware that supports disabling hsync/vsync:
 *    blank_mode == 2: suspend vsync
 *    blank_mode == 3: suspend hsync
 *    blank_mode == 4: powerdown
 */
/* A blank_mode of 1+VESA_NO_BLANKING or 1+VESA_POWERDOWN act alike... */
	struct fb_info_control *p = (struct fb_info_control *) info;
	int	ctrl;

	if(blank_mode == 1+VESA_NO_BLANKING)
		blank_mode = 1+VESA_POWERDOWN;
	ctrl = ld_le32(&p->control_regs->ctrl.r) | 0x33;
	if (blank_mode)
		--blank_mode;
	if (blank_mode & VESA_VSYNC_SUSPEND)
		ctrl &= ~3;
	if (blank_mode & VESA_HSYNC_SUSPEND)
		ctrl &= ~0x30;
	out_le32(&p->control_regs->ctrl.r, ctrl);

/* TODO: Figure out how the heck to powerdown this thing! */

    return;
}

static int controlfb_getcolreg(u_int regno, u_int *red, u_int *green,
			     u_int *blue, u_int *transp, struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;

	if (regno > 255)
		return 1;
	*red = (p->palette[regno].red<<8) | p->palette[regno].red;
	*green = (p->palette[regno].green<<8) | p->palette[regno].green;
	*blue = (p->palette[regno].blue<<8) | p->palette[regno].blue;
	*transp = 0;
	return 0;
}

static int controlfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;
	u_int i;
	__u8 r, g, b;

	if (regno > 255)
		return 1;

	r = red >> 8;
	g = green >> 8;
	b = blue >> 8;

	p->palette[regno].red = r;
	p->palette[regno].green = g;
	p->palette[regno].blue = b;

	out_8(&p->cmap_regs->addr, regno);	/* tell clut what addr to fill	*/
	out_8(&p->cmap_regs->lut, r);		/* send one color channel at	*/
	out_8(&p->cmap_regs->lut, g);		/* a time...			*/
	out_8(&p->cmap_regs->lut, b);

	if (regno < 16)
		switch (p->par.cmode) {
#ifdef FBCON_HAS_CFB16
			case CMODE_16:
				p->fbcon_cmap.cfb16[regno] = (regno << 10) | (regno << 5) | regno;
				break;
#endif
#ifdef FBCON_HAS_CFB32
			case CMODE_32:
				i = (regno << 8) | regno;
				p->fbcon_cmap.cfb32[regno] = (i << 16) | i;
				break;
#endif
		}
	return 0;
}

static void do_install_cmap(int con, struct fb_info *info)
{
	if (con != currcon)
		return;
	if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, 1, controlfb_setcolreg,
			    info);
	else {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_set_cmap(fb_default_cmap(size), 1, controlfb_setcolreg,
			    info);
	}
}

static inline int control_vram_reqd(int video_mode, int color_mode)
{
	return (control_reg_init[video_mode-1]->vres
		* control_reg_init[video_mode-1]->hres << color_mode)
	       + control_reg_init[video_mode-1]->offset[color_mode];
}

static void set_control_clock(unsigned char *params)
{
	struct adb_request req;
	int i;

#ifdef CONFIG_ADB_CUDA
	for (i = 0; i < 3; ++i) {
		cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
			     0x50, i + 1, params[i]);
		while (!req.complete)
			cuda_poll();
	}
#endif	
}


static void __init init_control(struct fb_info_control *p)
{
	struct fb_par_control parstruct;
	struct fb_par_control *par = &parstruct;
	struct fb_var_screeninfo var;

	p->sense = read_control_sense(p);
	printk(KERN_INFO "Monitor sense value = 0x%x, ", p->sense);
	/* Try to pick a video mode out of NVRAM if we have one. */
	if (default_vmode == VMODE_NVRAM) {
		par->vmode = nvram_read_byte(NV_VMODE);
		if(par->vmode <= 0 || par->vmode > VMODE_MAX || !control_reg_init[par->vmode - 1])
			par->vmode = VMODE_CHOOSE;
		if(par->vmode == VMODE_CHOOSE)
			par->vmode = mac_map_monitor_sense(p->sense);
		if(!control_reg_init[par->vmode - 1])
			par->vmode = VMODE_640_480_60;
	} else
		par->vmode=default_vmode;

	if (default_cmode == CMODE_NVRAM){
		par->cmode = nvram_read_byte(NV_CMODE);
		if(par->cmode < CMODE_8 || par->cmode > CMODE_32)
			par->cmode = CMODE_8;}
	else
		par->cmode=default_cmode;
	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	while(par->cmode > CMODE_8 && control_vram_reqd(par->vmode, par->cmode) > p->total_vram)
		par->cmode--;
	
	printk("using video mode %d and color mode %d.\n", par->vmode, par->cmode);
	
	par->vxres = par->xres = control_reg_init[par->vmode - 1]->hres;
	par->yres = control_reg_init[par->vmode - 1]->vres;
	par->vyres = p->total_vram / (par->vxres << par->cmode);
	par->xoffset = par->yoffset = 0;
	
	control_init_info(&p->info, p);
	
	par_set = 1;	/* Debug */
	
	control_par_to_var(par, &var);
	var.activate = FB_ACTIVATE_NOW;
	control_set_var(&var, -1, &p->info);
	
	p->info.flags = FBINFO_FLAG_DEFAULT;
	if (register_framebuffer(&p->info) < 0) {
		kfree(p);
		return;
	}
	
	printk(KERN_INFO "fb%d: control display adapter\n", GET_FB_IDX(p->info.node));	
}

#define STORE_D2(a,d) \
	out_8(&p->cmap_regs->addr, (a)); \
	out_8(&p->cmap_regs->d2,   (d))

/* Now how about actually saying, Make it so! */
/* Some things in here probably don't need to be done each time. */
static void control_set_hardware(struct fb_info_control *p, struct fb_par_control *par)
{
	struct control_regvals	*init;
	volatile struct preg	*rp;
	int			flags, ctrl, i;
	int			vmode, cmode;
	
	if(PAR_EQUAL(&p->par, par))
		return;
	
	p->par = *par;
	
	vmode = p->par.vmode;
	cmode = p->par.cmode;
	
	init = control_reg_init[vmode - 1];
	
	if (control_vram_reqd(vmode, cmode) > 0x200000)
		flags = 0x51;
	else if (p->control_use_bank2)
		flags = 0x39;
	else
		flags = 0x31;
	if (vmode >= VMODE_1280_960_75 && cmode >= CMODE_16)
		ctrl = 0x7f;
	else
		ctrl = 0x3b;

	/* Initialize display timing registers */
	out_le32(&p->control_regs->ctrl.r, 0x43b);
	
	set_control_clock(init->clock_params);
	
	STORE_D2(0x20, init->radacal_ctrl[cmode]);
	STORE_D2(0x21, p->control_use_bank2 ? 0 : 1);
	STORE_D2(0x10, 0);
	STORE_D2(0x11, 0);

	rp = &p->control_regs->vswin;
	for (i = 0; i < 16; ++i, ++rp)
		out_le32(&rp->r, init->regs[i]);
	
	out_le32(&p->control_regs->pitch.r, par->vxres << cmode);
	out_le32(&p->control_regs->mode.r, init->mode[cmode]);
	out_le32(&p->control_regs->flags.r, flags);
	out_le32(&p->control_regs->start_addr.r,
	    par->yoffset * (par->vxres << cmode));
	out_le32(&p->control_regs->reg18.r, 0x1e5);
	out_le32(&p->control_regs->reg19.r, 0);

	for (i = 0; i < 16; ++i) {
		controlfb_setcolreg(color_table[i], default_red[i]<<8,
				    default_grn[i]<<8, default_blu[i]<<8,
				    0, (struct fb_info *)p);
	}
/* Does the above need to be here each time? -- danj */

	/* Turn on display */
	out_le32(&p->control_regs->ctrl.r, ctrl);
	
#ifdef CONFIG_FB_COMPAT_XPMAC
	/* And let the world know the truth. */
	if (!console_fb_info || console_fb_info == &p->info) {
		display_info.height = p->par.yres;
		display_info.width = p->par.xres;
		display_info.depth = (cmode == CMODE_32) ? 32 :
			((cmode == CMODE_16) ? 16 : 8);
		display_info.pitch = p->par.vxres << p->par.cmode;
		display_info.mode = vmode;
		strncpy(display_info.name, "control",
			sizeof(display_info.name));
		display_info.fb_address = p->frame_buffer_phys
			 + control_reg_init[vmode-1]->offset[cmode];
		display_info.cmap_adr_address = p->cmap_regs_phys;
		display_info.cmap_data_address = p->cmap_regs_phys + 0x30;
		display_info.disp_reg_address = p->control_regs_phys;
		console_fb_info = &p->info;
	}
#endif /* CONFIG_FB_COMPAT_XPMAC */
}

int __init control_init(void)
{
	struct device_node *dp;

	dp = find_devices("control");
	if (dp != 0)
		control_of_init(dp);
	return 0;
}

static void __init control_of_init(struct device_node *dp)
{
	struct fb_info_control	*p;
	unsigned long		addr, size;
	int			i, bank1, bank2;

	if(dp->n_addrs != 2) {
		printk(KERN_ERR "expecting 2 address for control (got %d)", dp->n_addrs);
		return;
	}
	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (p == 0)
		return;
	memset(p, 0, sizeof(*p));

	/* Map in frame buffer and registers */
	for (i = 0; i < dp->n_addrs; ++i) {
		addr = dp->addrs[i].address;
		size = dp->addrs[i].size;
		/* Let's assume we can request either all or nothing */
		if (!request_mem_region(addr, size, "controlfb")) {
		    kfree(p);
		    return;
		}
		if (size >= 0x800000) {
			/* use the big-endian aperture (??) */
			addr += 0x800000;
			/* map at most 8MB for the frame buffer */
			p->frame_buffer_phys = addr;
			p->frame_buffer = __ioremap(addr, 0x800000, _PAGE_WRITETHRU);
		} else {
			p->control_regs_phys = addr;
			p->control_regs = ioremap(addr, size);
		}
	}
	p->cmap_regs_phys = 0xf301b000;	 /* XXX not in prom? */
	request_mem_region(p->cmap_regs_phys, 0x1000, "controlfb cmap");
	p->cmap_regs = ioremap(p->cmap_regs_phys, 0x1000);

	/* Work out which banks of VRAM we have installed. */
	/* According to Andrew Fyfe <bandr@best.com>, the VRAM behaves like so: */
	/* afyfe: observations from an 8500:
	 * - with 2M vram in bank 1, it appears at offsets 0, 2M and 4M
	 * - with 2M vram in bank 2, it appears only at offset 6M
	 * - with 4M vram, it appears only as a 4M block at offset 0.
	 */

	/* We know there is something at 2M if there is something at 0M. */
	out_8(&p->frame_buffer[0x200000], 0xa5);
	out_8(&p->frame_buffer[0x200001], 0x38);
	asm volatile("eieio; dcbi 0,%0" : : "r" (&p->frame_buffer[0x200000]) : "memory" );

	out_8(&p->frame_buffer[0], 0x5a);
	out_8(&p->frame_buffer[1], 0xc7);
	asm volatile("eieio; dcbi 0,%0" : : "r" (&p->frame_buffer[0]) : "memory" );

	bank1 =  (in_8(&p->frame_buffer[0x000000]) == 0x5a)
		&& (in_8(&p->frame_buffer[0x000001]) == 0xc7);
	bank2 =  (in_8(&p->frame_buffer[0x200000]) == 0xa5)
		&& (in_8(&p->frame_buffer[0x200001]) == 0x38);

	if(bank2 && !bank1)
		printk(KERN_INFO "controlfb: Found memory at 2MB but not at 0!  Please contact dan@debian.org\n");

	if(!bank1) {
		out_8(&p->frame_buffer[0x600000], 0xa5);
		out_8(&p->frame_buffer[0x600001], 0x38);
		asm volatile("eieio; dcbi 0,%0" : : "r" (&p->frame_buffer[0x600000]) : "memory" );
		bank2 = (in_8(&p->frame_buffer[0x600000]) == 0xa5)
			&& (in_8(&p->frame_buffer[0x600001]) == 0x38);
		/* If we don't have bank 1 installed, we hope we have bank 2 :-) */
		p->control_use_bank2 = 1;
		p->frame_buffer += 0x600000;
		p->frame_buffer_phys += 0x600000;
	}
	
	p->total_vram = (bank1 + bank2) * 0x200000;
	
	printk(KERN_INFO "controlfb: Memory bank 1 %s, bank 2 %s, total VRAM %dMB\n",
		bank1 ? "present" : "absent", bank2 ? "present" : "absent",
		2 * (bank1 + bank2));

	init_control(p);
}

/*
 * Get the monitor sense value.
 * Note that this can be called before calibrate_delay,
 * so we can't use udelay.
 */
static int read_control_sense(struct fb_info_control *p)
{
	int sense;

	out_le32(&p->control_regs->mon_sense.r, 7);	/* drive all lines high */
	__delay(200);
	out_le32(&p->control_regs->mon_sense.r, 077);	/* turn off drivers */
	__delay(2000);
	sense = (in_le32(&p->control_regs->mon_sense.r) & 0x1c0) << 2;

	/* drive each sense line low in turn and collect the other 2 */
	out_le32(&p->control_regs->mon_sense.r, 033);	/* drive A low */
	__delay(2000);
	sense |= (in_le32(&p->control_regs->mon_sense.r) & 0xc0) >> 2;
	out_le32(&p->control_regs->mon_sense.r, 055);	/* drive B low */
	__delay(2000);
	sense |= ((in_le32(&p->control_regs->mon_sense.r) & 0x100) >> 5)
		| ((in_le32(&p->control_regs->mon_sense.r) & 0x40) >> 4);
	out_le32(&p->control_regs->mon_sense.r, 066);	/* drive C low */
	__delay(2000);
	sense |= (in_le32(&p->control_regs->mon_sense.r) & 0x180) >> 7;

	out_le32(&p->control_regs->mon_sense.r, 077);	/* turn off drivers */
	
	return sense;
}

/***********************  Various translation functions  ***********************/
#if 1
/* This routine takes a user-supplied var, and picks the best vmode/cmode from it. */
static int control_var_to_par(struct fb_var_screeninfo *var,
	struct fb_par_control *par, const struct fb_info *fb_info)
{
	int xres = var->xres;
	int yres = var->yres;
	int bpp = var->bits_per_pixel;
	struct fb_info_control *p = (struct fb_info_control *) fb_info;

    /*
     *  Get the video params out of 'var'. If a value doesn't fit, round it up,
     *  if it's too big, return -EINVAL.
     *
     *  Suggestion: Round up in the following order: bits_per_pixel, xres,
     *  yres, xres_virtual, yres_virtual, xoffset, yoffset, grayscale,
     *  bitfields, horizontal timing, vertical timing.
     */
	/* swiped by jonh from atyfb.c */
	if (xres <= 640 && yres <= 480)
		par->vmode = VMODE_640_480_67;		/* 640x480, 67Hz */
	else if (xres <= 640 && yres <= 870)
		par->vmode = VMODE_640_870_75P;		/* 640x870, 75Hz (portrait) */
	else if (xres <= 800 && yres <= 600)
		par->vmode = VMODE_800_600_75;		/* 800x600, 75Hz */
	else if (xres <= 832 && yres <= 624)
		par->vmode = VMODE_832_624_75;		/* 832x624, 75Hz */
	else if (xres <= 1024 && yres <= 768)
		par->vmode = VMODE_1024_768_75;		/* 1024x768, 75Hz */
	else if (xres <= 1152 && yres <= 870)
		par->vmode = VMODE_1152_870_75;		/* 1152x870, 75Hz */
	else if (xres <= 1280 && yres <= 960)
		par->vmode = VMODE_1280_960_75;		/* 1280x960, 75Hz */
	else if (xres <= 1280 && yres <= 1024)
		par->vmode = VMODE_1280_1024_75;	/* 1280x1024, 75Hz */
	else {
		printk(KERN_ERR "Bad x/y res in var_to_par\n");
		return -EINVAL;
	}

	xres = control_reg_init[par->vmode-1]->hres;
	yres = control_reg_init[par->vmode-1]->vres;

	par->xres = xres;
	par->yres = yres;

	if (var->xres_virtual <= xres)
		par->vxres = xres;
	else if(var->xres_virtual > xres) {
		par->vxres = xres;
	} else	/* NotReached at present */
		par->vxres = (var->xres_virtual+7) & ~7;

	if (var->yres_virtual <= yres)
		par->vyres = yres;
	else
		par->vyres = var->yres_virtual;

	if (var->xoffset > 0 || var->yoffset+yres > par->vyres) {
		printk(KERN_ERR "Bad offsets in var_to_par\n");
		return -EINVAL;
	}

	par->xoffset = (var->xoffset+7) & ~7;
	par->yoffset = var->yoffset;


 	if (bpp <= 8)
		par->cmode = CMODE_8;
	else if (bpp <= 16)
		par->cmode = CMODE_16;
	else if (bpp <= 32)
		par->cmode = CMODE_32;
	else {
		printk(KERN_ERR "Bad bpp in var_to_par\n");
		return -EINVAL;
	}

	if (control_vram_reqd(par->vmode, par->cmode) > p->total_vram) {
		printk(KERN_ERR "Too much VRAM required for vmode %d cmode %d.\n", par->vmode, par->cmode);
		return -EINVAL;
	}

	/* Check if we know about the wanted video mode */
	if (control_reg_init[par->vmode - 1] == NULL) {
		printk(KERN_ERR "init is null in control_var_to_par().\n");
		/* I'm not sure if control has any specific requirements --	*/
		/* if we have a regvals struct, we're good to go?		*/
		return -EINVAL;
	}

	return 0;
}
#else
/* This routine takes a user-supplied var, and picks the best vmode/cmode from it. */
static int control_var_to_par(struct fb_var_screeninfo *var,
	struct fb_par_control *par, const struct fb_info *fb_info)
{
	struct fb_info_control *p = (struct fb_info_control *) fb_info;
	
	if(mac_var_to_vmode(var, &par->vmode, &par->cmode) != 0)
		return -EINVAL;
	par->xres = par->vxres = vmode_attrs[par->vmode - 1].hres;
	par->yres = par->vyres = vmode_attrs[par->vmode - 1].vres;
	par->xoffset = par->yoffset = 0;
	
	if (control_vram_reqd(par->vmode, par->cmode) > p->total_vram)
		return -EINVAL;

	/* Check if we know about the wanted video mode */
	if(!control_reg_init[par->vmode-1]) {
		/* I'm not sure if control has any specific requirements --	*/
		/* if we have a regvals struct, we're good to go?		*/
		return -EINVAL;
	}
	return 0;
}
#endif

/***********  Convert hardware data in par to an fb_var_screeninfo ***********/

static void control_par_to_var(struct fb_par_control *par, struct fb_var_screeninfo *var)
{
	struct control_regints *rv;
	
	rv = (struct control_regints *) control_reg_init[par->vmode - 1]->regs;
	
	memset(var, 0, sizeof(*var));
	var->xres = control_reg_init[par->vmode - 1]->hres;
	var->yres = control_reg_init[par->vmode - 1]->vres;
	var->xres_virtual = par->vxres;
	var->yres_virtual = par->vyres;
	var->xoffset = par->xoffset;
	var->yoffset = par->yoffset;
	var->grayscale = 0;
	
	if(par->cmode != CMODE_8 && par->cmode != CMODE_16 && par->cmode != CMODE_32) {
		printk(KERN_ERR "Bad color mode in control_par_to_var()!\n");
		par->cmode = CMODE_8;
	}
	switch(par->cmode) {
	case CMODE_8:
		var->bits_per_pixel = 8;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case CMODE_16:	/* RGB 555 */
		var->bits_per_pixel = 16;
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case CMODE_32:	/* RGB 888 */
		var->bits_per_pixel = 32;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
	var->nonstd = 0;
	var->activate = 0;
	var->height = -1;
	var->width = -1;
	var->vmode = FB_VMODE_NONINTERLACED;

	var->left_margin = (rv->heblank - rv->hesync)
		<< ((par->vmode > 18) ? 2 : 1);
	var->right_margin = (rv->hssync - rv->hsblank)
		<< ((par->vmode > 18) ? 2 : 1);
	var->hsync_len = (rv->hperiod + 2 - rv->hssync + rv->hesync)
		<< ((par->vmode > 18) ? 2 : 1);

	var->upper_margin = (rv->veblank - rv->vesync) >> 1;
	var->lower_margin = (rv->vssync - rv->vsblank) >> 1;
	var->vsync_len = (rv->vperiod - rv->vssync + rv->vesync) >> 1;

	/* Acording to macmodes.c... */
	if((par->vmode >= 9 && par->vmode <= 12) ||
	   (par->vmode >= 16 && par->vmode <= 18) ||
	   (par->vmode == 20))
	{
		var->sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT;
	} else {
		var->sync = 0;          /* I suppose */
	}

    /* The reason these are both here: with my revised margin calculations, */
    /* these SHOULD both give the same answer for each mode.  Some day I    */
    /* will sit down and check the rest.  Works perfectly for vmode 13.     */

#if 0
/* jonh's pixclocks...*/
	/* no long long support in the kernel :-( */
	/* this splittig trick will work if xres > 232 */
	var->pixclock = 1000000000/
	(var->left_margin+var->xres+var->right_margin+var->hsync_len);
	var->pixclock *= 1000;
	var->pixclock /= vmode_attrs[par->vmode-1].vfreq*
	 (var->upper_margin+var->yres+var->lower_margin+var->vsync_len);
#else
/* danj's */
	/* 10^12 * clock_params[0] / (3906400 * clock_params[1] * 2^clock_params[2]) */
	/* (10^12 * clock_params[0] / (3906400 * clock_params[1])) >> clock_params[2] */
	/* (255990.17 * clock_params[0] / clock_params[1]) >> clock_params[2] */
	var->pixclock = 255990 * control_reg_init[par->vmode-1]->clock_params[0];
	var->pixclock /= control_reg_init[par->vmode-1]->clock_params[1];
	var->pixclock >>= control_reg_init[par->vmode-1]->clock_params[2];
#endif
}

static void control_par_to_fix(struct fb_par_control *par, struct fb_fix_screeninfo *fix,
	struct fb_info_control *p)
{
	memset(fix, 0, sizeof(*fix));
	strcpy(fix->id, "control");
	fix->mmio_start = p->control_regs_phys;
	fix->mmio_len = sizeof(struct control_regs);
	fix->type = FB_TYPE_PACKED_PIXELS;
	
	fix->ypanstep = 1;
	/*
		fix->type_aux = 0;
		fix->ywrapstep = 0;
		fix->ypanstep = 0;
		fix->xpanstep = 0;
	*/

	fix->smem_start = (p->frame_buffer_phys
		+ control_reg_init[par->vmode-1]->offset[par->cmode]);
	fix->smem_len = p->total_vram - control_reg_init[par->vmode-1]->offset[par->cmode];
	fix->visual = (par->cmode == CMODE_8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
	fix->line_length = par->vxres << par->cmode;
}

/* We never initialize any display except for p->disp.
   And p->disp is already memset to 0.  So no memset here.
   [Found by Takashi Oe]
*/
static void control_par_to_display(struct fb_par_control *par,
  struct display *disp, struct fb_fix_screeninfo *fix, struct fb_info_control *p)
{
	/* memset(disp, 0, sizeof(*disp)); */
	disp->type = fix->type;
	disp->can_soft_blank = 1;
	disp->scrollmode = SCROLL_YNOMOVE | SCROLL_YNOPARTIAL;
	disp->ypanstep = fix->ypanstep;
	disp->ywrapstep = fix->ywrapstep;
#if 0
		disp->type_aux = fix->type_aux;
		disp->cmap.red = NULL;	/* ??? danj */
		disp->cmap.green = NULL;
		disp->cmap.blue = NULL;
		disp->cmap.transp = NULL;
			/* Yeah, I realize I just set 0 = 0. */
#endif

	control_par_to_var(par, &disp->var);
	disp->screen_base = (char *) p->frame_buffer
		 + control_reg_init[par->vmode-1]->offset[par->cmode];
	disp->visual = fix->visual;
	disp->line_length = fix->line_length;
	control_set_dispsw(disp, par->cmode, p);
}

static void control_cfb16_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p)*2;
    for (rows = fontheight(p); rows--; dest += bytes) {
       switch (fontwidth(p)) {
       case 16:
           ((u32 *)dest)[6] ^= 0x3def3def; ((u32 *)dest)[7] ^= 0x3def3def;
           /* FALL THROUGH */
       case 12:
           ((u32 *)dest)[4] ^= 0x3def3def; ((u32 *)dest)[5] ^= 0x3def3def;
           /* FALL THROUGH */
       case 8:
           ((u32 *)dest)[2] ^= 0x3def3def; ((u32 *)dest)[3] ^= 0x3def3def;
           /* FALL THROUGH */
       case 4:
           ((u32 *)dest)[0] ^= 0x3def3def; ((u32 *)dest)[1] ^= 0x3def3def;
       }
    }
}

static void control_cfb32_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p) * 4;
    for (rows = fontheight(p); rows--; dest += bytes) {
       switch (fontwidth(p)) {
       case 16:
           ((u32 *)dest)[12] ^= 0x0f0f0f0f; ((u32 *)dest)[13] ^= 0x0f0f0f0f;
           ((u32 *)dest)[14] ^= 0x0f0f0f0f; ((u32 *)dest)[15] ^= 0x0f0f0f0f;
           /* FALL THROUGH */
       case 12:
           ((u32 *)dest)[8] ^= 0x0f0f0f0f; ((u32 *)dest)[9] ^= 0x0f0f0f0f;
           ((u32 *)dest)[10] ^= 0x0f0f0f0f; ((u32 *)dest)[11] ^= 0x0f0f0f0f;
           /* FALL THROUGH */
       case 8:
           ((u32 *)dest)[4] ^= 0x0f0f0f0f; ((u32 *)dest)[5] ^= 0x0f0f0f0f;
           ((u32 *)dest)[6] ^= 0x0f0f0f0f; ((u32 *)dest)[7] ^= 0x0f0f0f0f;
           /* FALL THROUGH */
       case 4:
           ((u32 *)dest)[0] ^= 0x0f0f0f0f; ((u32 *)dest)[1] ^= 0x0f0f0f0f;
           ((u32 *)dest)[2] ^= 0x0f0f0f0f; ((u32 *)dest)[3] ^= 0x0f0f0f0f;
           /* FALL THROUGH */
       }
    }
}

static struct display_switch control_cfb16 = {
    setup:		fbcon_cfb16_setup,
    bmove:		fbcon_cfb16_bmove,
    clear:		fbcon_cfb16_clear,
    putc:		fbcon_cfb16_putc,
    putcs:		fbcon_cfb16_putcs,
    revc:		control_cfb16_revc,
    clear_margins:	fbcon_cfb16_clear_margins,
    fontwidthmask:	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};

static struct display_switch control_cfb32 = {
    setup:		fbcon_cfb32_setup,
    bmove:		fbcon_cfb32_bmove,
    clear:		fbcon_cfb32_clear,
    putc:		fbcon_cfb32_putc,
    putcs:		fbcon_cfb32_putcs,
    revc:		control_cfb32_revc,
    clear_margins:	fbcon_cfb32_clear_margins,
    fontwidthmask:	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};


static void control_set_dispsw(struct display *disp, int cmode, struct fb_info_control *p)
{
	switch (cmode) {
#ifdef FBCON_HAS_CFB8
		case CMODE_8:
			disp->dispsw = &fbcon_cfb8;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case CMODE_16:
			disp->dispsw = &control_cfb16;
			disp->dispsw_data = p->fbcon_cmap.cfb16;
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case CMODE_32:
			disp->dispsw = &control_cfb32;
			disp->dispsw_data = p->fbcon_cmap.cfb32;
			break;
#endif
		default:
			disp->dispsw = &fbcon_dummy;
			break;
	}
}

static void __init control_init_info(struct fb_info *info, struct fb_info_control *p)
{
	strcpy(info->modename, "control");
	info->node = -1;	/* ??? danj */
	info->fbops = &controlfb_ops;
	info->disp = &p->display;
	strcpy(info->fontname, fontname);
	info->changevar = NULL;
	info->switch_con = &controlfb_switch;
	info->updatevar = &controlfb_updatevar;
	info->blank = &controlfb_blank;
}

/* Parse user speficied options (`video=controlfb:') */
void __init control_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return;

	for (this_opt = strtok(options, ","); this_opt;
	     this_opt = strtok(NULL, ",")) {
		if (!strncmp(this_opt, "font:", 5)) {
			char *p;
			int i;

			p = this_opt +5;
			for (i = 0; i < sizeof(fontname) - 1; i++)
				if (!*p || *p == ' ' || *p == ',')
					break;
			memcpy(fontname, this_opt + 5, i);
			fontname[i] = 0;
		}
		if (!strncmp(this_opt, "vmode:", 6)) {
			int vmode = simple_strtoul(this_opt+6, NULL, 0);
		if (vmode > 0 && vmode <= VMODE_MAX)
			default_vmode = vmode;
		} else if (!strncmp(this_opt, "cmode:", 6)) {
			int depth = simple_strtoul(this_opt+6, NULL, 0);
			switch (depth) {
			 case CMODE_8:
			 case CMODE_16:
			 case CMODE_32:
			 	default_cmode = depth;
			 	break;
			 case 8:
				default_cmode = CMODE_8;
				break;
			 case 15:
			 case 16:
				default_cmode = CMODE_16;
				break;
			 case 24:
			 case 32:
				default_cmode = CMODE_32;
				break;
			}
		}
	}
}

#if 0
static int controlfb_pan_display(struct fb_var_screeninfo *var,
			   struct controlfb_par *par,
			   const struct fb_info *fb_info)
{
    /*
     *  Pan (or wrap, depending on the `vmode' field) the display using the
     *  `xoffset' and `yoffset' fields of the `var' structure.
     *  If the values don't fit, return -EINVAL.
     */

	FUNCID;

    return 0;
}

#endif
