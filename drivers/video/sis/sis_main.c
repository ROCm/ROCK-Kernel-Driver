/*
 * SiS 300/630/730/540/315/550/650/740 frame buffer device
 * for Linux kernels 2.4.x and 2.5.x
 *
 * Partly based on the VBE 2.0 compliant graphic boards framebuffer driver,
 * which is (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * Authors:   	SiS (www.sis.com.tw)
 *		(Various others)
 *		Thomas Winischhofer <thomas@winischhofer.net>:
 *			- SiS Xabre (330) support
 *			- many fixes and enhancements for all chipset series,
 *			- extended bridge handling, TV output for Chrontel 7005
 *                      - 650/LVDS support (for LCD panels up to 1600x1200)
 *                      - 650/740/Chrontel 7019 support
 *                      - 30xB/30xLV LCD, TV and VGA2 support
 *			- memory queue handling enhancements,
 *                      - 2D acceleration and y-panning,
 *                      - portation to 2.5 API
 *			- etc.
 *			(see http://www.winischhofer.net/
 *			for more information and updates)
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vt_kern.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/agp_backend.h>
#include <linux/types.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#include <linux/spinlock.h>
#endif

#include "osdef.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#include <video/sisfb.h>
#else
#include <linux/sisfb.h>
#endif

#include <asm/io.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>
#endif

#include "vgatypes.h"
#include "sis_main.h"
#include "sis.h"

#if 0
#ifdef LINUXBIOS
#include "bios.h"
#endif
#endif

/* -------------------- Macro definitions ---------------------------- */

#undef SISFBDEBUG 	/* TW: no debugging */

#ifdef SISFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#ifdef SISFBACCEL
#ifdef FBCON_HAS_CFB8
extern struct display_switch fbcon_sis8;
#endif
#ifdef FBCON_HAS_CFB16
extern struct display_switch fbcon_sis16;
#endif
#ifdef FBCON_HAS_CFB32
extern struct display_switch fbcon_sis32;
#endif
#endif
#endif

/* --------------- Hardware Access Routines -------------------------- */

void sisfb_set_reg4(u16 port, unsigned long data)
{
	outl((u32) (data & 0xffffffff), port);
}

u32 sisfb_get_reg3(u16 port)
{
	u32 data;

	data = inl(port);
	return (data);
}

/* ------------ Interface for init & mode switching code ------------- */

BOOLEAN
sisfb_query_VGA_config_space(PSIS_HW_DEVICE_INFO psishw_ext,
	unsigned long offset, unsigned long set, unsigned long *value)
{
	static struct pci_dev *pdev = NULL;
	static unsigned char init = 0, valid_pdev = 0;

	if (!set)
		DPRINTK("sisfb: Get VGA offset 0x%lx\n", offset);
	else
		DPRINTK("sisfb: Set offset 0x%lx to 0x%lx\n", offset, *value);

	if (!init) {
		init = TRUE;
		pdev = pci_find_device(PCI_VENDOR_ID_SI, ivideo.chip_id, pdev);
		if (pdev)
			valid_pdev = TRUE;
	}

	if (!valid_pdev) {
		printk(KERN_DEBUG "sisfb: Can't find SiS %d VGA device.\n",
				ivideo.chip_id);
		return FALSE;
	}

	if (set == 0)
		pci_read_config_dword(pdev, offset, (u32 *)value);
	else
		pci_write_config_dword(pdev, offset, (u32)(*value));

	return TRUE;
}

BOOLEAN sisfb_query_north_bridge_space(PSIS_HW_DEVICE_INFO psishw_ext,
	unsigned long offset, unsigned long set, unsigned long *value)
{
	static struct pci_dev *pdev = NULL;
	static unsigned char init = 0, valid_pdev = 0;
	u16 nbridge_id = 0;

	if (!init) {
		init = TRUE;
		switch (ivideo.chip) {
		case SIS_540:
			nbridge_id = PCI_DEVICE_ID_SI_540;
			break;
		case SIS_630:
			nbridge_id = PCI_DEVICE_ID_SI_630;
			break;
		case SIS_730:
			nbridge_id = PCI_DEVICE_ID_SI_730;
			break;
		case SIS_550:
			nbridge_id = PCI_DEVICE_ID_SI_550;
			break;
		case SIS_650:
			nbridge_id = PCI_DEVICE_ID_SI_650;
			break;
		case SIS_740:			
			nbridge_id = PCI_DEVICE_ID_SI_740;
			break;
		default:
			nbridge_id = 0;
			break;
		}

		pdev = pci_find_device(PCI_VENDOR_ID_SI, nbridge_id, pdev);
		if (pdev)
			valid_pdev = TRUE;
	}

	if (!valid_pdev) {
		printk(KERN_DEBUG "sisfb: Can't find SiS %d North Bridge device.\n",
				nbridge_id);
		return FALSE;
	}

	if (set == 0)
		pci_read_config_dword(pdev, offset, (u32 *)value);
	else
		pci_write_config_dword(pdev, offset, (u32)(*value));

	return TRUE;
}

/* ------------------ Internal helper routines ----------------- */

static void sisfb_search_mode(const char *name)
{
	int i = 0, j = 0;

	if(name == NULL) {
	   printk(KERN_ERR "sisfb: Internal error, using default mode.\n");
	   sisfb_mode_idx = DEFAULT_MODE;
	   return;
	}
		
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)		
        if (!strcmp(name, sisbios_mode[MODE_INDEX_NONE].name)) {
	   printk(KERN_ERR "sisfb: Mode 'none' not supported anymore. Using default.\n");
	   sisfb_mode_idx = DEFAULT_MODE;
	   return;
	}
#endif		

	while(sisbios_mode[i].mode_no != 0) {
		if (!strcmp(name, sisbios_mode[i].name)) {
			sisfb_mode_idx = i;
			j = 1;
			break;
		}
		i++;
	}
	if(!j) printk(KERN_INFO "sisfb: Invalid mode '%s'\n", name);
}

static void sisfb_search_vesamode(unsigned int vesamode)
{
	int i = 0, j = 0;

	if(vesamode == 0) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	
		sisfb_mode_idx = MODE_INDEX_NONE;
#else
		printk(KERN_ERR "sisfb: Mode 'none' not supported anymore. Using default.\n");
		sisfb_mode_idx = DEFAULT_MODE;
#endif		
		return;
	}

	vesamode &= 0x1dff;  /* Clean VESA mode number from other flags */

	while(sisbios_mode[i].mode_no != 0) {
		if( (sisbios_mode[i].vesa_mode_no_1 == vesamode) ||
		    (sisbios_mode[i].vesa_mode_no_2 == vesamode) ) {
			sisfb_mode_idx = i;
			j = 1;
			break;
		}
		i++;
	}
	if(!j) printk(KERN_INFO "sisfb: Invalid VESA mode 0x%x'\n", vesamode);
}

static int sisfb_validate_mode(int myindex)
{
   u16 xres, yres;

#ifdef CONFIG_FB_SIS_300
   if(sisvga_engine == SIS_300_VGA) {
       if(!(sisbios_mode[myindex].chipset & MD_SIS300)) {
           return(-1);
       }
   }
#endif
#ifdef CONFIG_FB_SIS_315
   if(sisvga_engine == SIS_315_VGA) {
       if(!(sisbios_mode[myindex].chipset & MD_SIS315)) {
	   return(-1);
       }
   }
#endif

   switch (ivideo.disp_state & DISPTYPE_DISP2) {
     case DISPTYPE_LCD:
	switch (sishw_ext.ulCRT2LCDType) {
	case LCD_640x480:
		xres =  640; yres =  480;  break;
	case LCD_800x600:
		xres =  800; yres =  600;  break;
        case LCD_1024x600:
		xres = 1024; yres =  600;  break;		
	case LCD_1024x768:
	 	xres = 1024; yres =  768;  break;
	case LCD_1152x768:
		xres = 1152; yres =  768;  break;		
	case LCD_1280x960:
	        xres = 1280; yres =  960;  break;		
	case LCD_1280x768:
		xres = 1280; yres =  768;  break;
	case LCD_1280x1024:
		xres = 1280; yres = 1024;  break;
	case LCD_1400x1050:
		xres = 1400; yres = 1050;  break;		
	case LCD_1600x1200:
		xres = 1600; yres = 1200;  break;
	case LCD_320x480:				/* TW: FSTN */
		xres =  320; yres =  480;  break;
	default:
	        xres =    0; yres =    0;  break;
	}
	if(sisbios_mode[myindex].xres > xres) {
	        return(-1);
	}
        if(sisbios_mode[myindex].yres > yres) {
	        return(-1);
	}
	if((sishw_ext.usExternalChip == 0x01) ||   /* LVDS */
           (sishw_ext.usExternalChip == 0x05) ||   /* LVDS+Chrontel */
	   (sishw_ext.Is301BDH)) {		   /* 301B-DH */
	   switch (sisbios_mode[myindex].xres) {
	   	case 512:
	       		if(sisbios_mode[myindex].yres != 512) return -1;
			if(sishw_ext.ulCRT2LCDType == LCD_1024x600) return -1;
	       		break;
	   	case 640:
		       	if((sisbios_mode[myindex].yres != 400) &&
	           	   (sisbios_mode[myindex].yres != 480))
		          	return -1;
	       		break;
	   	case 800:
		       	if(sisbios_mode[myindex].yres != 600) return -1;
	       		break;
	   	case 1024:
		       	if((sisbios_mode[myindex].yres != 600) &&
	           	   (sisbios_mode[myindex].yres != 768))
		          	return -1;
			if((sisbios_mode[myindex].yres == 600) &&
			   (sishw_ext.ulCRT2LCDType != LCD_1024x600))
			   	return -1;
			break;
		case 1152:
			if((sisbios_mode[myindex].yres) != 768) return -1;
			if(sishw_ext.ulCRT2LCDType != LCD_1152x768) return -1;
			break;
	   	case 1280:
		   	if((sisbios_mode[myindex].yres != 768) &&
	           	   (sisbios_mode[myindex].yres != 1024))
		          	return -1;
			if((sisbios_mode[myindex].yres == 768) &&
			   (sishw_ext.ulCRT2LCDType != LCD_1280x768))
			   	return -1;				
			break;
	   	case 1400:
		   	if(sisbios_mode[myindex].yres != 1050) return -1;
			break;
	   	case 1600:
		   	if(sisbios_mode[myindex].yres != 1200) return -1;
			break;
	   	default:
		        return -1;		
	   }
	} else {
	   switch (sisbios_mode[myindex].xres) {
	   	case 512:
	       		if(sisbios_mode[myindex].yres != 512) return -1;
	       		break;
	   	case 640:
		       	if((sisbios_mode[myindex].yres != 400) &&
	           	   (sisbios_mode[myindex].yres != 480))
		          	return -1;
	       		break;
	   	case 800:
		       	if(sisbios_mode[myindex].yres != 600) return -1;
	       		break;
	   	case 1024:
		       	if(sisbios_mode[myindex].yres != 768) return -1;
			break;
	   	case 1280:
		   	if((sisbios_mode[myindex].yres != 960) &&
	           	   (sisbios_mode[myindex].yres != 1024))
		          	return -1;
			if(sisbios_mode[myindex].yres == 960) {
			    if(sishw_ext.ulCRT2LCDType == LCD_1400x1050) 
			   	return -1;
			}
			break;
	   	case 1400:
		   	if(sisbios_mode[myindex].yres != 1050) return -1;
			break;
	   	case 1600:
		   	if(sisbios_mode[myindex].yres != 1200) return -1;
			break;
	   	default:
		        return -1;		
	   }
	}
	break;
     case DISPTYPE_TV:
	switch (sisbios_mode[myindex].xres) {
	case 512:
	case 640:
	case 800:
		break;
	case 720:
		if (ivideo.TV_type == TVMODE_NTSC) {
			if (sisbios_mode[myindex].yres != 480) {
				return(-1);
			}
		} else if (ivideo.TV_type == TVMODE_PAL) {
			if (sisbios_mode[myindex].yres != 576) {
				return(-1);
			}
		}
		/* TW: LVDS/CHRONTEL does not support 720 */
		if (ivideo.hasVB == HASVB_LVDS_CHRONTEL ||
					ivideo.hasVB == HASVB_CHRONTEL) {
				return(-1);
		}
		break;
	case 1024:
		if (ivideo.TV_type == TVMODE_NTSC) {
			if(sisbios_mode[myindex].bpp == 32) {
			       return(-1);
			}
		}
		/* TW: LVDS/CHRONTEL only supports < 800 (1024 on 650/Ch7019)*/
		if (ivideo.hasVB == HASVB_LVDS_CHRONTEL ||
					ivideo.hasVB == HASVB_CHRONTEL) {
		    if(ivideo.chip < SIS_315H) {
				return(-1);
		    }
		}
		break;
	default:
		return(-1);
	}
	break;
     case DISPTYPE_CRT2:	
        if(sisbios_mode[myindex].xres > 1280) return -1;
	break;	
     }
     return(myindex);
}

static void sisfb_search_crt2type(const char *name)
{
	int i = 0;

	if(name == NULL)
		return;

	while(sis_crt2type[i].type_no != -1) {
		if (!strcmp(name, sis_crt2type[i].name)) {
			sisfb_crt2type = sis_crt2type[i].type_no;
			sisfb_tvplug = sis_crt2type[i].tvplug_no;
			break;
		}
		i++;
	}
	if(sisfb_crt2type < 0)
		printk(KERN_INFO "sisfb: Invalid CRT2 type: %s\n", name);
}

static void sisfb_search_queuemode(const char *name)
{
	int i = 0;

	if(name == NULL)
		return;

	while (sis_queuemode[i].type_no != -1) {
		if (!strcmp(name, sis_queuemode[i].name)) {
			sisfb_queuemode = sis_queuemode[i].type_no;
			break;
		}
		i++;
	}
	if (sisfb_queuemode < 0)
		printk(KERN_INFO "sisfb: Invalid queuemode type: %s\n", name);
}

static u8 sisfb_search_refresh_rate(unsigned int rate)
{
	u16 xres, yres;
	int i = 0;

	xres = sisbios_mode[sisfb_mode_idx].xres;
	yres = sisbios_mode[sisfb_mode_idx].yres;

	sisfb_rate_idx = 0;
	while ((sisfb_vrate[i].idx != 0) && (sisfb_vrate[i].xres <= xres)) {
		if ((sisfb_vrate[i].xres == xres) && (sisfb_vrate[i].yres == yres)) {
			if (sisfb_vrate[i].refresh == rate) {
				sisfb_rate_idx = sisfb_vrate[i].idx;
				break;
			} else if (sisfb_vrate[i].refresh > rate) {
				if ((sisfb_vrate[i].refresh - rate) <= 3) {
					DPRINTK("sisfb: Adjusting rate from %d up to %d\n",
						rate, sisfb_vrate[i].refresh);
					sisfb_rate_idx = sisfb_vrate[i].idx;
					ivideo.refresh_rate = sisfb_vrate[i].refresh;
				} else if (((rate - sisfb_vrate[i-1].refresh) <= 2)
						&& (sisfb_vrate[i].idx != 1)) {
					DPRINTK("sisfb: Adjusting rate from %d down to %d\n",
						rate, sisfb_vrate[i-1].refresh);
					sisfb_rate_idx = sisfb_vrate[i-1].idx;
					ivideo.refresh_rate = sisfb_vrate[i-1].refresh;
				} 
				break;
			} else if((rate - sisfb_vrate[i].refresh) <= 2) {
				DPRINTK("sisfb: Adjusting rate from %d down to %d\n",
						rate, sisfb_vrate[i].refresh);
	           		sisfb_rate_idx = sisfb_vrate[i].idx;
		   		break;
	       		}
		}
		i++;
	}
	if (sisfb_rate_idx > 0) {
		return sisfb_rate_idx;
	} else {
		printk(KERN_INFO
			"sisfb: Unsupported rate %d for %dx%d\n", rate, xres, yres);
		return 0;
	}
}

static void sisfb_search_tvstd(const char *name)
{
	int i = 0;

	if(name == NULL)
		return;

	while (sis_tvtype[i].type_no != -1) {
		if (!strcmp(name, sis_tvtype[i].name)) {
			sisfb_tvmode = sis_tvtype[i].type_no;
			break;
		}
		i++;
	}
}

static BOOLEAN sisfb_bridgeisslave(void)
{
   unsigned char usScratchP1_00;

   if(ivideo.hasVB == HASVB_NONE) return FALSE;

   inSISIDXREG(SISPART1,0x00,usScratchP1_00);
   if( ((sisvga_engine == SIS_300_VGA) && (usScratchP1_00 & 0xa0) == 0x20) ||
       ((sisvga_engine == SIS_315_VGA) && (usScratchP1_00 & 0x50) == 0x10) ) {
	   return TRUE;
   } else {
           return FALSE;
   }
}

static BOOLEAN sisfbcheckvretracecrt1(void)
{
   unsigned char temp;

   inSISIDXREG(SISCR,0x17,temp);
   if(!(temp & 0x80)) return FALSE;
   
   if(sisvga_engine == SIS_315_VGA) {
      inSISIDXREG(SISSR,0x1f,temp);
      if(temp & 0xc0) return FALSE;
   }

   if(inSISREG(SISINPSTAT) & 0x08) return TRUE;
   else 			   return FALSE;
}

static BOOLEAN sisfbcheckvretracecrt2(void)
{
   unsigned char temp, reg;

   switch(sisvga_engine) {
   case SIS_300_VGA:
   	reg = 0x25;
	break;
   case SIS_315_VGA:
   	reg = 0x30;
	break;
   default:
        return FALSE;
   }

   inSISIDXREG(SISPART1, reg, temp);
   if(temp & 0x02) return FALSE;
   else 	   return TRUE;
}

static BOOLEAN sisfb_CheckVBRetrace(void) 
{
   if(ivideo.disp_state & DISPTYPE_DISP2) {
      if(sisfb_bridgeisslave()) {
         return(sisfbcheckvretracecrt1());
      } else {
         return(sisfbcheckvretracecrt2());
      }
   } 
   return(sisfbcheckvretracecrt1());
}

/* ----------- FBDev related routines for all series ----------- */

static int sisfb_do_set_var(struct fb_var_screeninfo *var, int isactive,
		      struct fb_info *info)
{
	unsigned int htotal =
		var->left_margin + var->xres + var->right_margin +
		var->hsync_len;
	unsigned int vtotal = 0; 
	double drate = 0, hrate = 0;
	int found_mode = 0;
	int old_mode;
	unsigned char reg;

	TWDEBUG("Inside do_set_var");
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	
	inSISIDXREG(SISCR,0x34,reg);
	if(reg & 0x80) {
	   printk(KERN_INFO "sisfb: Cannot change display mode, X server is active\n");
	   return -EBUSY;
	}
#endif	

	if((var->vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED) {
		vtotal = var->upper_margin + var->yres + var->lower_margin +
		         var->vsync_len;
		vtotal <<= 1;
	} else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		vtotal = var->upper_margin + var->yres + var->lower_margin +
		         var->vsync_len;
		vtotal <<= 2;
	} else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		vtotal = var->upper_margin + (var->yres/2) + var->lower_margin +
		         var->vsync_len; 
	} else 	vtotal = var->upper_margin + var->yres + var->lower_margin +
		         var->vsync_len;

	if(!(htotal) || !(vtotal)) {
		DPRINTK("sisfb: Invalid 'var' information\n");
		return -EINVAL;
	}

	if(var->pixclock && htotal && vtotal) {
	   drate = 1E12 / var->pixclock;
	   hrate = drate / htotal;
	   ivideo.refresh_rate = (unsigned int) (hrate / vtotal * 2 + 0.5);
	} else ivideo.refresh_rate = 60;

	/* TW: Calculation wrong for 1024x600 - force it to 60Hz */
	if((var->xres == 1024) && (var->yres == 600)) ivideo.refresh_rate = 60;

	printk(KERN_DEBUG "sisfb: Change mode to %dx%dx%d-%dHz\n",
		var->xres,var->yres,var->bits_per_pixel,ivideo.refresh_rate);

	old_mode = sisfb_mode_idx;
	sisfb_mode_idx = 0;

	while( (sisbios_mode[sisfb_mode_idx].mode_no != 0) &&
	       (sisbios_mode[sisfb_mode_idx].xres <= var->xres) ) {
		if( (sisbios_mode[sisfb_mode_idx].xres == var->xres) &&
		    (sisbios_mode[sisfb_mode_idx].yres == var->yres) &&
		    (sisbios_mode[sisfb_mode_idx].bpp == var->bits_per_pixel)) {
			sisfb_mode_no = sisbios_mode[sisfb_mode_idx].mode_no;
			found_mode = 1;
			break;
		}
		sisfb_mode_idx++;
	}

	if(found_mode)
		sisfb_mode_idx = sisfb_validate_mode(sisfb_mode_idx);
	else
		sisfb_mode_idx = -1;

       	if(sisfb_mode_idx < 0) {
		printk(KERN_ERR "sisfb: Mode %dx%dx%d not supported\n", var->xres,
		       var->yres, var->bits_per_pixel);
		sisfb_mode_idx = old_mode;
		return -EINVAL;
	}

	if(sisfb_search_refresh_rate(ivideo.refresh_rate) == 0) {
		sisfb_rate_idx = sisbios_mode[sisfb_mode_idx].rate_idx;
		ivideo.refresh_rate = 60;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	if(((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) && isactive) {
#else
	if(isactive) {
#endif
		sisfb_pre_setmode();

		if(SiSSetMode(&SiS_Pr, &sishw_ext, sisfb_mode_no) == 0) {
			printk(KERN_ERR "sisfb: Setting mode[0x%x] failed\n", sisfb_mode_no);
			return -EINVAL;
		}

		outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

		sisfb_post_setmode();

		DPRINTK("sisfb: Set new mode: %dx%dx%d-%d \n",
			sisbios_mode[sisfb_mode_idx].xres,
			sisbios_mode[sisfb_mode_idx].yres,
			sisbios_mode[sisfb_mode_idx].bpp,
			ivideo.refresh_rate);

		ivideo.video_bpp = sisbios_mode[sisfb_mode_idx].bpp;
		ivideo.video_vwidth = ivideo.video_width = sisbios_mode[sisfb_mode_idx].xres;
		ivideo.video_vheight = ivideo.video_height = sisbios_mode[sisfb_mode_idx].yres;
		ivideo.org_x = ivideo.org_y = 0;
		ivideo.video_linelength = ivideo.video_width * (ivideo.video_bpp >> 3);
		ivideo.accel = 0;
		if(sisfb_accel) {
		   ivideo.accel = (var->accel_flags & FB_ACCELF_TEXT) ? -1 : 0;
		}
		switch(ivideo.video_bpp) {
        	case 8:
            		ivideo.DstColor = 0x0000;
	    		ivideo.SiS310_AccelDepth = 0x00000000;
			ivideo.video_cmap_len = 256;
            		break;
        	case 16:
            		ivideo.DstColor = 0x8000;
            		ivideo.SiS310_AccelDepth = 0x00010000;
			ivideo.video_cmap_len = 16;
            		break;
        	case 32:
            		ivideo.DstColor = 0xC000;
	    		ivideo.SiS310_AccelDepth = 0x00020000;
			ivideo.video_cmap_len = 16;
            		break;
		default:
			ivideo.video_cmap_len = 16;
		        printk(KERN_ERR "sisfb: Unsupported depth %d", ivideo.video_bpp);
			ivideo.accel = 0;
			break;
    		}

	}
	TWDEBUG("End of do_set_var");
	return 0;
}

#ifdef SISFB_PAN
static int sisfb_pan_var(struct fb_var_screeninfo *var)
{
	unsigned int base;

	TWDEBUG("Inside pan_var");
	
	if (var->xoffset > (var->xres_virtual - var->xres)) {
	        printk(KERN_INFO "Pan: xo: %d xv %d xr %d\n",
			var->xoffset, var->xres_virtual, var->xres);
		return -EINVAL;
	}
	if(var->yoffset > (var->yres_virtual - var->yres)) {
		printk(KERN_INFO "Pan: yo: %d yv %d yr %d\n",
			var->yoffset, var->yres_virtual, var->yres);
		return -EINVAL;
	}

        base = var->yoffset * var->xres_virtual + var->xoffset;

        /* calculate base bpp dep. */
        switch(var->bits_per_pixel) {
        case 16:
        	base >>= 1;
        	break;
	case 32:
            	break;
	case 8:
        default:
        	base >>= 2;
            	break;
        }
	
	outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

        outSISIDXREG(SISCR, 0x0D, base & 0xFF);
	outSISIDXREG(SISCR, 0x0C, (base >> 8) & 0xFF);
	outSISIDXREG(SISSR, 0x0D, (base >> 16) & 0xFF);
	if(sisvga_engine == SIS_315_VGA) {
		setSISIDXREG(SISSR, 0x37, 0xFE, (base >> 24) & 0x01);
	}
        if(ivideo.disp_state & DISPTYPE_DISP2) {
		orSISIDXREG(SISPART1, sisfb_CRT2_write_enable, 0x01);
        	outSISIDXREG(SISPART1, 0x06, (base & 0xFF));
        	outSISIDXREG(SISPART1, 0x05, ((base >> 8) & 0xFF));
        	outSISIDXREG(SISPART1, 0x04, ((base >> 16) & 0xFF));
		if(sisvga_engine == SIS_315_VGA) {
			setSISIDXREG(SISPART1, 0x02, 0x7F, ((base >> 24) & 0x01) << 7);
		}
        }
	TWDEBUG("End of pan_var");
	return 0;
}
#endif

static void sisfb_bpp_to_var(struct fb_var_screeninfo *var)
{
	switch(var->bits_per_pixel) {
	   case 8:
	   	var->red.offset = var->green.offset = var->blue.offset = 0;
		var->red.length = var->green.length = var->blue.length = 6;
		ivideo.video_cmap_len = 256;
		break;
	   case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		ivideo.video_cmap_len = 16;
		break;
	   case 32:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		ivideo.video_cmap_len = 16;
		break;
	}
}

void sis_dispinfo(struct ap_data *rec)
{
	rec->minfo.bpp    = ivideo.video_bpp;
	rec->minfo.xres   = ivideo.video_width;
	rec->minfo.yres   = ivideo.video_height;
	rec->minfo.v_xres = ivideo.video_vwidth;
	rec->minfo.v_yres = ivideo.video_vheight;
	rec->minfo.org_x  = ivideo.org_x;
	rec->minfo.org_y  = ivideo.org_y;
	rec->minfo.vrate  = ivideo.refresh_rate;
	rec->iobase       = ivideo.vga_base - 0x30;
	rec->mem_size     = ivideo.video_size;
	rec->disp_state   = ivideo.disp_state; 
	rec->version      = (VER_MAJOR << 24) | (VER_MINOR << 16) | VER_LEVEL; 
	rec->hasVB        = ivideo.hasVB; 
	rec->TV_type      = ivideo.TV_type; 
	rec->TV_plug      = ivideo.TV_plug; 
	rec->chip         = ivideo.chip;
}

/* ------------ FBDev related routines for 2.4 series ----------- */

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)

static void sisfb_crtc_to_var(struct fb_var_screeninfo *var)
{
	u16 VRE, VBE, VRS, VBS, VDE, VT;
	u16 HRE, HBE, HRS, HBS, HDE, HT;
	u8  sr_data, cr_data, cr_data2, cr_data3, mr_data;
	int A, B, C, D, E, F, temp;
	double hrate, drate;

	TWDEBUG("Inside crtc_to_var");
	inSISIDXREG(SISSR, IND_SIS_COLOR_MODE, sr_data);

	if (sr_data & SIS_INTERLACED_MODE)
		var->vmode = FB_VMODE_INTERLACED;
	else
		var->vmode = FB_VMODE_NONINTERLACED;

	switch ((sr_data & 0x1C) >> 2) {
	   case SIS_8BPP_COLOR_MODE:
		var->bits_per_pixel = 8;
		break;
	   case SIS_16BPP_COLOR_MODE:
		var->bits_per_pixel = 16;
		break;
	   case SIS_32BPP_COLOR_MODE:
		var->bits_per_pixel = 32;
		break;
	}

	sisfb_bpp_to_var(var);
	
	inSISIDXREG(SISSR, 0x0A, sr_data);

        inSISIDXREG(SISCR, 0x06, cr_data);

        inSISIDXREG(SISCR, 0x07, cr_data2);

	VT = (cr_data & 0xFF) | ((u16) (cr_data2 & 0x01) << 8) |
	     ((u16) (cr_data2 & 0x20) << 4) | ((u16) (sr_data & 0x01) << 10);
	A = VT + 2;

	inSISIDXREG(SISCR, 0x12, cr_data);

	VDE = (cr_data & 0xff) | ((u16) (cr_data2 & 0x02) << 7) |
	      ((u16) (cr_data2 & 0x40) << 3) | ((u16) (sr_data & 0x02) << 9);
	E = VDE + 1;

	inSISIDXREG(SISCR, 0x10, cr_data);

	VRS = (cr_data & 0xff) | ((u16) (cr_data2 & 0x04) << 6) |
	      ((u16) (cr_data2 & 0x80) << 2) | ((u16) (sr_data & 0x08) << 7);
	F = VRS + 1 - E;

	inSISIDXREG(SISCR, 0x15, cr_data);

	inSISIDXREG(SISCR, 0x09, cr_data3);

	VBS = (cr_data & 0xff) | ((u16) (cr_data2 & 0x08) << 5) |
	      ((u16) (cr_data3 & 0x20) << 4) | ((u16) (sr_data & 0x04) << 8);

	inSISIDXREG(SISCR, 0x16, cr_data);

	VBE = (cr_data & 0xff) | ((u16) (sr_data & 0x10) << 4);
	temp = VBE - ((E - 1) & 511);
	B = (temp > 0) ? temp : (temp + 512);

	inSISIDXREG(SISCR, 0x11, cr_data);

	VRE = (cr_data & 0x0f) | ((sr_data & 0x20) >> 1);
	temp = VRE - ((E + F - 1) & 31);
	C = (temp > 0) ? temp : (temp + 32);

	D = B - F - C;

        var->yres = E;
#ifndef SISFB_PAN
	var->yres_virtual = E;
#endif
	/* TW: We have to report the physical dimension to the console! */
	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		var->yres <<= 1;
#ifndef SISFB_PAN
		var->yres_virtual <<= 1;
#endif
	} else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		var->yres >>= 1;
#ifndef SISFB_PAN
		var->yres_virtual >>= 1;
#endif
	}

	var->upper_margin = D;
	var->lower_margin = F;
	var->vsync_len = C;

	inSISIDXREG(SISSR, 0x0b, sr_data);

	inSISIDXREG(SISCR, 0x00, cr_data);

	HT = (cr_data & 0xff) | ((u16) (sr_data & 0x03) << 8);
	A = HT + 5;

	inSISIDXREG(SISCR, 0x01, cr_data);

	HDE = (cr_data & 0xff) | ((u16) (sr_data & 0x0C) << 6);
	E = HDE + 1;

	inSISIDXREG(SISCR, 0x04, cr_data);

	HRS = (cr_data & 0xff) | ((u16) (sr_data & 0xC0) << 2);
	F = HRS - E - 3;

	inSISIDXREG(SISCR, 0x02, cr_data);

	HBS = (cr_data & 0xff) | ((u16) (sr_data & 0x30) << 4);

	inSISIDXREG(SISSR, 0x0c, sr_data);

	inSISIDXREG(SISCR, 0x03, cr_data);

	inSISIDXREG(SISCR, 0x05, cr_data2);

	HBE = (cr_data & 0x1f) | ((u16) (cr_data2 & 0x80) >> 2) |
	      ((u16) (sr_data & 0x03) << 6);
	HRE = (cr_data2 & 0x1f) | ((sr_data & 0x04) << 3);

	temp = HBE - ((E - 1) & 255);
	B = (temp > 0) ? temp : (temp + 256);

	temp = HRE - ((E + F + 3) & 63);
	C = (temp > 0) ? temp : (temp + 64);

	D = B - F - C;

	var->xres = var->xres_virtual = E * 8;
	var->left_margin = D * 8;
	var->right_margin = F * 8;
	var->hsync_len = C * 8;

	var->activate = FB_ACTIVATE_NOW;

	var->sync = 0;

	mr_data = inSISREG(SISMISCR);
	if (mr_data & 0x80)
		var->sync &= ~FB_SYNC_VERT_HIGH_ACT;
	else
		var->sync |= FB_SYNC_VERT_HIGH_ACT;

	if (mr_data & 0x40)
		var->sync &= ~FB_SYNC_HOR_HIGH_ACT;
	else
		var->sync |= FB_SYNC_HOR_HIGH_ACT;

	VT += 2;
	VT <<= 1;
	HT = (HT + 5) * 8;

	hrate = (double) ivideo.refresh_rate * (double) VT / 2;
	drate = hrate * HT;
	var->pixclock = (u32) (1E12 / drate);

#ifdef SISFB_PAN
	if(sisfb_ypan) {
	    var->yres_virtual = ivideo.heapstart / (var->xres * (var->bits_per_pixel >> 3));
	    if(var->yres_virtual <= var->yres) {
	        var->yres_virtual = var->yres;
	    }
	} else
#endif
	   var->yres_virtual = var->yres;

        TWDEBUG("end of crtc_to_var");
}

static int sis_getcolreg(unsigned regno, unsigned *red, unsigned *green, unsigned *blue,
			 unsigned *transp, struct fb_info *fb_info)
{
	if (regno >= ivideo.video_cmap_len)
		return 1;

	*red = sis_palette[regno].red;
	*green = sis_palette[regno].green;
	*blue = sis_palette[regno].blue;
	*transp = 0;
	return 0;
}

static int sisfb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
                           unsigned transp, struct fb_info *fb_info)
{
	if (regno >= ivideo.video_cmap_len)
		return 1;

	sis_palette[regno].red = red;
	sis_palette[regno].green = green;
	sis_palette[regno].blue = blue;

	switch (ivideo.video_bpp) {
#ifdef FBCON_HAS_CFB8
	case 8:
	        outSISREG(SISDACA, regno);
		outSISREG(SISDACD, (red >> 10));
		outSISREG(SISDACD, (green >> 10));
		outSISREG(SISDACD, (blue >> 10));
		if (ivideo.disp_state & DISPTYPE_DISP2) {
		        outSISREG(SISDAC2A, regno);
			outSISREG(SISDAC2D, (red >> 8));
			outSISREG(SISDAC2D, (green >> 8));
			outSISREG(SISDAC2D, (blue >> 8));
		}
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		sis_fbcon_cmap.cfb16[regno] =
		    ((red & 0xf800)) | ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
		red >>= 8;
		green >>= 8;
		blue >>= 8;
		sis_fbcon_cmap.cfb32[regno] = (red << 16) | (green << 8) | (blue);
		break;
#endif
	}
	return 0;
}

static void sisfb_set_disp(int con, struct fb_var_screeninfo *var,
                           struct fb_info *info)
{
	struct fb_fix_screeninfo fix;
	long   flags;
	struct display *display;
	struct display_switch *sw;

	if(con >= 0)
		display = &fb_display[con];
	else
		display = &sis_disp;

	sisfb_get_fix(&fix, con, 0);

	display->screen_base = ivideo.video_vbase;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->next_line = fix.line_length;
	display->can_soft_blank = 0;
	display->inverse = sisfb_inverse;
	display->var = *var;

	save_flags(flags);

	switch (ivideo.video_bpp) {
#ifdef FBCON_HAS_CFB8
	   case 8:
#ifdef SISFBACCEL
		sw = ivideo.accel ? &fbcon_sis8 : &fbcon_cfb8;
#else
		sw = &fbcon_cfb8;
#endif
		break;
#endif
#ifdef FBCON_HAS_CFB16
	   case 16:
#ifdef SISFBACCEL
		sw = ivideo.accel ? &fbcon_sis16 : &fbcon_cfb16;
#else
		sw = &fbcon_cfb16;
#endif
		display->dispsw_data = sis_fbcon_cmap.cfb16;
		break;
#endif
#ifdef FBCON_HAS_CFB32
	   case 32:
#ifdef SISFBACCEL
		sw = ivideo.accel ? &fbcon_sis32 : &fbcon_cfb32;
#else
		sw = &fbcon_cfb32;
#endif
		display->dispsw_data = sis_fbcon_cmap.cfb32;
		break;
#endif
	   default:
		sw = &fbcon_dummy;
		return;
	}
	memcpy(&sisfb_sw, sw, sizeof(*sw));
	display->dispsw = &sisfb_sw;
	restore_flags(flags);

#ifdef SISFB_PAN
        if((ivideo.accel) && (sisfb_ypan)) {
  	    /* display->scrollmode = SCROLL_YPAN; - not defined */
	} else {
	    display->scrollmode = SCROLL_YREDRAW;
	    sisfb_sw.bmove = fbcon_redraw_bmove;
	}
#else
	display->scrollmode = SCROLL_YREDRAW;
	sisfb_sw.bmove = fbcon_redraw_bmove;
#endif
}

static void sisfb_do_install_cmap(int con, struct fb_info *info)
{
        if (con != currcon)
		return;

        if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, 1, sisfb_setcolreg, info);
        else
		fb_set_cmap(fb_default_cmap(ivideo.video_cmap_len), 1,
			    sisfb_setcolreg, info);
}


static int sisfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	TWDEBUG("inside get_var");
	if(con == -1)
		memcpy(var, &default_var, sizeof(struct fb_var_screeninfo));
	else
		*var = fb_display[con].var;

 	/* For FSTN, DSTN */
	if (var->xres == 320 && var->yres == 480)
		var->yres = 240;
		
	TWDEBUG("end of get_var");
	return 0;
}

static int sisfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	int err;
	unsigned int cols, rows;

	TWDEBUG("inside set_var");

	fb_display[con].var.activate = FB_ACTIVATE_NOW;
        if(sisfb_do_set_var(var, con == currcon, info)) {
		sisfb_crtc_to_var(var);
		return -EINVAL;
	}

	sisfb_crtc_to_var(var);

	sisfb_set_disp(con, var, info);

	if(info->changevar)
		(*info->changevar) (con);

	if((err = fb_alloc_cmap(&fb_display[con].cmap, 0, 0)))
		return err;

	sisfb_do_install_cmap(con, info);

	cols = sisbios_mode[sisfb_mode_idx].cols;
	rows = sisbios_mode[sisfb_mode_idx].rows;
	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);

	TWDEBUG("end of set_var");
	return 0;
}

static int sisfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	TWDEBUG("inside get_cmap");
        if (con == currcon)
		return fb_get_cmap(cmap, kspc, sis_getcolreg, info);

	else if (fb_display[con].cmap.len)
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(ivideo.video_cmap_len), cmap, kspc ? 0 : 2);

	TWDEBUG("end of get_cmap");
	return 0;
}

static int sisfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	int err;

	TWDEBUG("inside set_cmap");
	if (!fb_display[con].cmap.len) {
		err = fb_alloc_cmap(&fb_display[con].cmap, ivideo.video_cmap_len, 0);
		if (err)
			return err;
	}
        
	if (con == currcon)
		return fb_set_cmap(cmap, kspc, sisfb_setcolreg, info);

	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	TWDEBUG("end of set_cmap");
	return 0;
}

#ifdef SISFB_PAN
static int sisfb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info* info)
{
	int err;
	
	TWDEBUG("inside pan_display");
	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0 || var->yoffset >= fb_display[con].var.yres_virtual || var->xoffset)
			return -EINVAL;
	} else {
		if (var->xoffset+fb_display[con].var.xres > fb_display[con].var.xres_virtual ||
		    var->yoffset+fb_display[con].var.yres > fb_display[con].var.yres_virtual)
			return -EINVAL;
	}

        if(con == currcon) {
	   if((err = sisfb_pan_var(var)) < 0) return err;
	}

	fb_display[con].var.xoffset = var->xoffset;
	fb_display[con].var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		fb_display[con].var.vmode |= FB_VMODE_YWRAP;
	else
		fb_display[con].var.vmode &= ~FB_VMODE_YWRAP;

	TWDEBUG("end of pan_display");
	return 0;
}
#endif

static int sisfb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma)
{
	struct fb_var_screeninfo var;
	unsigned long start;
	unsigned long off;
	u32 len, mmio_off;

	TWDEBUG("inside mmap");
	if(vma->vm_pgoff > (~0UL >> PAGE_SHIFT))  return -EINVAL;

	off = vma->vm_pgoff << PAGE_SHIFT;

	start = (unsigned long) ivideo.video_base;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + ivideo.video_size);
#if 0
	if (off >= len) {
		off -= len;
#endif
	/* By Jake Page: Treat mmap request with offset beyond heapstart
	 *               as request for mapping the mmio area 
	 */
	mmio_off = PAGE_ALIGN((start & ~PAGE_MASK) + ivideo.heapstart);
	if(off >= mmio_off) {
		off -= mmio_off;		
		sisfb_get_var(&var, currcon, info);
		if(var.accel_flags) return -EINVAL;

		start = (unsigned long) ivideo.mmio_base;
		len = PAGE_ALIGN((start & ~PAGE_MASK) + sisfb_mmio_size);
	}

	start &= PAGE_MASK;
	if((vma->vm_end - vma->vm_start + off) > len)	return -EINVAL;

	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;
	vma->vm_flags |= VM_IO;   /* by Jake Page; is that really needed? */

#if defined(__i386__) || defined(__x86_64__)
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) |= _PAGE_PCD;
#endif
	if (io_remap_page_range(vma->vm_start, off, vma->vm_end - vma->vm_start,
				vma->vm_page_prot))
		return -EAGAIN;

        TWDEBUG("end of mmap");
	return 0;
}

static void sis_get_glyph(struct fb_info *info, SIS_GLYINFO *gly)
{
	struct display *p = &fb_display[currcon];
	u16 c;
	u8 *cdat;
	int widthb;
	u8 *gbuf = gly->gmask;
	int size;

	TWDEBUG("Inside get_glyph");
	gly->fontheight = fontheight(p);
	gly->fontwidth = fontwidth(p);
	widthb = (fontwidth(p) + 7) / 8;

	c = gly->ch & p->charmask;
	if (fontwidth(p) <= 8)
		cdat = p->fontdata + c * fontheight(p);
	else
		cdat = p->fontdata + (c * fontheight(p) << 1);

	size = fontheight(p) * widthb;
	memcpy(gbuf, cdat, size);
	gly->ngmask = size;
	TWDEBUG("End of get_glyph");
}

static int sisfb_update_var(int con, struct fb_info *info)
{
#ifdef SISFB_PAN
        return(sisfb_pan_var(&fb_display[con].var));
#else
	return 0;
#endif	
}

static int sisfb_switch(int con, struct fb_info *info)
{
	int cols, rows;

        if(fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, 1, sis_getcolreg, info);

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

	if(!memcmp(&fb_display[con].var, &fb_display[currcon].var,
	                           sizeof(struct fb_var_screeninfo))) {
		currcon = con;
		return 1;
	}

	currcon = con;

	sisfb_do_set_var(&fb_display[con].var, 1, info);

	sisfb_set_disp(con, &fb_display[con].var, info);

	sisfb_do_install_cmap(con, info);

	cols = sisbios_mode[sisfb_mode_idx].cols;
	rows = sisbios_mode[sisfb_mode_idx].rows;
	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);

	sisfb_update_var(con, info);

	return 1;
}

static void sisfb_blank(int blank, struct fb_info *info)
{
	u8 reg;

	inSISIDXREG(SISCR, 0x17, reg);

	if(blank > 0)
		reg &= 0x7f;
	else
		reg |= 0x80;

        outSISIDXREG(SISCR, 0x17, reg);		
	outSISIDXREG(SISSR, 0x00, 0x01);    /* Synchronous Reset */
	outSISIDXREG(SISSR, 0x00, 0x03);    /* End Reset */
	printk(KERN_DEBUG "sisfb_blank() called (%d)\n", blank);
}


static int sisfb_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg, int con,
		       struct fb_info *info)
{
	TWDEBUG("inside ioctl");
	switch (cmd) {
	   case FBIO_ALLOC:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_malloc((struct sis_memreq *) arg);
		break;
	   case FBIO_FREE:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_free(*(unsigned long *) arg);
		break;
	   case FBIOGET_GLYPH:
                sis_get_glyph(info,(SIS_GLYINFO *) arg);
		break;	
	   case FBIOGET_HWCINFO:
		{
			unsigned long *hwc_offset = (unsigned long *) arg;

			if (sisfb_caps & HW_CURSOR_CAP)
				*hwc_offset = sisfb_hwcursor_vbase -
				    (unsigned long) ivideo.video_vbase;
			else
				*hwc_offset = 0;

			break;
		}
	   case FBIOPUT_MODEINFO:
		{
			struct mode_info *x = (struct mode_info *)arg;

			ivideo.video_bpp        = x->bpp;
			ivideo.video_width      = x->xres;
			ivideo.video_height     = x->yres;
			ivideo.video_vwidth     = x->v_xres;
			ivideo.video_vheight    = x->v_yres;
			ivideo.org_x            = x->org_x;
			ivideo.org_y            = x->org_y;
			ivideo.refresh_rate     = x->vrate;
			ivideo.video_linelength = ivideo.video_vwidth * (ivideo.video_bpp >> 3);
			switch(ivideo.video_bpp) {
        		case 8:
            			ivideo.DstColor = 0x0000;
	    			ivideo.SiS310_AccelDepth = 0x00000000;
				ivideo.video_cmap_len = 256;
            			break;
        		case 16:
            			ivideo.DstColor = 0x8000;
            			ivideo.SiS310_AccelDepth = 0x00010000;
				ivideo.video_cmap_len = 16;
            			break;
        		case 32:
            			ivideo.DstColor = 0xC000;
	    			ivideo.SiS310_AccelDepth = 0x00020000;
				ivideo.video_cmap_len = 16;
            			break;
			default:
				ivideo.video_cmap_len = 16;
		       	 	printk(KERN_ERR "sisfb: Unsupported depth %d", ivideo.video_bpp);
				ivideo.accel = 0;
				break;
    			}

			break;
		}
	   case FBIOGET_DISPINFO:
		sis_dispinfo((struct ap_data *)arg);
		break;
	   case SISFB_GET_INFO:  /* TW: New for communication with X driver */
	        {
			sisfb_info *x = (sisfb_info *)arg;

			x->sisfb_id = SISFB_ID;
			x->sisfb_version = VER_MAJOR;
			x->sisfb_revision = VER_MINOR;
			x->sisfb_patchlevel = VER_LEVEL;
			x->chip_id = ivideo.chip_id;
			x->memory = ivideo.video_size / 1024;
			x->heapstart = ivideo.heapstart / 1024;
			x->fbvidmode = sisfb_mode_no;
			x->sisfb_caps = sisfb_caps;
			x->sisfb_tqlen = 512; /* yet unused */
			x->sisfb_pcibus = ivideo.pcibus;
			x->sisfb_pcislot = ivideo.pcislot;
			x->sisfb_pcifunc = ivideo.pcifunc;
			x->sisfb_lcdpdc = sisfb_detectedpdc;
			x->sisfb_lcda = sisfb_detectedlcda;
	                break;
		}
	   case SISFB_GET_VBRSTATUS:
	        {
			unsigned long *vbrstatus = (unsigned long *) arg;
			if(sisfb_CheckVBRetrace()) *vbrstatus = 1;
			else		           *vbrstatus = 0;
		}
	   default:
		return -EINVAL;
	}
	TWDEBUG("end of ioctl");
	return 0;

}
#endif

/* ------------ FBDev related routines for 2.5 series ----------- */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)

static int sisfb_open(struct fb_info *info, int user)
{
    return 0;
}

static int sisfb_release(struct fb_info *info, int user)
{
    return 0;
}

static int sisfb_get_cmap_len(const struct fb_var_screeninfo *var)
{
	int rc = 16;		

	switch(var->bits_per_pixel) {
	case 8:
		rc = 256;	
		break;
	case 16:
		rc = 16;	
		break;		
	case 32:
		rc = 16;
		break;	
	}
	return rc;
}

static int sisfb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
                           unsigned transp, struct fb_info *info)
{
	if (regno >= sisfb_get_cmap_len(&info->var))
		return 1;

	switch (info->var.bits_per_pixel) {
	case 8:
	        outSISREG(SISDACA, regno);
		outSISREG(SISDACD, (red >> 10));
		outSISREG(SISDACD, (green >> 10));
		outSISREG(SISDACD, (blue >> 10));
		if (ivideo.disp_state & DISPTYPE_DISP2) {
		        outSISREG(SISDAC2A, regno);
			outSISREG(SISDAC2D, (red >> 8));
			outSISREG(SISDAC2D, (green >> 8));
			outSISREG(SISDAC2D, (blue >> 8));
		}
		break;
	case 16:
		((u32 *)(info->pseudo_palette))[regno] =
		    ((red & 0xf800)) | ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		break;
	case 32:
		red >>= 8;
		green >>= 8;
		blue >>= 8;
		((u32 *) (info->pseudo_palette))[regno] = 
			(red << 16) | (green << 8) | (blue);
		break;
	}
	return 0;
}

static int sisfb_set_par(struct fb_info *info)
{
	int err;

	TWDEBUG("inside set_par");
        if((err = sisfb_do_set_var(&info->var, 1, info)))
		return err;

	sisfb_get_fix(&info->fix, info->currcon, info);

	TWDEBUG("end of set_par");
	return 0;
}

static int sisfb_check_var(struct fb_var_screeninfo *var,
                           struct fb_info *info)
{
	unsigned int htotal =
		var->left_margin + var->xres + var->right_margin +
		var->hsync_len;
	unsigned int vtotal = 0;
	double drate = 0, hrate = 0;
	int found_mode = 0;
	int refresh_rate, search_idx;

	TWDEBUG("Inside check_var");

	if((var->vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED) {
		vtotal = var->upper_margin + var->yres + var->lower_margin +
		         var->vsync_len;   
		vtotal <<= 1;
	} else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		vtotal = var->upper_margin + var->yres + var->lower_margin +
		         var->vsync_len;   
		vtotal <<= 2;
	} else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		vtotal = var->upper_margin + (var->yres/2) + var->lower_margin +
		         var->vsync_len;   
	} else 	vtotal = var->upper_margin + var->yres + var->lower_margin +
		         var->vsync_len;

	if(!(htotal) || !(vtotal)) {
		SISFAIL("sisfb: no valid timing data");
	}

	if((var->pixclock) && (htotal)) {
	   drate = 1E12 / var->pixclock;
	   hrate = drate / htotal;
	   refresh_rate = (unsigned int) (hrate / vtotal * 2 + 0.5);
	} else refresh_rate = 60;

	/* TW: Calculation wrong for 1024x600 - force it to 60Hz */
	if((var->xres == 1024) && (var->yres == 600)) refresh_rate = 60;

	search_idx = 0;
	while( (sisbios_mode[search_idx].mode_no != 0) &&
	       (sisbios_mode[search_idx].xres <= var->xres) ) {
		if( (sisbios_mode[search_idx].xres == var->xres) &&
		    (sisbios_mode[search_idx].yres == var->yres) &&
		    (sisbios_mode[search_idx].bpp == var->bits_per_pixel)) {
		        if(sisfb_validate_mode(search_idx) > 0) {
			   found_mode = 1;
			   break;
			}
		}
		search_idx++;
	}

	if(!found_mode) {
	
		printk(KERN_ERR "sisfb: %dx%dx%d is no valid mode\n", 
			var->xres, var->yres, var->bits_per_pixel);
			
                search_idx = 0;
		while(sisbios_mode[search_idx].mode_no != 0) {
		       
		   if( (var->xres <= sisbios_mode[search_idx].xres) &&
		       (var->yres <= sisbios_mode[search_idx].yres) && 
		       (var->bits_per_pixel == sisbios_mode[search_idx].bpp) ) {
		          if(sisfb_validate_mode(search_idx) > 0) {
			     found_mode = 1;
			     break;
			  }
		   }
		   search_idx++;
	        }			
		if(found_mode) {
			var->xres = sisbios_mode[search_idx].xres;
		      	var->yres = sisbios_mode[search_idx].yres;
		      	printk(KERN_DEBUG "sisfb: Adapted to mode %dx%dx%d\n",
		   		var->xres, var->yres, var->bits_per_pixel);
		   
		} else {
		   	printk(KERN_ERR "sisfb: Failed to find similar mode to %dx%dx%d\n", 
				var->xres, var->yres, var->bits_per_pixel);
		   	return -EINVAL;
		}
	}

	/* TW: TODO: Check the refresh rate */		
	
	/* Adapt RGB settings */
	sisfb_bpp_to_var(var);	
	
	/* Sanity check for offsets */
	if (var->xoffset < 0)
		var->xoffset = 0;
	if (var->yoffset < 0)
		var->yoffset = 0;

	/* Horiz-panning not supported */
	if(var->xres != var->xres_virtual)
		var->xres_virtual = var->xres;

	if(!sisfb_ypan) {
		if(var->yres != var->yres_virtual)
			var->yres_virtual = var->yres;
	} else {
	   /* TW: Now patch yres_virtual if we use panning */
	   /* *** May I do this? *** */
	   var->yres_virtual = ivideo.heapstart / (var->xres * (var->bits_per_pixel >> 3));
	    if(var->yres_virtual <= var->yres) {
	    	/* TW: Paranoia check */
	        var->yres_virtual = var->yres;
	    }
	}
	
	/* Truncate offsets to maximum if too high */
	if (var->xoffset > var->xres_virtual - var->xres)
		var->xoffset = var->xres_virtual - var->xres - 1;

	if (var->yoffset > var->yres_virtual - var->yres)
		var->yoffset = var->yres_virtual - var->yres - 1;
	
	/* Set everything else to 0 */
	var->red.msb_right = 
	    var->green.msb_right =
	    var->blue.msb_right =
	    var->transp.offset = var->transp.length = var->transp.msb_right = 0;		
		
	TWDEBUG("end of check_var");
	return 0;
}

#ifdef SISFB_PAN
static int sisfb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info* info)
{
	int err;
	
	TWDEBUG("inside pan_display");
	
	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;
	if (var->yoffset > (var->yres_virtual - var->yres))
		return -EINVAL;

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0
		    || var->yoffset >= info->var.yres_virtual
		    || var->xoffset) return -EINVAL;
	} else {
		if (var->xoffset + info->var.xres > info->var.xres_virtual ||
		    var->yoffset + info->var.yres > info->var.yres_virtual)
			return -EINVAL;
	}
    
	if((err = sisfb_pan_var(var)) < 0) return err;

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;

	TWDEBUG("end of pan_display");
	return 0;
}
#endif

static int sisfb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma)
{
	unsigned long start;
	unsigned long off;
	u32 len, mmio_off;

	TWDEBUG("inside mmap");
	if(vma->vm_pgoff > (~0UL >> PAGE_SHIFT))  return -EINVAL;

	off = vma->vm_pgoff << PAGE_SHIFT;

	start = (unsigned long) ivideo.video_base;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + ivideo.video_size);
#if 0
	if (off >= len) {
		off -= len;
#endif
	/* By Jake Page: Treat mmap request with offset beyond heapstart
	 *               as request for mapping the mmio area 
	 */
	mmio_off = PAGE_ALIGN((start & ~PAGE_MASK) + ivideo.heapstart);
	if(off >= mmio_off) {
		off -= mmio_off;		
		if(info->var.accel_flags) return -EINVAL;

		start = (unsigned long) ivideo.mmio_base;
		len = PAGE_ALIGN((start & ~PAGE_MASK) + sisfb_mmio_size);
	}

	start &= PAGE_MASK;
	if((vma->vm_end - vma->vm_start + off) > len)	return -EINVAL;

	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;
	vma->vm_flags |= VM_IO;   /* by Jake Page; is that really needed? */

#if defined(__i386__) || defined(__x86_64__)
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) |= _PAGE_PCD;
#endif
	if (io_remap_page_range(vma, vma->vm_start, off, vma->vm_end - vma->vm_start,
				vma->vm_page_prot))
		return -EAGAIN;

        TWDEBUG("end of mmap");
	return 0;
}

static int sisfb_blank(int blank, struct fb_info *info)
{
	u8 reg;

	inSISIDXREG(SISCR, 0x17, reg);

	if(blank > 0)
		reg &= 0x7f;
	else
		reg |= 0x80;

        outSISIDXREG(SISCR, 0x17, reg);		
	outSISIDXREG(SISSR, 0x00, 0x01);    /* Synchronous Reset */
	outSISIDXREG(SISSR, 0x00, 0x03);    /* End Reset */
        return(0);
}

static int sisfb_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg, 
		       struct fb_info *info)
{
	TWDEBUG("inside ioctl");
	switch (cmd) {
	   case FBIO_ALLOC:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_malloc((struct sis_memreq *) arg);
		break;
	   case FBIO_FREE:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		sis_free(*(unsigned long *) arg);
		break;
	   case FBIOGET_HWCINFO:
		{
			unsigned long *hwc_offset = (unsigned long *) arg;

			if (sisfb_caps & HW_CURSOR_CAP)
				*hwc_offset = sisfb_hwcursor_vbase -
				    (unsigned long) ivideo.video_vbase;
			else
				*hwc_offset = 0;

			break;
		}
	   case FBIOPUT_MODEINFO:
		{
			struct mode_info *x = (struct mode_info *)arg;

			ivideo.video_bpp        = x->bpp;
			ivideo.video_width      = x->xres;
			ivideo.video_height     = x->yres;
			ivideo.video_vwidth     = x->v_xres;
			ivideo.video_vheight    = x->v_yres;
			ivideo.org_x            = x->org_x;
			ivideo.org_y            = x->org_y;
			ivideo.refresh_rate     = x->vrate;
			ivideo.video_linelength = ivideo.video_vwidth * (ivideo.video_bpp >> 3);
			switch(ivideo.video_bpp) {
        		case 8:
            			ivideo.DstColor = 0x0000;
	    			ivideo.SiS310_AccelDepth = 0x00000000;
				ivideo.video_cmap_len = 256;
            			break;
        		case 16:
            			ivideo.DstColor = 0x8000;
            			ivideo.SiS310_AccelDepth = 0x00010000;
				ivideo.video_cmap_len = 16;
            			break;
        		case 32:
            			ivideo.DstColor = 0xC000;
	    			ivideo.SiS310_AccelDepth = 0x00020000;
				ivideo.video_cmap_len = 16;
            			break;
			default:
				ivideo.video_cmap_len = 16;
		       	 	printk(KERN_ERR "sisfb: Unsupported accel depth %d", ivideo.video_bpp);
				ivideo.accel = 0;
				break;
    			}

			break;
		}
	   case FBIOGET_DISPINFO:
		sis_dispinfo((struct ap_data *)arg);
		break;
	   case SISFB_GET_INFO:  /* TW: New for communication with X driver */
	        {
			sisfb_info *x = (sisfb_info *)arg;

			x->sisfb_id = SISFB_ID;
			x->sisfb_version = VER_MAJOR;
			x->sisfb_revision = VER_MINOR;
			x->sisfb_patchlevel = VER_LEVEL;
			x->chip_id = ivideo.chip_id;
			x->memory = ivideo.video_size / 1024;
			x->heapstart = ivideo.heapstart / 1024;
			x->fbvidmode = sisfb_mode_no;
			x->sisfb_caps = sisfb_caps;
			x->sisfb_tqlen = 512; /* yet unused */
			x->sisfb_pcibus = ivideo.pcibus;
			x->sisfb_pcislot = ivideo.pcislot;
			x->sisfb_pcifunc = ivideo.pcifunc;
			x->sisfb_lcdpdc = sisfb_detectedpdc;
			x->sisfb_lcda = sisfb_detectedlcda;
	                break;
		}
	   case SISFB_GET_VBRSTATUS:
	        {
			unsigned long *vbrstatus = (unsigned long *) arg;
			if(sisfb_CheckVBRetrace()) *vbrstatus = 1;
			else		           *vbrstatus = 0;
		}
	   default:
		return -EINVAL;
	}
	TWDEBUG("end of ioctl");
	return 0;

}

#endif

/* ----------- FBDev related routines for all series ---------- */

static int sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	TWDEBUG("inside get_fix");
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	
	strcpy(fix->id, sis_fb_info.modename);
#else
	strcpy(fix->id, myid);
#endif	

	fix->smem_start = ivideo.video_base;

        /* TW */
        if((!sisfb_mem) || (sisfb_mem > (ivideo.video_size/1024))) {
	    if (ivideo.video_size > 0x1000000) {
	        fix->smem_len = 0xc00000;
	    } else if (ivideo.video_size > 0x800000)
		fix->smem_len = 0x800000;
	    else
		fix->smem_len = 0x400000;
        } else
		fix->smem_len = sisfb_mem * 1024;

	fix->type        = video_type;
	fix->type_aux    = 0;
	if(ivideo.video_bpp == 8)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_TRUECOLOR;
	fix->xpanstep    = 0;
#ifdef SISFB_PAN
        if(sisfb_ypan) 	 fix->ypanstep = 1;
#endif
	fix->ywrapstep   = 0;
	fix->line_length = ivideo.video_linelength;
	fix->mmio_start  = ivideo.mmio_base;
	fix->mmio_len    = sisfb_mmio_size;
	if(sisvga_engine == SIS_300_VGA) 
	   fix->accel    = FB_ACCEL_SIS_GLAMOUR;
	else if(ivideo.chip == SIS_330)
	   fix->accel    = FB_ACCEL_SIS_XABRE;
	else 
	   fix->accel    = FB_ACCEL_SIS_GLAMOUR_2;
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)		
	fix->reserved[0] = ivideo.video_size & 0xFFFF;
	fix->reserved[1] = (ivideo.video_size >> 16) & 0xFFFF;
	fix->reserved[2] = sisfb_caps;
#endif	

	TWDEBUG("end of get_fix");
	return 0;
}

/* ----------------  fb_ops structures ----------------- */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static struct fb_ops sisfb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	sisfb_get_fix,
	fb_get_var:	sisfb_get_var,
	fb_set_var:	sisfb_set_var,
	fb_get_cmap:	sisfb_get_cmap,
	fb_set_cmap:	sisfb_set_cmap,
#ifdef SISFB_PAN
        fb_pan_display:	sisfb_pan_display,
#endif
	fb_ioctl:	sisfb_ioctl,
	fb_mmap:	sisfb_mmap,
};
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static struct fb_ops sisfb_ops = {
	.owner        =	THIS_MODULE,
	.fb_open      = sisfb_open,
	.fb_release   = sisfb_release,
	.fb_check_var = sisfb_check_var,
	.fb_set_par   = sisfb_set_par,
	.fb_setcolreg = sisfb_setcolreg,
#ifdef SISFB_PAN
        .fb_pan_display = sisfb_pan_display,
#endif	
        .fb_blank     = sisfb_blank,
	.fb_fillrect  = fbcon_sis_fillrect,
	.fb_copyarea  = fbcon_sis_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_cursor    = soft_cursor,	
	.fb_sync      = fbcon_sis_sync,
	.fb_ioctl     =	sisfb_ioctl,
	.fb_mmap      =	sisfb_mmap,
};
#endif


/* ---------------- Chip generation dependent routines ---------------- */

#ifdef CONFIG_FB_SIS_300 /* for SiS 300/630/540/730 */

static int sisfb_get_dram_size_300(void)
{
	struct pci_dev *pdev = NULL;
	int pdev_valid = 0;
	u8  pci_data, reg;
	u16 nbridge_id;

	switch (ivideo.chip) {
	   case SIS_540:
		nbridge_id = PCI_DEVICE_ID_SI_540;
		break;
	   case SIS_630:
		nbridge_id = PCI_DEVICE_ID_SI_630;
		break;
	   case SIS_730:
		nbridge_id = PCI_DEVICE_ID_SI_730;
		break;
	   default:
		nbridge_id = 0;
		break;
	}

	if (nbridge_id == 0) {  /* 300 */

	        inSISIDXREG(SISSR, IND_SIS_DRAM_SIZE,reg);
		ivideo.video_size =
		        ((unsigned int) ((reg & SIS_DRAM_SIZE_MASK) + 1) << 20);

	} else {		/* 540, 630, 730 */

		pdev = pci_find_device(PCI_VENDOR_ID_SI, nbridge_id, pdev);
		if (pdev) {
			pci_read_config_byte(pdev, IND_BRI_DRAM_STATUS, &pci_data);
			pci_data = (pci_data & BRI_DRAM_SIZE_MASK) >> 4;
			ivideo.video_size = (unsigned int)(1 << (pci_data+21));
			pdev_valid = 1;

			reg = SIS_DATA_BUS_64 << 6;
			switch (pci_data) {
			   case BRI_DRAM_SIZE_2MB:
				reg |= SIS_DRAM_SIZE_2MB;
				break;
			   case BRI_DRAM_SIZE_4MB:
				reg |= SIS_DRAM_SIZE_4MB;
				break;
			   case BRI_DRAM_SIZE_8MB:
				reg |= SIS_DRAM_SIZE_8MB;
				break;
			   case BRI_DRAM_SIZE_16MB:
				reg |= SIS_DRAM_SIZE_16MB;
				break;
			   case BRI_DRAM_SIZE_32MB:
				reg |= SIS_DRAM_SIZE_32MB;
				break;
			   case BRI_DRAM_SIZE_64MB:
				reg |= SIS_DRAM_SIZE_64MB;
				break;
			}
			outSISIDXREG(SISSR, IND_SIS_DRAM_SIZE, reg);
		}
	
		if (!pdev_valid)  return -1;
	}
	return 0;
}

static void sisfb_detect_VB_connect_300()
{
	u8 sr16, sr17, cr32, temp;

	ivideo.TV_plug = ivideo.TV_type = 0;

        switch(ivideo.hasVB) {
	  case HASVB_LVDS_CHRONTEL:
	  case HASVB_CHRONTEL:
	     SiS_SenseCh();
	     break;
	  case HASVB_301:
	  case HASVB_302:
	     SiS_Sense30x();
	     break;
	}

	inSISIDXREG(SISSR, IND_SIS_SCRATCH_REG_17, sr17);
        inSISIDXREG(SISCR, IND_SIS_SCRATCH_REG_CR32, cr32);

	if ((sr17 & 0x0F) && (ivideo.chip != SIS_300)) {

		if ((sr17 & 0x01) && !sisfb_crt1off)
			sisfb_crt1off = 0;
		else {
			if (sr17 & 0x0E)
				sisfb_crt1off = 1;
			else
				sisfb_crt1off = 0;
		}

		if (sisfb_crt2type != -1)
			/* TW: override detected CRT2 type */
			ivideo.disp_state = sisfb_crt2type;
                else if (sr17 & 0x04)
			ivideo.disp_state = DISPTYPE_TV;			
		else if (sr17 & 0x02)
			ivideo.disp_state = DISPTYPE_LCD;			
		else if (sr17 & 0x08 )
			ivideo.disp_state = DISPTYPE_CRT2;
		else
			ivideo.disp_state = 0;

		if(sisfb_tvplug != -1)
			/* PR/TW: override detected TV type */
			ivideo.TV_plug = sisfb_tvplug;
		else if (sr17 & 0x20)
			ivideo.TV_plug = TVPLUG_SVIDEO;
		else if (sr17 & 0x10)
			ivideo.TV_plug = TVPLUG_COMPOSITE;

		inSISIDXREG(SISSR, IND_SIS_SCRATCH_REG_16, sr16);
		if (sr16 & 0x20)
			ivideo.TV_type = TVMODE_PAL;
		else
			ivideo.TV_type = TVMODE_NTSC;

	} else {

		if ((cr32 & SIS_CRT1) && !sisfb_crt1off)
			sisfb_crt1off = 0;
		else {
			if (cr32 & 0x5F)
				sisfb_crt1off = 1;
			else
				sisfb_crt1off = 0;
		}

		if (sisfb_crt2type != -1)
			/* TW: override detected CRT2 type */
			ivideo.disp_state = sisfb_crt2type;
		else if (cr32 & SIS_VB_TV)
			ivideo.disp_state = DISPTYPE_TV;
		else if (cr32 & SIS_VB_LCD)
			ivideo.disp_state = DISPTYPE_LCD;
		else if (cr32 & SIS_VB_CRT2)
			ivideo.disp_state = DISPTYPE_CRT2;
		else
			ivideo.disp_state = 0;

		/* TW: Detect TV plug & type */
		if(sisfb_tvplug != -1)
			/* PR/TW: override with option */
		        ivideo.TV_plug = sisfb_tvplug;
		else if (cr32 & SIS_VB_HIVISION) {
			ivideo.TV_type = TVMODE_HIVISION;
			ivideo.TV_plug = TVPLUG_SVIDEO;
		}
		else if (cr32 & SIS_VB_SVIDEO)
			ivideo.TV_plug = TVPLUG_SVIDEO;
		else if (cr32 & SIS_VB_COMPOSITE)
			ivideo.TV_plug = TVPLUG_COMPOSITE;
		else if (cr32 & SIS_VB_SCART)
			ivideo.TV_plug = TVPLUG_SCART;

		if (ivideo.TV_type == 0) {
		        inSISIDXREG(SISSR, IND_SIS_POWER_ON_TRAP, temp);
			if (temp & 0x01)
				ivideo.TV_type = TVMODE_PAL;
			else
				ivideo.TV_type = TVMODE_NTSC;
		}

	}

	/* TW: Copy forceCRT1 option to CRT1off if option is given */
    	if (sisfb_forcecrt1 != -1) {
    		if(sisfb_forcecrt1) sisfb_crt1off = 0;
		else                sisfb_crt1off = 1;
    	}
}

static void sisfb_get_VB_type_300(void)
{
	u8 reg;

	if(ivideo.chip != SIS_300) {
		if(!sisfb_has_VB_300()) {
		        inSISIDXREG(SISCR, IND_SIS_SCRATCH_REG_CR37, reg);
			switch ((reg & SIS_EXTERNAL_CHIP_MASK) >> 1) {
			   case SIS_EXTERNAL_CHIP_LVDS:
				ivideo.hasVB = HASVB_LVDS;
				break;
			   case SIS_EXTERNAL_CHIP_TRUMPION:
				ivideo.hasVB = HASVB_TRUMPION;
				break;
			   case SIS_EXTERNAL_CHIP_LVDS_CHRONTEL:
				ivideo.hasVB = HASVB_LVDS_CHRONTEL;
				break;
			   case SIS_EXTERNAL_CHIP_CHRONTEL:
				ivideo.hasVB = HASVB_CHRONTEL;
				break;
			   default:
				break;
			}
		}
	} else {
		sisfb_has_VB_300();
	}
}

static int sisfb_has_VB_300(void)
{
	u8 vb_chipid;

	inSISIDXREG(SISPART4, 0x00, vb_chipid);
	switch (vb_chipid) {
	   case 0x01:
		ivideo.hasVB = HASVB_301;
		break;
	   case 0x02:
		ivideo.hasVB = HASVB_302;
		break;
	   default:
		ivideo.hasVB = HASVB_NONE;
		return FALSE;
	}
	return TRUE;

}

#endif  /* CONFIG_FB_SIS_300 */


#ifdef CONFIG_FB_SIS_315    /* for SiS 315/550/650/740/330 */

static int sisfb_get_dram_size_315(void)
{
	struct pci_dev *pdev = NULL;
	int pdev_valid = 0;
	u8  pci_data;
	u8  reg = 0;

	if (ivideo.chip == SIS_550 || ivideo.chip == SIS_650 || ivideo.chip == SIS_740) {

#ifdef LINUXBIOS

		while ((pdev = pci_find_device(PCI_VENDOR_ID_SI, PCI_ANY_ID, pdev)) != NULL) {
			if ((pdev->device == PCI_DEVICE_ID_SI_550) ||
			     (pdev->device == PCI_DEVICE_ID_SI_650) ||
			     (pdev->device == PCI_DEVICE_ID_SI_740)) {
				pci_read_config_byte(pdev, IND_BRI_DRAM_STATUS,
				                     &pci_data);
				pci_data = (pci_data & BRI_DRAM_SIZE_MASK) >> 4;
				ivideo.video_size = (unsigned int)(1 << (pci_data + 21));
				pdev_valid = 1;

				/* TW: Initialize SR14 "by hand" */
				inSISIDXREG(SISSR, IND_SIS_DRAM_SIZE, reg);
				reg &= 0xC0;
				switch (pci_data) {
				   case BRI_DRAM_SIZE_4MB:
					reg |= SIS550_DRAM_SIZE_4MB;
					break;
				   case BRI_DRAM_SIZE_8MB:
					reg |= SIS550_DRAM_SIZE_8MB;
					break;
				   case BRI_DRAM_SIZE_16MB:
					reg |= SIS550_DRAM_SIZE_16MB;
					break;
				   case BRI_DRAM_SIZE_32MB:
					reg |= SIS550_DRAM_SIZE_32MB;
					break;
				   case BRI_DRAM_SIZE_64MB:
					reg |= SIS550_DRAM_SIZE_64MB;
					break;
				}

			        /* TODO: set Dual channel and bus width bits here */

				outSISIDXREG(SISSR, IND_SIS_DRAM_SIZE, reg);
				break;
			}  
		}
	
		if (!pdev_valid)  return -1;

#else

                inSISIDXREG(SISSR, IND_SIS_DRAM_SIZE, reg);
		switch (reg & SIS550_DRAM_SIZE_MASK) {
		   case SIS550_DRAM_SIZE_4MB:
			ivideo.video_size = 0x400000;   break;
		   case SIS550_DRAM_SIZE_8MB:
			ivideo.video_size = 0x800000;   break;
		   case SIS550_DRAM_SIZE_16MB:
			ivideo.video_size = 0x1000000;  break;
		   case SIS550_DRAM_SIZE_24MB:
			ivideo.video_size = 0x1800000;  break;
		   case SIS550_DRAM_SIZE_32MB:
			ivideo.video_size = 0x2000000;	break;
		   case SIS550_DRAM_SIZE_64MB:
			ivideo.video_size = 0x4000000;	break;
		   case SIS550_DRAM_SIZE_96MB:
			ivideo.video_size = 0x6000000;	break;
		   case SIS550_DRAM_SIZE_128MB:
			ivideo.video_size = 0x8000000;	break;
		   case SIS550_DRAM_SIZE_256MB:
			ivideo.video_size = 0x10000000;	break;
		   default:
		        /* TW: Some 550 BIOSes don't seem to initialize SR14 correctly (if at all),
			 *     do it the hard way ourselves in this case. Unfortunately, we don't
			 *     support 24, 48, 96 and other "odd" amounts here.
			 */
		        printk(KERN_INFO
			       "sisfb: Warning: Could not determine memory size, "
			       "now reading from PCI config\n");
			pdev_valid = 0;

			while ((pdev = pci_find_device(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_550, pdev)) != NULL) {
				pci_read_config_byte(pdev, IND_BRI_DRAM_STATUS,
				                     &pci_data);
				pci_data = (pci_data & BRI_DRAM_SIZE_MASK) >> 4;
				ivideo.video_size = (unsigned int)(1 << (pci_data+21));
				pdev_valid = 1;
				/* TW: Initialize SR14=IND_SIS_DRAM_SIZE */
				inSISIDXREG(SISSR, IND_SIS_DRAM_SIZE, reg);
				reg &= 0xC0;
				switch (pci_data) {
				   case BRI_DRAM_SIZE_4MB:
					reg |= SIS550_DRAM_SIZE_4MB;  break;
				   case BRI_DRAM_SIZE_8MB:
					reg |= SIS550_DRAM_SIZE_8MB;  break;
				   case BRI_DRAM_SIZE_16MB:
					reg |= SIS550_DRAM_SIZE_16MB; break;
				   case BRI_DRAM_SIZE_32MB:
					reg |= SIS550_DRAM_SIZE_32MB; break;
				   case BRI_DRAM_SIZE_64MB:
					reg |= SIS550_DRAM_SIZE_64MB; break;
				   default:
				   	printk(KERN_INFO "sisfb: Unable to determine memory size, giving up.\n");
					return -1;
				}
				outSISIDXREG(SISSR, IND_SIS_DRAM_SIZE, reg);
			}
			if (!pdev_valid) {
				printk(KERN_INFO "sisfb: Total confusion - No SiS PCI VGA device found?!\n");
				return -1;
			}
			return 0;
		}
#endif
		return 0;

	} else {	/* 315 */

	        inSISIDXREG(SISSR, IND_SIS_DRAM_SIZE, reg);
		switch ((reg & SIS315_DRAM_SIZE_MASK) >> 4) {
		   case SIS315_DRAM_SIZE_2MB:
			ivideo.video_size = 0x200000;
			break;
		   case SIS315_DRAM_SIZE_4MB:
			ivideo.video_size = 0x400000;
			break;
		   case SIS315_DRAM_SIZE_8MB:
			ivideo.video_size = 0x800000;
			break;
		   case SIS315_DRAM_SIZE_16MB:
			ivideo.video_size = 0x1000000;
			break;
		   case SIS315_DRAM_SIZE_32MB:
			ivideo.video_size = 0x2000000;
			break;
		   case SIS315_DRAM_SIZE_64MB:
			ivideo.video_size = 0x4000000;
			break;
		   case SIS315_DRAM_SIZE_128MB:
			ivideo.video_size = 0x8000000;
			break;
		   default:
			return -1;
		}
		
		reg &= SIS315_DUAL_CHANNEL_MASK;
		reg >>= 2;
		
		if(ivideo.chip == SIS_330) {
		
		   if(reg) ivideo.video_size <<= 1;
		
		} else {
		   
		   switch (reg) {
		      case SIS315_SINGLE_CHANNEL_2_RANK:
			   ivideo.video_size <<= 1;
			   break;
		      case SIS315_DUAL_CHANNEL_1_RANK:
			   ivideo.video_size <<= 1;
			   break;
		      case SIS315_ASYM_DDR:		/* TW: DDR asymentric */
			   ivideo.video_size += (ivideo.video_size/2);
			   break;
		   }
		}

		return 0;
	}
	
	return -1;
	
}

static void sisfb_detect_VB_connect_315(void)
{
	u8 cr32, temp=0;

	ivideo.TV_plug = ivideo.TV_type = 0;

        switch(ivideo.hasVB) {
	  case HASVB_LVDS_CHRONTEL:
	  case HASVB_CHRONTEL:
	     SiS_SenseCh();
	     break;
	  case HASVB_301:
	  case HASVB_302:
	     SiS_Sense30x();
	     break;
	}

	inSISIDXREG(SISCR, IND_SIS_SCRATCH_REG_CR32, cr32);

	if ((cr32 & SIS_CRT1) && !sisfb_crt1off)
		sisfb_crt1off = 0;
	else {
		if (cr32 & 0x5F)   
			sisfb_crt1off = 1;
		else
			sisfb_crt1off = 0;
	}

	if (sisfb_crt2type != -1)
		/* TW: Override with option */
		ivideo.disp_state = sisfb_crt2type;
	else if (cr32 & SIS_VB_TV)
		ivideo.disp_state = DISPTYPE_TV;		
	else if (cr32 & SIS_VB_LCD)
		ivideo.disp_state = DISPTYPE_LCD;		
	else if (cr32 & SIS_VB_CRT2)
		ivideo.disp_state = DISPTYPE_CRT2;
	else
		ivideo.disp_state = 0;

	if(sisfb_tvplug != -1)
		/* PR/TW: Override with option */
	        ivideo.TV_plug = sisfb_tvplug;
	else if (cr32 & SIS_VB_HIVISION) {
		ivideo.TV_type = TVMODE_HIVISION;
		ivideo.TV_plug = TVPLUG_SVIDEO;
	}
	else if (cr32 & SIS_VB_SVIDEO)
		ivideo.TV_plug = TVPLUG_SVIDEO;
	else if (cr32 & SIS_VB_COMPOSITE)
		ivideo.TV_plug = TVPLUG_COMPOSITE;
	else if (cr32 & SIS_VB_SCART)
		ivideo.TV_plug = TVPLUG_SCART;

	if(ivideo.TV_type == 0) {
	    /* TW: PAL/NTSC changed for 650 */
	    if((ivideo.chip <= SIS_315PRO) || (ivideo.chip >= SIS_330)) {

                inSISIDXREG(SISCR, 0x38, temp);
		if(temp & 0x10)
			ivideo.TV_type = TVMODE_PAL;
		else
			ivideo.TV_type = TVMODE_NTSC;

	    } else {

	        inSISIDXREG(SISCR, 0x79, temp);
		if(temp & 0x20)
			ivideo.TV_type = TVMODE_PAL;
		else
			ivideo.TV_type = TVMODE_NTSC;
	    }
	}

	/* TW: Copy forceCRT1 option to CRT1off if option is given */
    	if (sisfb_forcecrt1 != -1) {
    		if (sisfb_forcecrt1) sisfb_crt1off = 0;
		else   	             sisfb_crt1off = 1;
    	}
}

static void sisfb_get_VB_type_315(void)
{
	u8 reg;

	if (!sisfb_has_VB_315()) {
	        inSISIDXREG(SISCR, IND_SIS_SCRATCH_REG_CR37, reg);
		switch ((reg & SIS_EXTERNAL_CHIP_MASK) >> 1) {
	 	   case SIS310_EXTERNAL_CHIP_LVDS:
			ivideo.hasVB = HASVB_LVDS;
			break;
		   case SIS310_EXTERNAL_CHIP_LVDS_CHRONTEL:
			ivideo.hasVB = HASVB_LVDS_CHRONTEL;
			break;
		   default:
			break;
		}
	}
}


static int sisfb_has_VB_315(void)
{
	u8 vb_chipid;

	inSISIDXREG(SISPART4, 0x00, vb_chipid);
	switch (vb_chipid) {
	   case 0x01:
		ivideo.hasVB = HASVB_301;
		break;
	   case 0x02:
		ivideo.hasVB = HASVB_302;
		break;
	   default:
		ivideo.hasVB = HASVB_NONE;
		return FALSE;
	}
	return TRUE;
}

#endif   /* CONFIG_FB_SIS_315 */

/* ------------------ Sensing routines ------------------ */

/* TW: Determine and detect attached devices on SiS30x */
int
SISDoSense(int tempbl, int tempbh, int tempcl, int tempch)
{
    int temp,i;

    outSISIDXREG(SISPART4,0x11,tempbl);
    temp = tempbh | tempcl;
    setSISIDXREG(SISPART4,0x10,0xe0,temp);
    for(i=0; i<10; i++) SiS_LongWait(&SiS_Pr);
    tempch &= 0x7f;
    inSISIDXREG(SISPART4,0x03,temp);
    temp ^= 0x0e;
    temp &= tempch;
    return(temp);
}

void
SiS_Sense30x(void)
{
  u8 backupP4_0d;
  u8 testsvhs_tempbl, testsvhs_tempbh;
  u8 testsvhs_tempcl, testsvhs_tempch;
  u8 testcvbs_tempbl, testcvbs_tempbh;
  u8 testcvbs_tempcl, testcvbs_tempch;
  u8 testvga2_tempbl, testvga2_tempbh;
  u8 testvga2_tempcl, testvga2_tempch;
  int myflag, result;

  inSISIDXREG(SISPART4,0x0d,backupP4_0d);
  outSISIDXREG(SISPART4,0x0d,(backupP4_0d | 0x04));

  if(sisvga_engine == SIS_300_VGA) {

  	testvga2_tempbh = 0x00; testvga2_tempbl = 0xd1;
        testsvhs_tempbh = 0x00; testsvhs_tempbl = 0xb9;
	testcvbs_tempbh = 0x00; testcvbs_tempbl = 0xb3;
	if((sishw_ext.ujVBChipID != VB_CHIP_301) &&
	   (sishw_ext.ujVBChipID != VB_CHIP_302) ) {
	   testvga2_tempbh = 0x01; testvga2_tempbl = 0x90;
	   testsvhs_tempbh = 0x01; testsvhs_tempbl = 0x6b;
	   testcvbs_tempbh = 0x01; testcvbs_tempbl = 0x74;
	}
	inSISIDXREG(SISPART4,0x01,myflag);
	if(myflag & 0x04) {
	   testvga2_tempbh = 0x00; testvga2_tempbl = 0xfd;
	   testsvhs_tempbh = 0x00; testsvhs_tempbl = 0xdd;
	   testcvbs_tempbh = 0x00; testcvbs_tempbl = 0xee;
	}
	testvga2_tempch = 0x0e;	testvga2_tempcl = 0x08;
	testsvhs_tempch = 0x06;	testsvhs_tempcl = 0x04;
	testcvbs_tempch = 0x08; testcvbs_tempcl = 0x04;
	if(ivideo.chip == SIS_300) {
	   inSISIDXREG(SISSR,0x3b,myflag);
	   if(!(myflag & 0x01)) {
	      testvga2_tempbh = 0x00; testvga2_tempbl = 0x00;
	      testvga2_tempch = 0x00; testvga2_tempcl = 0x00;
	   }
	}

  } else {

	testvga2_tempbh = 0x00; testvga2_tempbl = 0xd1;
        testsvhs_tempbh = 0x00; testsvhs_tempbl = 0xb9;
	testcvbs_tempbh = 0x00; testcvbs_tempbl = 0xb3;
	if((sishw_ext.ujVBChipID != VB_CHIP_301) &&
	   (sishw_ext.ujVBChipID != VB_CHIP_302)) {
	      testvga2_tempbh = 0x01; testvga2_tempbl = 0x90;
	      testsvhs_tempbh = 0x01; testsvhs_tempbl = 0x6b;
	      testcvbs_tempbh = 0x01; testcvbs_tempbl = 0x74;
	      if(sishw_ext.ujVBChipID == VB_CHIP_301LV ||
	         sishw_ext.ujVBChipID == VB_CHIP_302LV) {
	         testvga2_tempbh = 0x00; testvga2_tempbl = 0x00;
	         testsvhs_tempbh = 0x02; testsvhs_tempbl = 0x00;
	         testcvbs_tempbh = 0x01; testcvbs_tempbl = 0x00;
	      }
	}
	if(sishw_ext.ujVBChipID != VB_CHIP_301LV &&
	   sishw_ext.ujVBChipID != VB_CHIP_302LV) {
	   inSISIDXREG(SISPART4,0x01,myflag);
	   if(myflag & 0x04) {
	      testvga2_tempbh = 0x00; testvga2_tempbl = 0xfd;
	      testsvhs_tempbh = 0x00; testsvhs_tempbl = 0xdd;
	      testcvbs_tempbh = 0x00; testcvbs_tempbl = 0xee;
	   }
	}
	if((sishw_ext.ujVBChipID == VB_CHIP_301LV) ||
	   (sishw_ext.ujVBChipID == VB_CHIP_302LV) ) {
	   testvga2_tempbh = 0x00; testvga2_tempbl = 0x00;
	   testvga2_tempch = 0x00; testvga2_tempcl = 0x00;
	   testsvhs_tempch = 0x04; testsvhs_tempcl = 0x08;
	   testcvbs_tempch = 0x08; testcvbs_tempcl = 0x08;
	} else {
	   testvga2_tempch = 0x0e; testvga2_tempcl = 0x08;
	   testsvhs_tempch = 0x06; testsvhs_tempcl = 0x04;
	   testcvbs_tempch = 0x08; testcvbs_tempcl = 0x04;
	}
    } 

    if(testvga2_tempch || testvga2_tempcl || testvga2_tempbh || testvga2_tempbl) {
        result = SISDoSense(testvga2_tempbl, testvga2_tempbh,
                            testvga2_tempcl, testvga2_tempch);
 	if(result) {
        	printk(KERN_INFO "sisfb: Detected secondary VGA connection\n");
		orSISIDXREG(SISCR, 0x32, 0x10);
	}
    }
    
    result = SISDoSense(testsvhs_tempbl, testsvhs_tempbh,
                        testsvhs_tempcl, testsvhs_tempch);
    if(result) {
        printk(KERN_INFO "sisfb: Detected TV connected to SVHS output\n");
        /* TW: So we can be sure that there IS a SVHS output */
	ivideo.TV_plug = TVPLUG_SVIDEO;
	orSISIDXREG(SISCR, 0x32, 0x02);
    }

    if(!result) {
        result = SISDoSense(testcvbs_tempbl, testcvbs_tempbh,
	                    testcvbs_tempcl, testcvbs_tempch);
	if(result) {
	    printk(KERN_INFO "sisfb: Detected TV connected to CVBS output\n");
	    /* TW: So we can be sure that there IS a CVBS output */
	    ivideo.TV_plug = TVPLUG_COMPOSITE;
	    orSISIDXREG(SISCR, 0x32, 0x01);
	}
    }
    SISDoSense(0, 0, 0, 0);

    outSISIDXREG(SISPART4,0x0d,backupP4_0d);
}

/* TW: Determine and detect attached TV's on Chrontel */
void
SiS_SenseCh(void)
{

   u8 temp1;
#ifdef CONFIG_FB_SIS_315
   u8 temp2;
#endif

   if(ivideo.chip < SIS_315H) {

#ifdef CONFIG_FB_SIS_300
       SiS_Pr.SiS_IF_DEF_CH70xx = 1;		/* TW: Chrontel 7005 */
       temp1 = SiS_GetCH700x(&SiS_Pr, 0x25);
       if ((temp1 >= 50) && (temp1 <= 100)) {
	   /* TW: Read power status */
	   temp1 = SiS_GetCH700x(&SiS_Pr, 0x0e);
	   if((temp1 & 0x03) != 0x03) {
     	        /* TW: Power all outputs */
		SiS_SetCH70xxANDOR(&SiS_Pr, 0x030E,0xF8);
	   }
	   /* TW: Sense connected TV devices */
	   SiS_SetCH700x(&SiS_Pr, 0x0110);
	   SiS_SetCH700x(&SiS_Pr, 0x0010);
	   temp1 = SiS_GetCH700x(&SiS_Pr, 0x10);
	   if(!(temp1 & 0x08)) {
		printk(KERN_INFO
		   "sisfb: Chrontel: Detected TV connected to SVHS output\n");
		/* TW: So we can be sure that there IS a SVHS output */
		ivideo.TV_plug = TVPLUG_SVIDEO;
		orSISIDXREG(SISCR, 0x32, 0x02);
	   } else if (!(temp1 & 0x02)) {
		printk(KERN_INFO
		   "sisfb: Chrontel: Detected TV connected to CVBS output\n");
		/* TW: So we can be sure that there IS a CVBS output */
		ivideo.TV_plug = TVPLUG_COMPOSITE;
		orSISIDXREG(SISCR, 0x32, 0x01);
	   } else {
 		SiS_SetCH70xxANDOR(&SiS_Pr, 0x010E,0xF8);
	   }
       } else if(temp1 == 0) {
	  SiS_SetCH70xxANDOR(&SiS_Pr, 0x010E,0xF8);
       }
#endif

   } else {

#ifdef CONFIG_FB_SIS_315
	SiS_Pr.SiS_IF_DEF_CH70xx = 2;		/* TW: Chrontel 7019 */
        temp1 = SiS_GetCH701x(&SiS_Pr, 0x49);
	SiS_SetCH701x(&SiS_Pr, 0x2049);
	SiS_DDC2Delay(&SiS_Pr, 0x96);
	temp2 = SiS_GetCH701x(&SiS_Pr, 0x20);
	temp2 |= 0x01;
	SiS_SetCH701x(&SiS_Pr, (temp2 << 8) | 0x20);
	SiS_DDC2Delay(&SiS_Pr, 0x96);
	temp2 ^= 0x01;
	SiS_SetCH701x(&SiS_Pr, (temp2 << 8) | 0x20);
	SiS_DDC2Delay(&SiS_Pr, 0x96);
	temp2 = SiS_GetCH701x(&SiS_Pr, 0x20);
	SiS_SetCH701x(&SiS_Pr, (temp1 << 8) | 0x49);
        temp1 = 0;
	if(temp2 & 0x02) temp1 |= 0x01;
	if(temp2 & 0x10) temp1 |= 0x01;
	if(temp2 & 0x04) temp1 |= 0x02;
	if( (temp1 & 0x01) && (temp1 & 0x02) ) temp1 = 0x04;
	switch(temp1) {
	case 0x01:
	     printk(KERN_INFO
		"sisfb: Chrontel: Detected TV connected to CVBS output\n");
	     ivideo.TV_plug = TVPLUG_COMPOSITE;
	     orSISIDXREG(SISCR, 0x32, 0x01);
             break;
	case 0x02:
	     printk(KERN_INFO
		"sisfb: Chrontel: Detected TV connected to SVHS output\n");
	     ivideo.TV_plug = TVPLUG_SVIDEO;
	     orSISIDXREG(SISCR, 0x32, 0x02);
             break;
	case 0x04:
	     /* TW: This should not happen */
	     printk(KERN_INFO
		"sisfb: Chrontel: Detected TV connected to SCART output\n");
             break;
	}
#endif

   }
}


/* ------------------------ Heap routines -------------------------- */

static int sisfb_heap_init(void)
{
	SIS_OH *poh;
	u8 temp=0;
#ifdef CONFIG_FB_SIS_315
	int            agp_enabled = 1;
	u32            agp_size;
	unsigned long *cmdq_baseport = 0;
	unsigned long *read_port = 0;
	unsigned long *write_port = 0;
	SIS_CMDTYPE    cmd_type;
#ifndef AGPOFF
	struct agp_kern_info  *agp_info;
	struct agp_memory     *agp;
	u32            agp_phys;
#endif
#endif
/* TW: The heap start is either set manually using the "mem" parameter, or
 *     defaults as follows:
 *     -) If more than 16MB videoRAM available, let our heap start at 12MB.
 *     -) If more than  8MB videoRAM available, let our heap start at  8MB.
 *     -) If 4MB or less is available, let it start at 4MB.
 *     This is for avoiding a clash with X driver which uses the beginning
 *     of the videoRAM. To limit size of X framebuffer, use Option MaxXFBMem
 *     in XF86Config-4.
 *     The heap start can also be specified by parameter "mem" when starting the sisfb
 *     driver. sisfb mem=1024 lets heap starts at 1MB, etc.
 */
     if ((!sisfb_mem) || (sisfb_mem > (ivideo.video_size/1024))) {
        if (ivideo.video_size > 0x1000000) {
	        ivideo.heapstart = 0xc00000;
	} else if (ivideo.video_size > 0x800000) {
	        ivideo.heapstart = 0x800000;
	} else {
		ivideo.heapstart = 0x400000;
	}
     } else {
           ivideo.heapstart = sisfb_mem * 1024;
     }
     sisfb_heap_start =
	       (unsigned long) (ivideo.video_vbase + ivideo.heapstart);
     printk(KERN_INFO "sisfb: Memory heap starting at %dK\n",
     					(int)(ivideo.heapstart / 1024));

     sisfb_heap_end = (unsigned long) ivideo.video_vbase + ivideo.video_size;
     sisfb_heap_size = sisfb_heap_end - sisfb_heap_start;

#ifdef CONFIG_FB_SIS_315
     if (sisvga_engine == SIS_315_VGA) {
        /* TW: Now initialize the 310 series' command queue mode.
	 * On 310/325, there are three queue modes available which
	 * are chosen by setting bits 7:5 in SR26:
	 * 1. MMIO queue mode (bit 5, 0x20). The hardware will keep
	 *    track of the queue, the FIFO, command parsing and so
	 *    on. This is the one comparable to the 300 series.
	 * 2. VRAM queue mode (bit 6, 0x40). In this case, one will
	 *    have to do queue management himself. Register 0x85c4 will
	 *    hold the location of the next free queue slot, 0x85c8
	 *    is the "queue read pointer" whose way of working is
	 *    unknown to me. Anyway, this mode would require a
	 *    translation of the MMIO commands to some kind of
	 *    accelerator assembly and writing these commands
	 *    to the memory location pointed to by 0x85c4.
	 *    We will not use this, as nobody knows how this
	 *    "assembly" works, and as it would require a complete
	 *    re-write of the accelerator code.
	 * 3. AGP queue mode (bit 7, 0x80). Works as 2., but keeps the
	 *    queue in AGP memory space.
	 *
	 * SR26 bit 4 is called "Bypass H/W queue".
	 * SR26 bit 1 is called "Enable Command Queue Auto Correction"
	 * SR26 bit 0 resets the queue
	 * Size of queue memory is encoded in bits 3:2 like this:
	 *    00  (0x00)  512K
	 *    01  (0x04)  1M
	 *    10  (0x08)  2M
	 *    11  (0x0C)  4M
	 * The queue location is to be written to 0x85C0.
	 *
         */
	cmdq_baseport = (unsigned long *)(ivideo.mmio_vbase + MMIO_QUEUE_PHYBASE);
	write_port    = (unsigned long *)(ivideo.mmio_vbase + MMIO_QUEUE_WRITEPORT);
	read_port     = (unsigned long *)(ivideo.mmio_vbase + MMIO_QUEUE_READPORT);

	DPRINTK("AGP base: 0x%p, read: 0x%p, write: 0x%p\n", cmdq_baseport, read_port, write_port);

	agp_size  = COMMAND_QUEUE_AREA_SIZE;

#ifndef AGPOFF
	if (sisfb_queuemode == AGP_CMD_QUEUE) {
		agp_info = vmalloc(sizeof(*agp_info));
		memset((void*)agp_info, 0x00, sizeof(*agp_info));
		agp_copy_info(agp_info);

		agp_backend_acquire();

		agp = agp_allocate_memory(COMMAND_QUEUE_AREA_SIZE/PAGE_SIZE,
					  AGP_NORMAL_MEMORY);
		if (agp == NULL) {
			DPRINTK("sisfb: Allocating AGP buffer failed.\n");
			agp_enabled = 0;
		} else {
			if (agp_bind_memory(agp, agp->pg_start) != 0) {
				DPRINTK("sisfb: AGP: Failed to bind memory\n");
				/* TODO: Free AGP memory here */
				agp_enabled = 0;
			} else {
				agp_enable(0);
			}
		}
	}
#else
	agp_enabled = 0;
#endif

	/* TW: Now select the queue mode */

	if ((agp_enabled) && (sisfb_queuemode == AGP_CMD_QUEUE)) {
		cmd_type = AGP_CMD_QUEUE;
		printk(KERN_INFO "sisfb: Using AGP queue mode\n");
/*	} else if (sisfb_heap_size >= COMMAND_QUEUE_AREA_SIZE)  */
        } else if (sisfb_queuemode == VM_CMD_QUEUE) {
		cmd_type = VM_CMD_QUEUE;
		printk(KERN_INFO "sisfb: Using VRAM queue mode\n");
	} else {
		printk(KERN_INFO "sisfb: Using MMIO queue mode\n");
		cmd_type = MMIO_CMD;
	}

	switch (agp_size) {
	   case 0x80000:
		temp = SIS_CMD_QUEUE_SIZE_512k;
		break;
	   case 0x100000:
		temp = SIS_CMD_QUEUE_SIZE_1M;
		break;
	   case 0x200000:
		temp = SIS_CMD_QUEUE_SIZE_2M;
		break;
	   case 0x400000:
		temp = SIS_CMD_QUEUE_SIZE_4M;
		break;
	}

	switch (cmd_type) {
	   case AGP_CMD_QUEUE:
#ifndef AGPOFF
		DPRINTK("sisfb: AGP buffer base = 0x%lx, offset = 0x%x, size = %dK\n",
			agp_info->aper_base, agp->physical, agp_size/1024);

		agp_phys = agp_info->aper_base + agp->physical;

		outSISIDXREG(SISCR,  IND_SIS_AGP_IO_PAD, 0);
		outSISIDXREG(SISCR,  IND_SIS_AGP_IO_PAD, SIS_AGP_2X);

                outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_THRESHOLD, COMMAND_QUEUE_THRESHOLD);

		outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_SET, SIS_CMD_QUEUE_RESET);

		*write_port = *read_port;

		temp |= SIS_AGP_CMDQUEUE_ENABLE;
		outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_SET, temp);

		*cmdq_baseport = agp_phys;

		sisfb_caps |= AGP_CMD_QUEUE_CAP;
#endif
		break;

	   case VM_CMD_QUEUE:
		sisfb_heap_end -= COMMAND_QUEUE_AREA_SIZE;
		sisfb_heap_size -= COMMAND_QUEUE_AREA_SIZE;

		outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_THRESHOLD, COMMAND_QUEUE_THRESHOLD);

		outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_SET, SIS_CMD_QUEUE_RESET);

		*write_port = *read_port;

		temp |= SIS_VRAM_CMDQUEUE_ENABLE;
		outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_SET, temp);

		*cmdq_baseport = ivideo.video_size - COMMAND_QUEUE_AREA_SIZE;

		sisfb_caps |= VM_CMD_QUEUE_CAP;

		DPRINTK("sisfb: VM Cmd Queue offset = 0x%lx, size is %dK\n",
			*cmdq_baseport, COMMAND_QUEUE_AREA_SIZE/1024);
		break;

	   default:  /* MMIO */
	   	/* TW: This previously only wrote SIS_MMIO_CMD_ENABLE
		 * to IND_SIS_CMDQUEUE_SET. I doubt that this is
		 * enough. Reserve memory in any way.
		 */
	   	sisfb_heap_end -= COMMAND_QUEUE_AREA_SIZE;
		sisfb_heap_size -= COMMAND_QUEUE_AREA_SIZE;

		outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_THRESHOLD, COMMAND_QUEUE_THRESHOLD);
		outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_SET, SIS_CMD_QUEUE_RESET);

		*write_port = *read_port;

		/* TW: Set Auto_Correction bit */
		temp |= (SIS_MMIO_CMD_ENABLE | SIS_CMD_AUTO_CORR);
		outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_SET, temp);

		*cmdq_baseport = ivideo.video_size - COMMAND_QUEUE_AREA_SIZE;

		sisfb_caps |= MMIO_CMD_QUEUE_CAP;

		DPRINTK("sisfb: MMIO Cmd Queue offset = 0x%lx, size is %dK\n",
			*cmdq_baseport, COMMAND_QUEUE_AREA_SIZE/1024);
		break;
	}
     } /* sisvga_engine = 315 */
#endif

#ifdef CONFIG_FB_SIS_300
     if (sisvga_engine == SIS_300_VGA) {
  	    /* TW: Now initialize TurboQueue. TB is always located at the very
	     * top of the video RAM. */
	    if (sisfb_heap_size >= TURBO_QUEUE_AREA_SIZE) {
		unsigned int  tqueue_pos;
		u8 tq_state;

		tqueue_pos = (ivideo.video_size -
		       TURBO_QUEUE_AREA_SIZE) / (64 * 1024);

		temp = (u8) (tqueue_pos & 0xff);

		inSISIDXREG(SISSR, IND_SIS_TURBOQUEUE_SET, tq_state);
		tq_state |= 0xf0;
		tq_state &= 0xfc;
		tq_state |= (u8) (tqueue_pos >> 8);
		outSISIDXREG(SISSR, IND_SIS_TURBOQUEUE_SET, tq_state);

		outSISIDXREG(SISSR, IND_SIS_TURBOQUEUE_ADR, temp);

		sisfb_caps |= TURBO_QUEUE_CAP;

		sisfb_heap_end -= TURBO_QUEUE_AREA_SIZE;
		sisfb_heap_size -= TURBO_QUEUE_AREA_SIZE;
		DPRINTK("sisfb: TurboQueue start at 0x%lx, size is %dK\n",
			sisfb_heap_end, TURBO_QUEUE_AREA_SIZE/1024);
	    }
     }
#endif
     /* TW: Now reserve memory for the HWCursor. It is always located at the very
            top of the videoRAM, right below the TB memory area (if used). */
     if (sisfb_heap_size >= sisfb_hwcursor_size) {
		sisfb_heap_end -= sisfb_hwcursor_size;
		sisfb_heap_size -= sisfb_hwcursor_size;
		sisfb_hwcursor_vbase = sisfb_heap_end;

		sisfb_caps |= HW_CURSOR_CAP;

		DPRINTK("sisfb: Hardware Cursor start at 0x%lx, size is %dK\n",
			sisfb_heap_end, sisfb_hwcursor_size/1024);
     }

     sisfb_heap.poha_chain = NULL;
     sisfb_heap.poh_freelist = NULL;

     poh = sisfb_poh_new_node();

     if(poh == NULL)  return 1;
	
     poh->poh_next = &sisfb_heap.oh_free;
     poh->poh_prev = &sisfb_heap.oh_free;
     poh->size = sisfb_heap_end - sisfb_heap_start + 1;
     poh->offset = sisfb_heap_start - (unsigned long) ivideo.video_vbase;

     DPRINTK("sisfb: Heap start:0x%p, end:0x%p, len=%dk\n",
		(char *) sisfb_heap_start, (char *) sisfb_heap_end,
		(unsigned int) poh->size / 1024);

     DPRINTK("sisfb: First Node offset:0x%x, size:%dk\n",
		(unsigned int) poh->offset, (unsigned int) poh->size / 1024);

     sisfb_heap.oh_free.poh_next = poh;
     sisfb_heap.oh_free.poh_prev = poh;
     sisfb_heap.oh_free.size = 0;
     sisfb_heap.max_freesize = poh->size;

     sisfb_heap.oh_used.poh_next = &sisfb_heap.oh_used;
     sisfb_heap.oh_used.poh_prev = &sisfb_heap.oh_used;
     sisfb_heap.oh_used.size = SENTINEL;

     return 0;
}

static SIS_OH *sisfb_poh_new_node(void)
{
	int           i;
	unsigned long cOhs;
	SIS_OHALLOC   *poha;
	SIS_OH        *poh;

	if (sisfb_heap.poh_freelist == NULL) {
		poha = kmalloc(OH_ALLOC_SIZE, GFP_KERNEL);
		if(!poha) return NULL;

		poha->poha_next = sisfb_heap.poha_chain;
		sisfb_heap.poha_chain = poha;

		cOhs = (OH_ALLOC_SIZE - sizeof(SIS_OHALLOC)) / sizeof(SIS_OH) + 1;

		poh = &poha->aoh[0];
		for (i = cOhs - 1; i != 0; i--) {
			poh->poh_next = poh + 1;
			poh = poh + 1;
		}

		poh->poh_next = NULL;
		sisfb_heap.poh_freelist = &poha->aoh[0];
	}

	poh = sisfb_heap.poh_freelist;
	sisfb_heap.poh_freelist = poh->poh_next;

	return (poh);
}

static SIS_OH *sisfb_poh_allocate(unsigned long size)
{
	SIS_OH *pohThis;
	SIS_OH *pohRoot;
	int     bAllocated = 0;

	if (size > sisfb_heap.max_freesize) {
		DPRINTK("sisfb: Can't allocate %dk size on offscreen\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	pohThis = sisfb_heap.oh_free.poh_next;

	while (pohThis != &sisfb_heap.oh_free) {
		if (size <= pohThis->size) {
			bAllocated = 1;
			break;
		}
		pohThis = pohThis->poh_next;
	}

	if (!bAllocated) {
		DPRINTK("sisfb: Can't allocate %dk size on offscreen\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	if (size == pohThis->size) {
		pohRoot = pohThis;
		sisfb_delete_node(pohThis);
	} else {
		pohRoot = sisfb_poh_new_node();

		if (pohRoot == NULL) {
			return (NULL);
		}

		pohRoot->offset = pohThis->offset;
		pohRoot->size = size;

		pohThis->offset += size;
		pohThis->size -= size;
	}

	sisfb_heap.max_freesize -= size;

	pohThis = &sisfb_heap.oh_used;
	sisfb_insert_node(pohThis, pohRoot);

	return (pohRoot);
}

static void sisfb_delete_node(SIS_OH *poh)
{
	SIS_OH *poh_prev;
	SIS_OH *poh_next;

	poh_prev = poh->poh_prev;
	poh_next = poh->poh_next;

	poh_prev->poh_next = poh_next;
	poh_next->poh_prev = poh_prev;

}

static void sisfb_insert_node(SIS_OH *pohList, SIS_OH *poh)
{
	SIS_OH *pohTemp;

	pohTemp = pohList->poh_next;

	pohList->poh_next = poh;
	pohTemp->poh_prev = poh;

	poh->poh_prev = pohList;
	poh->poh_next = pohTemp;
}

static SIS_OH *sisfb_poh_free(unsigned long base)
{
	SIS_OH *pohThis;
	SIS_OH *poh_freed;
	SIS_OH *poh_prev;
	SIS_OH *poh_next;
	unsigned long ulUpper;
	unsigned long ulLower;
	int foundNode = 0;

	poh_freed = sisfb_heap.oh_used.poh_next;

	while(poh_freed != &sisfb_heap.oh_used) {
		if(poh_freed->offset == base) {
			foundNode = 1;
			break;
		}

		poh_freed = poh_freed->poh_next;
	}

	if (!foundNode)  return (NULL);

	sisfb_heap.max_freesize += poh_freed->size;

	poh_prev = poh_next = NULL;
	ulUpper = poh_freed->offset + poh_freed->size;
	ulLower = poh_freed->offset;

	pohThis = sisfb_heap.oh_free.poh_next;

	while (pohThis != &sisfb_heap.oh_free) {
		if (pohThis->offset == ulUpper) {
			poh_next = pohThis;
		}
			else if ((pohThis->offset + pohThis->size) ==
				 ulLower) {
			poh_prev = pohThis;
		}
		pohThis = pohThis->poh_next;
	}

	sisfb_delete_node(poh_freed);

	if (poh_prev && poh_next) {
		poh_prev->size += (poh_freed->size + poh_next->size);
		sisfb_delete_node(poh_next);
		sisfb_free_node(poh_freed);
		sisfb_free_node(poh_next);
		return (poh_prev);
	}

	if (poh_prev) {
		poh_prev->size += poh_freed->size;
		sisfb_free_node(poh_freed);
		return (poh_prev);
	}

	if (poh_next) {
		poh_next->size += poh_freed->size;
		poh_next->offset = poh_freed->offset;
		sisfb_free_node(poh_freed);
		return (poh_next);
	}

	sisfb_insert_node(&sisfb_heap.oh_free, poh_freed);

	return (poh_freed);
}

static void sisfb_free_node(SIS_OH *poh)
{
	if(poh == NULL) return;

	poh->poh_next = sisfb_heap.poh_freelist;
	sisfb_heap.poh_freelist = poh;

}

void sis_malloc(struct sis_memreq *req)
{
	SIS_OH *poh;

	poh = sisfb_poh_allocate(req->size);

	if(poh == NULL) {
		req->offset = 0;
		req->size = 0;
		DPRINTK("sisfb: Video RAM allocation failed\n");
	} else {
		DPRINTK("sisfb: Video RAM allocation succeeded: 0x%p\n",
			(char *) (poh->offset + (unsigned long) ivideo.video_vbase));

		req->offset = poh->offset;
		req->size = poh->size;
	}

}

void sis_free(unsigned long base)
{
	SIS_OH *poh;

	poh = sisfb_poh_free(base);

	if(poh == NULL) {
		DPRINTK("sisfb: sisfb_poh_free() failed at base 0x%x\n",
			(unsigned int) base);
	}
}

/* --------------------- SetMode routines ------------------------- */

static void sisfb_pre_setmode(void)
{
	u8 cr30 = 0, cr31 = 0;

	inSISIDXREG(SISCR, 0x31, cr31);
	cr31 &= ~0x60;

	switch (ivideo.disp_state & DISPTYPE_DISP2) {
	   case DISPTYPE_CRT2:
		cr30 = (SIS_VB_OUTPUT_CRT2 | SIS_SIMULTANEOUS_VIEW_ENABLE);
		cr31 |= SIS_DRIVER_MODE;
		break;
	   case DISPTYPE_LCD:
		cr30  = (SIS_VB_OUTPUT_LCD | SIS_SIMULTANEOUS_VIEW_ENABLE);
		cr31 |= SIS_DRIVER_MODE;
		break;
	   case DISPTYPE_TV:
		if (ivideo.TV_type == TVMODE_HIVISION)
			cr30 = (SIS_VB_OUTPUT_HIVISION | SIS_SIMULTANEOUS_VIEW_ENABLE);
		else if (ivideo.TV_plug == TVPLUG_SVIDEO)
			cr30 = (SIS_VB_OUTPUT_SVIDEO | SIS_SIMULTANEOUS_VIEW_ENABLE);
		else if (ivideo.TV_plug == TVPLUG_COMPOSITE)
			cr30 = (SIS_VB_OUTPUT_COMPOSITE | SIS_SIMULTANEOUS_VIEW_ENABLE);
		else if (ivideo.TV_plug == TVPLUG_SCART)
			cr30 = (SIS_VB_OUTPUT_SCART | SIS_SIMULTANEOUS_VIEW_ENABLE);
		cr31 |= SIS_DRIVER_MODE;

	        if (sisfb_tvmode == 1 || ivideo.TV_type == TVMODE_PAL)
			cr31 |= 0x01;
                else
                        cr31 &= ~0x01;
		break;
	   default:	/* disable CRT2 */
		cr30 = 0x00;
		cr31 |= (SIS_DRIVER_MODE | SIS_VB_OUTPUT_DISABLE);
	}

	outSISIDXREG(SISCR, IND_SIS_SCRATCH_REG_CR30, cr30);
	outSISIDXREG(SISCR, IND_SIS_SCRATCH_REG_CR31, cr31);

        outSISIDXREG(SISCR, IND_SIS_SCRATCH_REG_CR33, (sisfb_rate_idx & 0x0F));

	if(ivideo.accel) sisfb_syncaccel();

	SiS_Pr.SiS_UseOEM = sisfb_useoem;
}

static void sisfb_post_setmode(void)
{
	u8 reg;
	BOOLEAN doit = TRUE;
#if 0	/* TW: Wrong: Is not in MMIO space, but in RAM */
	/* Backup mode number to MMIO space */
	if(ivideo.mmio_vbase) {
	  *(volatile u8 *)(((u8*)ivideo.mmio_vbase) + 0x449) = (unsigned char)sisfb_mode_no;
	}
#endif	

	if (ivideo.video_bpp == 8) {
		/* TW: We can't switch off CRT1 on LVDS/Chrontel in 8bpp Modes */
		if ((ivideo.hasVB == HASVB_LVDS) || (ivideo.hasVB == HASVB_LVDS_CHRONTEL)) {
			doit = FALSE;
		}
		/* TW: We can't switch off CRT1 on 301B-DH in 8bpp Modes if using LCD */
		if ( (sishw_ext.Is301BDH) && (ivideo.disp_state & DISPTYPE_LCD) ) {
	        	doit = FALSE;
	        }
	}

	/* TW: We can't switch off CRT1 if bridge is in slave mode */
	if(ivideo.hasVB != HASVB_NONE) {
		inSISIDXREG(SISPART1, 0x00, reg);
		if(sisvga_engine == SIS_300_VGA) {
			if((reg & 0xa0) == 0x20) {
				doit = FALSE;
			}
		}
		if(sisvga_engine == SIS_315_VGA) {
			if((reg & 0x50) == 0x10) {
				doit = FALSE;
			}
		}
	} else sisfb_crt1off = 0;

	inSISIDXREG(SISCR, 0x17, reg);
	if((sisfb_crt1off) && (doit))
		reg &= ~0x80;
	else 	      
		reg |= 0x80;
	outSISIDXREG(SISCR, 0x17, reg);

        andSISIDXREG(SISSR, IND_SIS_RAMDAC_CONTROL, ~0x04);

	if((ivideo.disp_state & DISPTYPE_TV) && (ivideo.hasVB == HASVB_301)) {

	   inSISIDXREG(SISPART4, 0x01, reg);

	   if(reg < 0xB0) {        	/* Set filter for SiS301 */

		switch (ivideo.video_width) {
		   case 320:
			filter_tb = (ivideo.TV_type == TVMODE_NTSC) ? 4 : 12;
			break;
		   case 640:
			filter_tb = (ivideo.TV_type == TVMODE_NTSC) ? 5 : 13;
			break;
		   case 720:
			filter_tb = (ivideo.TV_type == TVMODE_NTSC) ? 6 : 14;
			break;
		   case 800:
			filter_tb = (ivideo.TV_type == TVMODE_NTSC) ? 7 : 15;
			break;
		   default:
			filter = -1;
			break;
		}

		orSISIDXREG(SISPART1, sisfb_CRT2_write_enable, 0x01);

		if(ivideo.TV_type == TVMODE_NTSC) {

		        andSISIDXREG(SISPART2, 0x3a, 0x1f);

			if (ivideo.TV_plug == TVPLUG_SVIDEO) {

			        andSISIDXREG(SISPART2, 0x30, 0xdf);

			} else if (ivideo.TV_plug == TVPLUG_COMPOSITE) {

			        orSISIDXREG(SISPART2, 0x30, 0x20);

				switch (ivideo.video_width) {
				case 640:
				        outSISIDXREG(SISPART2, 0x35, 0xEB);
					outSISIDXREG(SISPART2, 0x36, 0x04);
					outSISIDXREG(SISPART2, 0x37, 0x25);
					outSISIDXREG(SISPART2, 0x38, 0x18);
					break;
				case 720:
					outSISIDXREG(SISPART2, 0x35, 0xEE);
					outSISIDXREG(SISPART2, 0x36, 0x0C);
					outSISIDXREG(SISPART2, 0x37, 0x22);
					outSISIDXREG(SISPART2, 0x38, 0x08);
					break;
				case 800:
					outSISIDXREG(SISPART2, 0x35, 0xEB);
					outSISIDXREG(SISPART2, 0x36, 0x15);
					outSISIDXREG(SISPART2, 0x37, 0x25);
					outSISIDXREG(SISPART2, 0x38, 0xF6);
					break;
				}
			}

		} else if(ivideo.TV_type == TVMODE_PAL) {

			andSISIDXREG(SISPART2, 0x3A, 0x1F);

			if (ivideo.TV_plug == TVPLUG_SVIDEO) {

			        andSISIDXREG(SISPART2, 0x30, 0xDF);

			} else if (ivideo.TV_plug == TVPLUG_COMPOSITE) {

			        orSISIDXREG(SISPART2, 0x30, 0x20);

				switch (ivideo.video_width) {
				case 640:
					outSISIDXREG(SISPART2, 0x35, 0xF1);
					outSISIDXREG(SISPART2, 0x36, 0xF7);
					outSISIDXREG(SISPART2, 0x37, 0x1F);
					outSISIDXREG(SISPART2, 0x38, 0x32);
					break;
				case 720:
					outSISIDXREG(SISPART2, 0x35, 0xF3);
					outSISIDXREG(SISPART2, 0x36, 0x00);
					outSISIDXREG(SISPART2, 0x37, 0x1D);
					outSISIDXREG(SISPART2, 0x38, 0x20);
					break;
				case 800:
					outSISIDXREG(SISPART2, 0x35, 0xFC);
					outSISIDXREG(SISPART2, 0x36, 0xFB);
					outSISIDXREG(SISPART2, 0x37, 0x14);
					outSISIDXREG(SISPART2, 0x38, 0x2A);
					break;
				}
			}
		}

		if ((filter >= 0) && (filter <=7)) {
			DPRINTK("FilterTable[%d]-%d: %02x %02x %02x %02x\n", filter_tb, filter, 
				sis_TV_filter[filter_tb].filter[filter][0],
				sis_TV_filter[filter_tb].filter[filter][1],
				sis_TV_filter[filter_tb].filter[filter][2],
				sis_TV_filter[filter_tb].filter[filter][3]
			);
			outSISIDXREG(SISPART2, 0x35, (sis_TV_filter[filter_tb].filter[filter][0]));
			outSISIDXREG(SISPART2, 0x36, (sis_TV_filter[filter_tb].filter[filter][1]));
			outSISIDXREG(SISPART2, 0x37, (sis_TV_filter[filter_tb].filter[filter][2]));
			outSISIDXREG(SISPART2, 0x38, (sis_TV_filter[filter_tb].filter[filter][3]));
		}

	     }
	  
	}

}

#ifndef MODULE
int sisfb_setup(char *options)
{
	char *this_opt;
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	sis_fb_info.fontname[0] = '\0';
#endif	

	ivideo.refresh_rate = 0;

        printk(KERN_INFO "sisfb: Options %s\n", options);

	if (!options || !*options)
		return 0;

	while((this_opt = strsep(&options, ",")) != NULL) {

		if (!*this_opt)	continue;

		if (!strncmp(this_opt, "mode:", 5)) {
			sisfb_search_mode(this_opt + 5);
		} else if (!strncmp(this_opt, "vesa:", 5)) {
			sisfb_search_vesamode(simple_strtoul(this_opt + 5, NULL, 0));
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)			
		} else if (!strcmp(this_opt, "inverse")) {
			sisfb_inverse = 1;
			/* fb_invert_cmaps(); */
		} else if (!strncmp(this_opt, "font:", 5)) {
			strcpy(sis_fb_info.fontname, this_opt + 5);
#endif			
		} else if (!strncmp(this_opt, "mode:", 5)) {
			sisfb_search_mode(this_opt + 5);
		} else if (!strncmp(this_opt, "vesa:", 5)) {
			sisfb_search_vesamode(simple_strtoul(this_opt + 5, NULL, 0));
		} else if (!strncmp(this_opt, "vrate:", 6)) {
			ivideo.refresh_rate = simple_strtoul(this_opt + 6, NULL, 0);
		} else if (!strncmp(this_opt, "rate:", 5)) {
			ivideo.refresh_rate = simple_strtoul(this_opt + 5, NULL, 0);
		} else if (!strncmp(this_opt, "off", 3)) {
			sisfb_off = 1;
		} else if (!strncmp(this_opt, "crt1off", 7)) {
			sisfb_crt1off = 1;
		} else if (!strncmp(this_opt, "filter:", 7)) {
			filter = (int)simple_strtoul(this_opt + 7, NULL, 0);
		} else if (!strncmp(this_opt, "forcecrt2type:", 14)) {
			sisfb_search_crt2type(this_opt + 14);
		} else if (!strncmp(this_opt, "forcecrt1:", 10)) {
			sisfb_forcecrt1 = (int)simple_strtoul(this_opt + 10, NULL, 0);
                } else if (!strncmp(this_opt, "tvmode:",7)) {
		        sisfb_search_tvstd(this_opt + 7);
                } else if (!strncmp(this_opt, "tvstandard:",11)) {
			sisfb_search_tvstd(this_opt + 7);
                } else if (!strncmp(this_opt, "mem:",4)) {
		        sisfb_mem = simple_strtoul(this_opt + 4, NULL, 0);
                } else if (!strncmp(this_opt, "dstn", 4)) {
			enable_dstn = 1;
			/* TW: DSTN overrules forcecrt2type */
			sisfb_crt2type = DISPTYPE_LCD;
		} else if (!strncmp(this_opt, "queuemode:", 10)) {
			sisfb_search_queuemode(this_opt + 10);
		} else if (!strncmp(this_opt, "pdc:", 4)) {
		        sisfb_pdc = simple_strtoul(this_opt + 4, NULL, 0);
		        if(sisfb_pdc & ~0x3c) {
			   printk(KERN_INFO "sisfb: Illegal pdc parameter\n");
			   sisfb_pdc = 0;
		        }
		} else if (!strncmp(this_opt, "noaccel", 7)) {
			sisfb_accel = 0;
		} else if (!strncmp(this_opt, "noypan", 6)) {
		        sisfb_ypan = 0;
		} else if (!strncmp(this_opt, "userom:", 7)) {
			sisfb_userom = (int)simple_strtoul(this_opt + 7, NULL, 0);
		} else if (!strncmp(this_opt, "useoem:", 7)) {
			sisfb_useoem = (int)simple_strtoul(this_opt + 7, NULL, 0);
		} else {
			printk(KERN_INFO "sisfb: Invalid option %s\n", this_opt);
		}

		/* TW: Acceleration only with MMIO mode */
		if((sisfb_queuemode != -1) && (sisfb_queuemode != MMIO_CMD)) {
			sisfb_ypan = 0;
			sisfb_accel = 0;
		}
		/* TW: Panning only with acceleration */
		if(sisfb_accel == 0) sisfb_ypan = 0;

	}
	return 0;
}
#endif

static char *sis_find_rom(void)
{
#if defined(__i386__)
        u32  segstart;
        unsigned char *rom_base;
        unsigned char *rom;
        int  stage;
        int  i;
        char sis_rom_sig[] = "Silicon Integrated Systems";
        char *sis_sig_300[4] = {
          "300", "540", "630", "730"
        };
        char *sis_sig_310[7] = {
          "315", "315", "315", "5315", "6325", "6325", "Xabre"
        };
	ushort sis_nums_300[4] = {
	  SIS_300, SIS_540, SIS_630, SIS_730
	};
	unsigned short sis_nums_310[7] = {
	  SIS_315PRO, SIS_315H, SIS_315, SIS_550, SIS_650, SIS_740, SIS_330
	};

        for(segstart=0x000c0000; segstart<0x000f0000; segstart+=0x00001000) {

                stage = 1;

                rom_base = (char *)ioremap(segstart, 0x1000);

                if ((*rom_base == 0x55) && (((*(rom_base + 1)) & 0xff) == 0xaa))
                   stage = 2;

                if (stage != 2) {
                   iounmap(rom_base);
                   continue;
                }


		rom = rom_base + (unsigned short)(*(rom_base + 0x12) | (*(rom_base + 0x13) << 8));
                if(strncmp(sis_rom_sig, rom, strlen(sis_rom_sig)) == 0) {
                    stage = 3;
		}
                if(stage != 3) {
                    iounmap(rom_base);
                    continue;
                }

		rom = rom_base + (unsigned short)(*(rom_base + 0x14) | (*(rom_base + 0x15) << 8));
                for(i = 0;(i < 4) && (stage != 4); i++) {
                    if(strncmp(sis_sig_300[i], rom, strlen(sis_sig_300[i])) == 0) {
                        if(sis_nums_300[i] == ivideo.chip) {
			   stage = 4;
                           break;
			}
                    }
                }
		if(stage != 4) {
                   for(i = 0;(i < 7) && (stage != 4); i++) {
                      if(strncmp(sis_sig_310[i], rom, strlen(sis_sig_310[i])) == 0) {
		          if(sis_nums_310[i] == ivideo.chip) {
                             stage = 4;
                             break;
			  }
                      }
                   }
		}

                if(stage != 4) {
                        iounmap(rom_base);
                        continue;
                }

                return rom_base;
        }
#endif
        return NULL;
}



int __init sisfb_init(void)
{
	struct pci_dev *pdev = NULL;
	struct board *b;
	int pdev_valid = 0;
	u32 reg32;
	u16 reg16;
	u8  reg, reg1;

	/* outb(0x77, 0x80); */  /* What is this? */

#if 0 	
	/* for DOC VB */
	sisfb_set_reg4(0xcf8,0x800000e0);
	reg32 = sisfb_get_reg3(0xcfc);
	reg32 = reg32 | 0x00001000;
	sisfb_set_reg4(0xcfc,reg32);
	}
#endif

	if (sisfb_off)
		return -ENXIO;

	if (enable_dstn)
		SiS_SetEnableDstn(&SiS_Pr);
		
	sisfb_registered = 0;

	memset(&sis_fb_info, 0, sizeof(sis_fb_info));
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	
	memset(&sis_disp, 0, sizeof(sis_disp));
#endif	

	while ((pdev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
		for (b = sisdev_list; b->vendor; b++) {
			if ((b->vendor == pdev->vendor)
			    && (b->device == pdev->device)) {
				pdev_valid = 1;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)				
				strcpy(sis_fb_info.modename, b->name);
#else				
				strcpy(myid, b->name);
#endif				
				ivideo.chip_id = pdev->device;
				pci_read_config_byte(pdev, PCI_REVISION_ID,
				                     &ivideo.revision_id);
				pci_read_config_word(pdev, PCI_COMMAND, &reg16);
				sishw_ext.jChipRevision = ivideo.revision_id;
				sisvga_enabled = reg16 & 0x01;
				ivideo.pcibus = pdev->bus->number;
				ivideo.pcislot = PCI_SLOT(pdev->devfn);
				ivideo.pcifunc = PCI_FUNC(pdev->devfn);
				ivideo.subsysvendor = pdev->subsystem_vendor;
				ivideo.subsysdevice = pdev->subsystem_device;
				break;
			}
		}

		if (pdev_valid)
			break;
	}

	if (!pdev_valid)
		return -ENODEV;

	switch (ivideo.chip_id) {
#ifdef CONFIG_FB_SIS_300
	   case PCI_DEVICE_ID_SI_300:
		ivideo.chip = SIS_300;
		sisvga_engine = SIS_300_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_300 * 2;  /* New X driver uses 2 buffers */
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_300;
		break;
	   case PCI_DEVICE_ID_SI_630_VGA:
		{
			sisfb_set_reg4(0xCF8, 0x80000000);
			reg32 = sisfb_get_reg3(0xCFC);
			if(reg32 == 0x07301039) {
				ivideo.chip = SIS_730;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)				
				strcpy(sis_fb_info.modename, "SIS 730");
#else
				strcpy(myid, "SIS 730");
#endif				
			} else
				ivideo.chip = SIS_630;

			sisvga_engine = SIS_300_VGA;
			sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_300 * 2;
			sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_300;
			break;
		}
	   case PCI_DEVICE_ID_SI_540_VGA:
		ivideo.chip = SIS_540;
		sisvga_engine = SIS_300_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_300 * 2;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_300;
		break;
#endif
#ifdef CONFIG_FB_SIS_315
	   case PCI_DEVICE_ID_SI_315H:
		ivideo.chip = SIS_315H;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315 * 2;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
	   case PCI_DEVICE_ID_SI_315:
		ivideo.chip = SIS_315;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315 * 2;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
	   case PCI_DEVICE_ID_SI_315PRO:
		ivideo.chip = SIS_315PRO;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315 * 2;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
	   case PCI_DEVICE_ID_SI_550_VGA:
		ivideo.chip = SIS_550;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315 * 2;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
	   case PCI_DEVICE_ID_SI_650_VGA:
	   	{
			ivideo.chip = SIS_650;  
			sisfb_set_reg4(0xCF8, 0x80000000);
			reg32 = sisfb_get_reg3(0xCFC);
			if(reg32 == 0x07401039) {
				ivideo.chip = SIS_740;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)				
				strcpy(sis_fb_info.modename, "SIS 740");
#else
				strcpy(myid, "SIS 740");				
#endif				
			}
			sisvga_engine = SIS_315_VGA;
			sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315 * 2;
			sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
			break;
		}
	   case PCI_DEVICE_ID_SI_330:
		ivideo.chip = SIS_330;
		sisvga_engine = SIS_315_VGA;
		sisfb_hwcursor_size = HW_CURSOR_AREA_SIZE_315 * 2;
		sisfb_CRT2_write_enable = IND_SIS_CRT2_WRITE_ENABLE_315;
		break;
#endif
           default:
	        return -ENODEV;
	}
	sishw_ext.jChipType = ivideo.chip;

	/* for Debug */
	if( (sishw_ext.jChipType == SIS_315PRO) ||
	    (sishw_ext.jChipType == SIS_315) )
		sishw_ext.jChipType = SIS_315H;

	ivideo.video_base = pci_resource_start(pdev, 0);
	ivideo.mmio_base = pci_resource_start(pdev, 1);
	sishw_ext.ulIOAddress = (unsigned short) ivideo.vga_base =
	    (unsigned short) SiS_Pr.RelIO = pci_resource_start(pdev, 2) + 0x30;

	sisfb_mmio_size =  pci_resource_len(pdev, 1);

	if(!sisvga_enabled) {
		if (pci_enable_device(pdev))   return -EIO;
	}

	SiS_Pr.SiS_Backup70xx = 0xff;
        SiS_Pr.SiS_CHOverScan = -1;
        SiS_Pr.SiS_ChSW = FALSE;
	SiS_Pr.SiS_UseLCDA = FALSE;
	SiS_Pr.UsePanelScaler = -1;
	SiSRegInit(&SiS_Pr, (USHORT)sishw_ext.ulIOAddress);

#ifdef CONFIG_FB_SIS_300
	/* TW: Find PCI systems for Chrontel/ISA bridge manipulation */
	if(ivideo.chip == SIS_630) {
	  int i=0;
          do {
	    if(mychswtable[i].subsysVendor == ivideo.subsysvendor &&
	       mychswtable[i].subsysCard   == ivideo.subsysdevice) {
		SiS_Pr.SiS_ChSW = TRUE;
            }
            i++;
          } while(mychswtable[i].subsysVendor != 0);
	}
#endif

        outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)		
#ifdef MODULE	
	inSISIDXREG(SISCR,0x34,reg);
	if(reg & 0x80) {
	   if((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF) {
	      printk(KERN_INFO "sisfb: Cannot initialize display mode, X server is active\n");
	      return -EBUSY;
	   }
	}
#endif	
#endif

#ifdef LINUXBIOS  /* -------------------------------- */

#ifdef CONFIG_FB_SIS_300
	if (sisvga_engine == SIS_300_VGA) {
	        outSISIDXREG(SISSR, 0x28, 0x37);

                outSISIDXREG(SISSR, 0x29, 0x61);

                orSISIDXREG(SISSR, IND_SIS_SCRATCH_REG_1A, SIS_SCRATCH_REG_1A_MASK);
	}
#endif
#ifdef CONFIG_FB_SIS_315
	if (ivideo.chip == SIS_550 || ivideo.chip == SIS_650 || ivideo.chip == SIS_740) {
	        outSISIDXREG(SISSR, 0x28, 0x5a);

                outSISIDXREG(SISSR, 0x29, 0x64);

                outSISIDXREG(SISCR, 0x3a, 0x00);
	}
#endif		

#endif /* LinuxBIOS  -------------------------------- */

	if (sisvga_engine == SIS_315_VGA) {
		switch (ivideo.chip) {
		   case SIS_315H:
		   case SIS_315:
		   case SIS_330:
			sishw_ext.bIntegratedMMEnabled = TRUE;
			break;
		   case SIS_550:
		   case SIS_650:
		   case SIS_740:
			sishw_ext.bIntegratedMMEnabled = TRUE;
			break;
		   default:
			break;
		}
	} else if (sisvga_engine == SIS_300_VGA) {
		if (ivideo.chip == SIS_300) {
			sishw_ext.bIntegratedMMEnabled = TRUE;
		} else {
		        inSISIDXREG(SISSR, IND_SIS_SCRATCH_REG_1A, reg);
			if (reg & SIS_SCRATCH_REG_1A_MASK)
				sishw_ext.bIntegratedMMEnabled = TRUE;
			else
				sishw_ext.bIntegratedMMEnabled = FALSE;
		}
	}

	sishw_ext.pDevice = NULL;
	if(sisfb_userom) {
	    sishw_ext.pjVirtualRomBase = sis_find_rom();
	    if(sishw_ext.pjVirtualRomBase) {
		printk(KERN_INFO "sisfb: Video ROM found and mapped to %p\n",
		        sishw_ext.pjVirtualRomBase);
		sishw_ext.UseROM = TRUE;
	    } else {
	        sishw_ext.UseROM = FALSE;
	        printk(KERN_INFO "sisfb: Video ROM not found\n");
	    }
	} else {
	    sishw_ext.pjVirtualRomBase = NULL;
	    sishw_ext.UseROM = FALSE;
	    printk(KERN_INFO "sisfb: Video ROM usage disabled\n");
	}
	sishw_ext.pjCustomizedROMImage = NULL;
	sishw_ext.bSkipDramSizing = 0;
	sishw_ext.pQueryVGAConfigSpace = &sisfb_query_VGA_config_space;
	sishw_ext.pQueryNorthBridgeSpace = &sisfb_query_north_bridge_space;
	strcpy(sishw_ext.szVBIOSVer, "0.84");

	/* TW: Mode numbers for 1280x960 are different for 300 and 310/325 series */
	if(sisvga_engine == SIS_300_VGA) {
		sisbios_mode[MODEINDEX_1280x960].mode_no = 0x6e;
		sisbios_mode[MODEINDEX_1280x960+1].mode_no = 0x6f;
		sisbios_mode[MODEINDEX_1280x960+2].mode_no = 0x7b;
		sisbios_mode[MODEINDEX_1280x960+3].mode_no = 0x7b;
	}

	sishw_ext.pSR = vmalloc(sizeof(SIS_DSReg) * SR_BUFFER_SIZE);
	if (sishw_ext.pSR == NULL) {
		printk(KERN_ERR "sisfb: Fatal error: Allocating SRReg space failed.\n");
		return -ENODEV;
	}
	sishw_ext.pSR[0].jIdx = sishw_ext.pSR[0].jVal = 0xFF;

	sishw_ext.pCR = vmalloc(sizeof(SIS_DSReg) * CR_BUFFER_SIZE);
	if (sishw_ext.pCR == NULL) {
	        vfree(sishw_ext.pSR);
		printk(KERN_ERR "sisfb: Fatal error: Allocating CRReg space failed.\n");
		return -ENODEV;
	}
	sishw_ext.pCR[0].jIdx = sishw_ext.pCR[0].jVal = 0xFF;

#ifdef CONFIG_FB_SIS_300
	if(sisvga_engine == SIS_300_VGA) {
		if(!sisvga_enabled) {
		        /* Mapping Max FB Size for 300 Init */
			sishw_ext.pjVideoMemoryAddress
				= ioremap(ivideo.video_base, 0x4000000);
			if((sisfb_mode_idx < 0) || ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF)) {
#ifdef LINUXBIOS		/* TW: SiSInit now for LinuxBIOS only */	        
				SiSInit(&SiS_Pr, &sishw_ext);
#endif
				outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);
			}
		}
#ifdef LINUXBIOS
		else {
			sishw_ext.pjVideoMemoryAddress
				= ioremap(ivideo.video_base, 0x4000000);
			if((sisfb_mode_idx < 0) || ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF)) {
				SiSInit(&SiS_Pr, &sishw_ext);
				outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);
			}
		}
		if((sisfb_mode_idx < 0) || ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF)) { 
		        orSISIDXREG(SISSR, 0x07, 0x10);
		}
#endif
		if(sisfb_get_dram_size_300()) {
		        vfree(sishw_ext.pSR);
			vfree(sishw_ext.pCR);
			printk(KERN_ERR "sisfb: Fatal error: Unable to determine RAM size\n");
			return -ENODEV;
		}
	}
#endif

#ifdef CONFIG_FB_SIS_315
	if (sisvga_engine == SIS_315_VGA) {
		if (!sisvga_enabled) {
			/* Mapping Max FB Size for 315 Init */
			sishw_ext.pjVideoMemoryAddress 
				= ioremap(ivideo.video_base, 0x8000000);
			if((sisfb_mode_idx < 0) || ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF)) { 
#ifdef LINUXBIOS
			        /* TW: SISInit is now for LINUXBIOS only */
				SiSInit(&SiS_Pr, &sishw_ext);
#endif

				outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

				sishw_ext.bSkipDramSizing = TRUE;
				sishw_ext.pSR[0].jIdx = 0x13;
				sishw_ext.pSR[1].jIdx = 0x14;
				sishw_ext.pSR[2].jIdx = 0xFF;
				inSISIDXREG(SISSR, 0x13, sishw_ext.pSR[0].jVal);
				inSISIDXREG(SISSR, 0x14, sishw_ext.pSR[1].jVal);
				sishw_ext.pSR[2].jVal = 0xFF;
			}
		}
#ifdef LINUXBIOS
		else {
			sishw_ext.pjVideoMemoryAddress
				= ioremap(ivideo.video_base, 0x8000000);
			if((sisfb_mode_idx < 0) || ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF)) { 

				SiSInit(&SiS_Pr, &sishw_ext);

				outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

				sishw_ext.bSkipDramSizing = TRUE;
				sishw_ext.pSR[0].jIdx = 0x13;
				sishw_ext.pSR[1].jIdx = 0x14;
				sishw_ext.pSR[2].jIdx = 0xFF;
				inSISIDXREG(SISSR, 0x13, sishw_ext.pSR[0].jVal);
				inSISIDXREG(SISSR, 0x14, sishw_ext.pSR[1].jVal);
				sishw_ext.pSR[2].jVal = 0xFF;
			}
		}
#endif
		if (sisfb_get_dram_size_315()) {
			vfree(sishw_ext.pSR);
			vfree(sishw_ext.pCR);
			printk(KERN_INFO "sisfb: Fatal error: Unable to determine RAM size.\n");
			return -ENODEV;
		}
	}
#endif

	if((sisfb_mode_idx < 0) || ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF)) { 

	        /* Enable PCI_LINEAR_ADDRESSING and MMIO_ENABLE  */
	        orSISIDXREG(SISSR, IND_SIS_PCI_ADDRESS_SET, (SIS_PCI_ADDR_ENABLE | SIS_MEM_MAP_IO_ENABLE));

                /* Enable 2D accelerator engine */
	        orSISIDXREG(SISSR, IND_SIS_MODULE_ENABLE, SIS_ENABLE_2D);

	}

	sishw_ext.ulVideoMemorySize = ivideo.video_size;

	if(sisfb_pdc) {
	    sishw_ext.pdc = sisfb_pdc;
	} else {
	    sishw_ext.pdc = 0;
	}

	if (!request_mem_region(ivideo.video_base, ivideo.video_size, "sisfb FB")) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to reserve frame buffer memory\n");
		printk(KERN_ERR "sisfb: Is there another framebuffer driver active?\n");
		vfree(sishw_ext.pSR);
		vfree(sishw_ext.pCR);
		return -ENODEV;
	}

	if (!request_mem_region(ivideo.mmio_base, sisfb_mmio_size, "sisfb MMIO")) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to reserve MMIO region\n");
		release_mem_region(ivideo.video_base, ivideo.video_size);
		vfree(sishw_ext.pSR);
		vfree(sishw_ext.pCR);
		return -ENODEV;
	}

	ivideo.video_vbase = sishw_ext.pjVideoMemoryAddress = 
		ioremap(ivideo.video_base, ivideo.video_size);
	ivideo.mmio_vbase = ioremap(ivideo.mmio_base, sisfb_mmio_size);

	printk(KERN_INFO "sisfb: Framebuffer at 0x%lx, mapped to 0x%p, size %dk\n",
	       ivideo.video_base, ivideo.video_vbase,
	       ivideo.video_size / 1024);

	printk(KERN_INFO "sisfb: MMIO at 0x%lx, mapped to 0x%p, size %ldk\n",
	       ivideo.mmio_base, ivideo.mmio_vbase,
	       sisfb_mmio_size / 1024);

	if(sisfb_heap_init()) {
		printk(KERN_WARNING "sisfb: Failed to initialize offscreen memory heap\n");
	}

	ivideo.mtrr = (unsigned int) 0;

	if((sisfb_mode_idx < 0) || ((sisbios_mode[sisfb_mode_idx].mode_no) != 0xFF)) { 

#ifdef CONFIG_FB_SIS_300
		if (sisvga_engine == SIS_300_VGA) {
			sisfb_get_VB_type_300();
		}
#endif

#ifdef CONFIG_FB_SIS_315
		if (sisvga_engine == SIS_315_VGA) {
			sisfb_get_VB_type_315();
		}
#endif

		sishw_ext.ujVBChipID = VB_CHIP_UNKNOWN;
		sishw_ext.Is301BDH = FALSE;
		sishw_ext.usExternalChip = 0;

		switch (ivideo.hasVB) {

		case HASVB_301:
		        inSISIDXREG(SISPART4, 0x01, reg);
			if (reg >= 0xE0) {
				sishw_ext.ujVBChipID = VB_CHIP_302LV;
				printk(KERN_INFO "sisfb: SiS302LV bridge detected (revision 0x%02x)\n",reg);
	  		} else if (reg >= 0xD0) {
				sishw_ext.ujVBChipID = VB_CHIP_301LV;
				printk(KERN_INFO "sisfb: SiS301LV bridge detected (revision 0x%02x)\n",reg);
	  		} else if (reg >= 0xB0) {
				sishw_ext.ujVBChipID = VB_CHIP_301B;
				inSISIDXREG(SISPART4,0x23,reg1);
				if(!(reg1 & 0x02)) sishw_ext.Is301BDH = TRUE;
				printk(KERN_INFO "sisfb: SiS301B%s bridge detected (revision 0x%02x)\n",
					(sishw_ext.Is301BDH ? "-DH" : ""), reg);
			} else {
				sishw_ext.ujVBChipID = VB_CHIP_301;
				printk(KERN_INFO "sisfb: SiS301 bridge detected\n");
			}
			break;
		case HASVB_302:
		        inSISIDXREG(SISPART4, 0x01, reg);
			if (reg >= 0xE0) {
				sishw_ext.ujVBChipID = VB_CHIP_302LV;
				printk(KERN_INFO "sisfb: SiS302LV bridge detected (revision 0x%02x)\n",reg);
	  		} else if (reg >= 0xD0) {
				sishw_ext.ujVBChipID = VB_CHIP_301LV;
				printk(KERN_INFO "sisfb: SiS302LV bridge detected (revision 0x%02x)\n",reg);
	  		} else if (reg >= 0xB0) {
				inSISIDXREG(SISPART4,0x23,reg1);
				if(!(reg1 & 0x02)) sishw_ext.Is301BDH = TRUE;
			        sishw_ext.ujVBChipID = VB_CHIP_302B;
				printk(KERN_INFO "sisfb: SiS302B%s bridge detected (revision 0x%02x)\n",
					(sishw_ext.Is301BDH ? "-DH" : ""), reg);
			} else {
			        sishw_ext.ujVBChipID = VB_CHIP_302;
				printk(KERN_INFO "sisfb: SiS302 bridge detected\n");
			}
			break;
		case HASVB_LVDS:
			sishw_ext.usExternalChip = 0x1;
			printk(KERN_INFO "sisfb: LVDS transmitter detected\n");
			break;
		case HASVB_TRUMPION:
			sishw_ext.usExternalChip = 0x2;
			printk(KERN_INFO "sisfb: Trumpion Zurac LVDS scaler detected\n");
			break;
		case HASVB_CHRONTEL:
			sishw_ext.usExternalChip = 0x4;
			printk(KERN_INFO "sisfb: Chrontel TV encoder detected\n");
			break;
		case HASVB_LVDS_CHRONTEL:
			sishw_ext.usExternalChip = 0x5;
			printk(KERN_INFO "sisfb: LVDS transmitter and Chrontel TV encoder detected\n");
			break;
		default:
			printk(KERN_INFO "sisfb: No or unknown bridge type detected\n");
			break;
		}

		if (ivideo.hasVB != HASVB_NONE) {
#ifdef CONFIG_FB_SIS_300
		      if (sisvga_engine == SIS_300_VGA) {
				sisfb_detect_VB_connect_300();
                      }
#endif
#ifdef CONFIG_FB_SIS_315
		      if (sisvga_engine == SIS_315_VGA) {
				sisfb_detect_VB_connect_315();
                      }
#endif
		}

		if (ivideo.disp_state & DISPTYPE_DISP2) {
			if (sisfb_crt1off)
				ivideo.disp_state |= DISPMODE_SINGLE;
			else
				ivideo.disp_state |= (DISPMODE_MIRROR | DISPTYPE_CRT1);
		} else {
			ivideo.disp_state = DISPMODE_SINGLE | DISPTYPE_CRT1;
		}

		if (ivideo.disp_state & DISPTYPE_LCD) {
		    if (!enable_dstn) {
		        inSISIDXREG(SISCR, IND_SIS_LCD_PANEL, reg);
			reg &= 0x0f;
			if (sisvga_engine == SIS_300_VGA) {
			    sishw_ext.ulCRT2LCDType = sis300paneltype[reg];
			} else {
			    sishw_ext.ulCRT2LCDType = sis310paneltype[reg];
			}
		    } else {
		        /* TW: FSTN/DSTN */
			sishw_ext.ulCRT2LCDType = LCD_320x480;
		    }
		}
		
		sisfb_detectedpdc = 0;
#ifndef LINUXBIOS
#ifdef CONFIG_FB_SIS_300
                /* TW: Save the current PanelDelayCompensation if the LCD is currently used */
		if(sisvga_engine == SIS_300_VGA) {
	          if((sishw_ext.usExternalChip == 0x01) ||   /* LVDS */
		     (sishw_ext.usExternalChip == 0x05) ||   /* LVDS+Chrontel */
		     (sishw_ext.Is301BDH)) {		     /* 301B-DH */
		       int tmp;
		       inSISIDXREG(SISCR,0x30,tmp);
		       if(tmp & 0x20) {
		          /* Currently on LCD? If yes, read current pdc */
		          inSISIDXREG(SISPART1,0x13,sisfb_detectedpdc);
			  sisfb_detectedpdc &= 0x3c;
			  if(sishw_ext.pdc == 0) {
			     /* Let option override detection */
			     sishw_ext.pdc = sisfb_detectedpdc;
			  }
			  printk(KERN_INFO
			         "sisfb: Detected LCD PanelDelayCompensation %d\n",
  			         sisfb_detectedpdc);
		       }
		       if((sishw_ext.pdc) && (sishw_ext.pdc != sisfb_detectedpdc)) {
		          printk(KERN_INFO
			         "sisfb: Using LCD PanelDelayCompensation %d\n",
				 sishw_ext.pdc);
		       }
	          }
		}
#endif
#endif
	        sisfb_detectedlcda = 0xff;
#ifndef LINUXBIOS
#ifdef CONFIG_FB_SIS_315
                /* TW: Try to find about LCDA */
		if(sisvga_engine == SIS_315_VGA) {
	          if((sishw_ext.ujVBChipID == VB_CHIP_302B) ||
		     (sishw_ext.ujVBChipID == VB_CHIP_301LV) ||
		     (sishw_ext.ujVBChipID == VB_CHIP_302LV)) {
		       int tmp;
		       inSISIDXREG(SISCR,0x34,tmp);
		       if(tmp <= 0x13) {	
		          /* Currently on LCDA? (Some BIOSes leave CR38) */
		          inSISIDXREG(SISCR,0x38,tmp);
			  if((tmp & 0x03) == 0x03) {
			     SiS_Pr.SiS_UseLCDA = TRUE;
			  } else {
			     /* Currently on LCDA? (Some newer BIOSes set D0 in CR35) */
			     inSISIDXREG(SISCR,0x35,tmp);
			     if(tmp & 0x01) {
			        SiS_Pr.SiS_UseLCDA = TRUE;
			     } else {
			        /* Currently on LCD? If so, we can find out 
				   by peeking the mode register 
				 */
			        inSISIDXREG(SISCR,0x30,tmp);
			        if(tmp & 0x20) {
			           inSISIDXREG(SISPART1,0x13,tmp);
				   if(tmp & 0x04) {
				      SiS_Pr.SiS_UseLCDA = TRUE;
				   }
			        }
			     }
			  }
		       } 
		       if(SiS_Pr.SiS_UseLCDA) {
		          sisfb_detectedlcda = 0x03;
		          printk(KERN_INFO
			         "sisfb: Bridge uses LCDA for low resolution and text modes\n");
		       }
	          }
		}
#endif
#endif

		if (sisfb_mode_idx >= 0)
			sisfb_mode_idx = sisfb_validate_mode(sisfb_mode_idx);

		if (sisfb_mode_idx < 0) {
			switch (ivideo.disp_state & DISPTYPE_DISP2) {
			   case DISPTYPE_LCD:
				sisfb_mode_idx = DEFAULT_LCDMODE;
				break;
			   case DISPTYPE_TV:
				sisfb_mode_idx = DEFAULT_TVMODE;
				break;
			   default:
				sisfb_mode_idx = DEFAULT_MODE;
				break;
			}
		}

		sisfb_mode_no = sisbios_mode[sisfb_mode_idx].mode_no;

		if (ivideo.refresh_rate != 0)
			sisfb_search_refresh_rate(ivideo.refresh_rate);

		if (sisfb_rate_idx == 0) {
			sisfb_rate_idx = sisbios_mode[sisfb_mode_idx].rate_idx;
			ivideo.refresh_rate = 60;
		}

		ivideo.video_bpp = sisbios_mode[sisfb_mode_idx].bpp;
		ivideo.video_vwidth = ivideo.video_width = sisbios_mode[sisfb_mode_idx].xres;
		ivideo.video_vheight = ivideo.video_height = sisbios_mode[sisfb_mode_idx].yres;
		ivideo.org_x = ivideo.org_y = 0;
		ivideo.video_linelength = ivideo.video_width * (ivideo.video_bpp >> 3);
		switch(ivideo.video_bpp) {
        	case 8:
            		ivideo.DstColor = 0x0000;
	    		ivideo.SiS310_AccelDepth = 0x00000000;
			ivideo.video_cmap_len = 256;
            		break;
        	case 16:
            		ivideo.DstColor = 0x8000;
            		ivideo.SiS310_AccelDepth = 0x00010000;
			ivideo.video_cmap_len = 16;
            		break;
        	case 32:
            		ivideo.DstColor = 0xC000;
	    		ivideo.SiS310_AccelDepth = 0x00020000;
			ivideo.video_cmap_len = 16;
            		break;
		default:
			ivideo.video_cmap_len = 16;
		        printk(KERN_INFO "sisfb: Unsupported depth %d", ivideo.video_bpp);
			break;
    		}
		
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	

		/* ---------------- For 2.4: Now switch the mode ------------------ */		
		
		printk(KERN_INFO "sisfb: Mode is %dx%dx%d (%dHz)\n",
	       		ivideo.video_width, ivideo.video_height, ivideo.video_bpp,
			ivideo.refresh_rate);

		sisfb_pre_setmode();

		if (SiSSetMode(&SiS_Pr, &sishw_ext, sisfb_mode_no) == 0) {
			printk(KERN_ERR "sisfb: Setting mode[0x%x] failed, using default mode\n", 
				sisfb_mode_no);
			return -1;
		}

		outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

		sisfb_post_setmode();

		sisfb_crtc_to_var(&default_var);
		
#else		/* --------- For 2.5: Setup a somewhat sane default var ------------ */

		printk(KERN_INFO "sisfb: Default mode is %dx%dx%d (%dHz)\n",
	       		ivideo.video_width, ivideo.video_height, ivideo.video_bpp,
			ivideo.refresh_rate);
			
		default_var.xres = default_var.xres_virtual = ivideo.video_width;
		default_var.yres = default_var.yres_virtual = ivideo.video_height;
		default_var.bits_per_pixel = ivideo.video_bpp;
		
		sisfb_bpp_to_var(&default_var);
		
		default_var.pixclock = (u32) (1E12 /
				sisfb_mode_rate_to_dclock(&SiS_Pr, &sishw_ext,
						sisfb_mode_no, sisfb_rate_idx));
						
		if(sisfb_mode_rate_to_ddata(&SiS_Pr, &sishw_ext,
			 sisfb_mode_no, sisfb_rate_idx,
			 &default_var.left_margin, &default_var.right_margin, 
			 &default_var.upper_margin, &default_var.lower_margin,
			 &default_var.hsync_len, &default_var.vsync_len,
			 &default_var.sync, &default_var.vmode)) {
			 
		   if((default_var.vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		      default_var.yres <<= 1;
		      default_var.yres_virtual <<= 1;
		   } else if((default_var.vmode	& FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		      default_var.pixclock >>= 1;
		      default_var.yres >>= 1;
		      default_var.yres_virtual >>= 1;
		   }
		   
	        }
#ifdef SISFB_PAN
		if(sisfb_ypan) {
	    		default_var.yres_virtual = 
				ivideo.heapstart / (default_var.xres * (default_var.bits_per_pixel >> 3));
	    		if(default_var.yres_virtual <= default_var.yres) {
	        		default_var.yres_virtual = default_var.yres;
	    		}
		} 
#endif
		
#endif

		ivideo.accel = 0;
		if(sisfb_accel) {
		   ivideo.accel = -1;
		   default_var.accel_flags |= FB_ACCELF_TEXT;
		   sisfb_initaccel();
		}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)		/* ---- 2.4 series init ---- */
		sis_fb_info.node = -1;
		sis_fb_info.flags = FBINFO_FLAG_DEFAULT;
		sis_fb_info.blank = &sisfb_blank;
		sis_fb_info.fbops = &sisfb_ops;
		sis_fb_info.switch_con = &sisfb_switch;
		sis_fb_info.updatevar = &sisfb_update_var;
		sis_fb_info.changevar = NULL;
		sis_fb_info.disp = &sis_disp;
			
		sisfb_set_disp(-1, &default_var, &sis_fb_info);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)		/* ---- 2.5 series init ---- */
		sis_fb_info.flags = FBINFO_FLAG_DEFAULT;
		sis_fb_info.var = default_var;
		sis_fb_info.fix = sisfb_fix;
		sis_fb_info.par = &ivideo;
		sis_fb_info.screen_base = ivideo.video_vbase;
		sis_fb_info.fbops = &sisfb_ops;
		sisfb_get_fix(&sis_fb_info.fix, -1, &sis_fb_info);
		sis_fb_info.pseudo_palette = pseudo_palette;
		
		fb_alloc_cmap(&sis_fb_info.cmap, 256 , 0);
#endif

#ifdef CONFIG_MTRR
		ivideo.mtrr = mtrr_add((unsigned int) ivideo.video_base,
				(unsigned int) ivideo.video_size,
				MTRR_TYPE_WRCOMB, 1);
		if(ivideo.mtrr) {
			printk(KERN_INFO "sisfb: Added MTRRs\n");
		}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
		vc_resize_con(1, 1, 0);
#endif

		TWDEBUG("Before calling register_framebuffer");
		
		if(register_framebuffer(&sis_fb_info) < 0)
			return -EINVAL;
			
		sisfb_registered = 1;			

		printk(KERN_INFO "sisfb: Installed SISFB_GET_INFO ioctl (%x)\n", SISFB_GET_INFO);
		
		printk(KERN_INFO "sisfb: 2D acceleration is %s, scrolling mode %s\n",
		     sisfb_accel ? "enabled" : "disabled",
		     sisfb_ypan  ? "ypan" : "redraw");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
		printk(KERN_INFO "fb%d: %s frame buffer device, Version %d.%d.%02d\n",
	       		GET_FB_IDX(sis_fb_info.node), sis_fb_info.modename, VER_MAJOR, VER_MINOR,
	       		VER_LEVEL);		     
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
		printk(KERN_INFO "fb%d: %s frame buffer device, Version %d.%d.%02d\n",
	       		sis_fb_info.node, myid, VER_MAJOR, VER_MINOR, VER_LEVEL);			     
#endif

	}	/* TW: if mode = "none" */
	return 0;
}


#ifdef MODULE

static char         *mode = NULL;
static int          vesa = -1;
static unsigned int rate = 0;
static unsigned int crt1off = 1;
static unsigned int mem = 0;
static unsigned int dstn = 0;
static char         *forcecrt2type = NULL;
static int          forcecrt1 = -1;
static char         *queuemode = NULL;
static int          pdc = 0;
static int          noaccel = -1;
static int          noypan  = -1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static int          inverse = 0;
#endif
static int          userom = 1;
static int          useoem = -1;
static char         *tvstandard = NULL;

MODULE_DESCRIPTION("SiS 300/540/630/730/315/550/650/740/330 framebuffer driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("SiS; Thomas Winischhofer <thomas@winischhofer.net>; Various others");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
MODULE_PARM(mode, "s");
MODULE_PARM_DESC(mode,
       "\nSelects the desired display mode in the format [X]x[Y]x[Depth], eg.\n"
         "800x600x16 (default: none if sisfb is a module; this leaves the\n"
	 "console untouched and the driver will only do the video memory\n"
	 "management for eg. DRM/DRI; 800x600x8 if sisfb is in the kernel)");
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)	 
MODULE_PARM(mode, "s");
MODULE_PARM_DESC(mode,
       "\nSelects the desired default display mode in the format [X]x[Y]x[Depth],\n"
         "eg. 1024x768x16 (default: 800x600x8)");
#endif	 

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
MODULE_PARM(vesa, "i");
MODULE_PARM_DESC(vesa,
       "\nSelects the desired display mode by VESA defined mode number, eg. 0x117\n"
         "(default: 0x0000 if sisfb is a module; this leaves the console untouched\n"
	 "and the driver will only do the video memory management for eg. DRM/DRI;\n"
	 "0x0103 if sisfb is in the kernel)");
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)	 
MODULE_PARM(vesa, "i");
MODULE_PARM_DESC(vesa,
       "\nSelects the desired default display mode by VESA defined mode number, eg.\n"
         "0x117 (default: 0x0103)");
#endif	 

MODULE_PARM(rate, "i");
MODULE_PARM_DESC(rate,
	"\nSelects the desired vertical refresh rate for CRT1 (external VGA) in Hz.\n"
	"(default: 60)");

MODULE_PARM(crt1off,   "i");
MODULE_PARM_DESC(crt1off,
	"(Deprecated, please use forcecrt1)");

MODULE_PARM(filter, "i");
MODULE_PARM_DESC(filter,
	"\nSelects TV flicker filter type (only for systems with a SiS301 video bridge).\n"
	  "(Possible values 0-7, default: [no filter])");

MODULE_PARM(dstn,   "i");
MODULE_PARM_DESC(dstn,
	"\nSelects DSTN/FSTN display mode for SiS550. This sets CRT2 type to LCD and\n"
	  "overrides forcecrt2type setting. (1=ON, 0=OFF) (default: 0)");

MODULE_PARM(queuemode,   "s");
MODULE_PARM_DESC(queuemode,
	"\nSelects the queue mode on 315/550/650/740/330. Possible choices are AGP, VRAM or\n"
  	  "MMIO. AGP is only available if the kernel has AGP support. The queue mode is\n"
	  "important to programs using the 2D/3D accelerator of the SiS chip. The modes\n"
	  "require a totally different way of programming the engines. If any mode than\n"
	  "MMIO is selected, sisfb will disable its own 2D acceleration. On\n"
	  "300/540/630/730, this option is ignored. (default: MMIO)");

/* TW: "Import" the options from the X driver */
MODULE_PARM(mem,    "i");
MODULE_PARM_DESC(mem,
	"\nDetermines the beginning of the video memory heap in KB. This heap is used\n"
	  "for video RAM management for eg. DRM/DRI. The default depends on the amount\n"
	  "of video RAM available. If 8MB of video RAM or less is available, the heap\n"
	  "starts at 4096KB, if between 8 and 16MB are available at 8192KB, otherwise\n"
	  "at 12288KB. The value is to be specified without 'KB' and should match\n"
	  "the MaxXFBMem setting for XFree 4.x (x>=2).");

MODULE_PARM(forcecrt2type, "s");
MODULE_PARM_DESC(forcecrt2type,
	"\nIf this option is omitted, the driver autodetects CRT2 output devices, such as\n"
	  "LCD, TV or secondary VGA. With this option, this autodetection can be\n"
	  "overridden. Possible parameters are LCD, TV, VGA or NONE. NONE disables CRT2.\n"
	  "On systems with a 301(B/LV) bridge, parameters SVIDEO, COMPOSITE or SCART can be\n"
	  "used instead of TV to override the TV detection. (default: [autodetected])");

MODULE_PARM(forcecrt1, "i");
MODULE_PARM_DESC(forcecrt1,
	"\nNormally, the driver autodetects whether or not CRT1 (external VGA) is \n"
	  "connected. With this option, the detection can be overridden (1=CRT1 ON,\n"
	  " 0=CRT1 off) (default: [autodetected])");

MODULE_PARM(pdc, "i");
MODULE_PARM_DESC(pdc,
        "\n(300 series only) This is for manually selecting the LCD panel delay\n"
	  "compensation. The driver should detect this correctly in most cases; however,\n"
	  "sometimes this is not possible. If you see 'small waves' on the LCD, try\n"
	  "setting this to 4, 32 or 24. If the problem persists, try other values\n"
	  "between 4 and 60 in steps of 4. (default: [autodetected])");

MODULE_PARM(noaccel, "i");
MODULE_PARM_DESC(noaccel,
        "\nIf set to anything other than 0, 2D acceleration and y-panning will be\n"
	"disabled. (default: 0)");

MODULE_PARM(noypan, "i");
MODULE_PARM_DESC(noypan,
        "\nIf set to anything other than 0, y-panning will be disabled and scrolling\n"
	"will be performed by redrawing the screen. This required 2D acceleration, so\n"
	"if the option noaccel is set, y-panning will be disabled. (default: 0)");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	
MODULE_PARM(inverse, "i");
MODULE_PARM_DESC(inverse,
        "\nSetting this to anything but 0 should invert the display colors, but this\n"
	"does not seem to work. (default: 0)");
#endif	

MODULE_PARM(userom, "i");
MODULE_PARM_DESC(userom,
        "\nSetting this to 0 keeps sisfb from using the video BIOS data which is needed\n"
	"for some LCD and TV setup. (default: 1)");

MODULE_PARM(useoem, "i");
MODULE_PARM_DESC(useoem,
        "\nSetting this to 0 keeps sisfb from using its internel OEM data for some LCD\n"
	"panels and TV connector types. (default: auto)");

MODULE_PARM(tvstandard, "s");
MODULE_PARM_DESC(tvstandard,
	"\nThis allows overriding the BIOS default for the TV standard. Valid choices are\n"
	"pal and ntsc. (default: auto)");

int init_module(void)
{
	int err;
	
	if(mode)
		sisfb_search_mode(mode);
	else if(vesa != -1)
		sisfb_search_vesamode(vesa);
	else  
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	
		/* For 2.4, set mode=none if no mode is given  */
		sisfb_mode_idx = MODE_INDEX_NONE;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
		/* For 2.5, we don't need this "mode=none" stuff anymore */	
		sisfb_mode_idx = DEFAULT_MODE;
#endif

	ivideo.refresh_rate = rate;

	if(forcecrt2type)
		sisfb_search_crt2type(forcecrt2type);

	if(tvstandard)
		sisfb_search_tvstd(tvstandard);

	if(crt1off == 0)
		sisfb_crt1off = 1;
	else
		sisfb_crt1off = 0;

	sisfb_forcecrt1 = forcecrt1;
	if(forcecrt1 == 1)
		sisfb_crt1off = 0;
	else if(forcecrt1 == 0)
		sisfb_crt1off = 1;

	if(noaccel == 1)      sisfb_accel = 0;
	else if(noaccel == 0) sisfb_accel = 1;

	if(noypan == 1)       sisfb_ypan = 0;
	else if(noypan == 0)  sisfb_ypan = 1;

	/* TW: Panning only with acceleration */
	if(sisfb_accel == 0)  sisfb_ypan = 0;
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	if(inverse)           sisfb_inverse = 1;
#endif

	if(mem)		      sisfb_mem = mem;

	sisfb_userom = userom;

	sisfb_useoem = useoem;

	enable_dstn = dstn;

	/* TW: DSTN overrules forcecrt2type */
	if (enable_dstn)      sisfb_crt2type = DISPTYPE_LCD;

	if (queuemode)        sisfb_search_queuemode(queuemode);
	
	/* TW: If other queuemode than MMIO, disable 2D accel and ypan */
	if((sisfb_queuemode != -1) && (sisfb_queuemode != MMIO_CMD)) {
	        sisfb_accel = 0;
		sisfb_ypan = 0;
	}

        if(pdc) {
	   if(!(pdc & ~0x3c)) {
	        sisfb_pdc = pdc & 0x3c;
	   }
	}

	if((err = sisfb_init()) < 0) return err;

	return 0;
}

void cleanup_module(void)
{
	/* TW: Release mem regions */
	release_mem_region(ivideo.video_base, ivideo.video_size);
	release_mem_region(ivideo.mmio_base, sisfb_mmio_size);
	
#ifdef CONFIG_MTRR
	/* TW: Release MTRR region */
	if(ivideo.mtrr) {
		mtrr_del(ivideo.mtrr,
		      (unsigned int)ivideo.video_base,
	              (unsigned int)ivideo.video_size);
	}
#endif

	/* Unregister the framebuffer */
	if(sisfb_registered) {
		unregister_framebuffer(&sis_fb_info);
	}
	
	if(sishw_ext.pSR) vfree(sishw_ext.pSR);
	if(sishw_ext.pCR) vfree(sishw_ext.pCR);
	
	/* TODO: Restore the initial mode */
	
	printk(KERN_INFO "sisfb: Module unloaded\n");
}

#endif

EXPORT_SYMBOL(sis_malloc);
EXPORT_SYMBOL(sis_free);
EXPORT_SYMBOL(sis_dispinfo);

EXPORT_SYMBOL(ivideo);
                                                                                           
