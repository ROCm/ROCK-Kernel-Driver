/*
 * sound/opl3sa2.c
 *
 * A low level driver for Yamaha OPL3-SA2 and SA3 cards.
 * NOTE: All traces of the name OPL3-SAx have now (December 2000) been
 *       removed from the driver code, as an email exchange with Yamaha
 *       provided the information that the YMF-719 is indeed just a
 *       re-badged 715.
 *
 * Copyright 1998-2001 Scott Murray <scott@spiteful.org>
 *
 * Originally based on the CS4232 driver (in cs4232.c) by Hannu Savolainen
 * and others.  Now incorporates code/ideas from pss.c, also by Hannu
 * Savolainen.  Both of those files are distributed with the following
 * license:
 *
 * "Copyright (C) by Hannu Savolainen 1993-1997
 *
 *  OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 *  Version 2 (June 1991). See the "COPYING" file distributed with this software
 *  for more info."
 *
 * As such, in accordance with the above license, this file, opl3sa2.c, is
 * distributed under the GNU GENERAL PUBLIC LICENSE (GPL) Version 2 (June 1991).
 * See the "COPYING" file distributed with this software for more information.
 *
 * Change History
 * --------------
 * Scott Murray            Original driver (Jun 14, 1998)
 * Paul J.Y. Lahaie        Changed probing / attach code order
 * Scott Murray            Added mixer support (Dec 03, 1998)
 * Scott Murray            Changed detection code to be more forgiving,
 *                         added force option as last resort,
 *                         fixed ioctl return values. (Dec 30, 1998)
 * Scott Murray            Simpler detection code should work all the time now
 *                         (with thanks to Ben Hutchings for the heuristic),
 *                         removed now unnecessary force option. (Jan 5, 1999)
 * Christoph Hellwig	   Adapted to module_init/module_exit (Mar 4, 2000)
 * Scott Murray            Reworked SA2 versus SA3 mixer code, updated chipset
 *                         version detection code (again!). (Dec 5, 2000)
 * Scott Murray            Adjusted master volume mixer scaling. (Dec 6, 2000)
 * Scott Murray            Based on a patch by Joel Yliluoma (aka Bisqwit),
 *                         integrated wide mixer and adjusted mic, bass, treble
 *                         scaling. (Dec 6, 2000)
 * Scott Murray            Based on a patch by Peter Englmaier, integrated
 *                         ymode and loopback options. (Dec 6, 2000)
 * Scott Murray            Inspired by a patch by Peter Englmaier, and based on
 *                         what ALSA does, added initialization code for the
 *                         default DMA and IRQ settings. (Dec 6, 2000)
 * Scott Murray            Added some more checks to the card detection code,
 *                         based on what ALSA does. (Dec 12, 2000)
 * Scott Murray            Inspired by similar patches from John Fremlin,
 *                         Jim Radford, Mike Rolig, and Ingmar Steen, added 2.4
 *                         ISA PnP API support, mainly based on bits from
 *                         sb_card.c and awe_wave.c. (Dec 12, 2000)
 * Scott Murray            Some small cleanups to the init code output.
 *                         (Jan 7, 2001)
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/isapnp.h>

#include "sound_config.h"

#include "ad1848.h"
#include "mpu401.h"

/* Useful control port indexes: */
#define OPL3SA2_SYS_CTRL     0x02
#define OPL3SA2_IRQ_CONFIG   0x03
#define OPL3SA2_DMA_CONFIG   0x06
#define OPL3SA2_MASTER_LEFT  0x07
#define OPL3SA2_MASTER_RIGHT 0x08
#define OPL3SA2_MIC          0x09
#define OPL3SA2_MISC         0x0A

#define OPL3SA3_WIDE         0x14
#define OPL3SA3_BASS         0x15
#define OPL3SA3_TREBLE       0x16

/* Useful constants: */
#define DEFAULT_VOLUME 50
#define DEFAULT_MIC    50
#define DEFAULT_TIMBRE 0

/* For checking against what the card returns: */
#define VERSION_UNKNOWN 0
#define VERSION_YMF711  1
#define VERSION_YMF715  2
#define VERSION_YMF715B 3
#define VERSION_YMF715E 4
/* also assuming that anything > 4 but <= 7 is a 715E */

/* Chipset type constants for use below */
#define CHIPSET_UNKNOWN -1
#define CHIPSET_OPL3SA2 0
#define CHIPSET_OPL3SA3 1

#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE
#define OPL3SA2_CARDS_MAX 4
#else
#define OPL3SA2_CARDS_MAX 1
#endif

/* This should be pretty obvious */
static int opl3sa2_cards_num; /* = 0 */

/* What's my version(s)? */
static int chipset[OPL3SA2_CARDS_MAX] = { CHIPSET_UNKNOWN };

/* Oh well, let's just cache the name(s) */
static char chipset_name[OPL3SA2_CARDS_MAX][12];

/* Where's my mixer(s)? */
static int opl3sa2_mixer[OPL3SA2_CARDS_MAX] = { -1 };

/* Bag o' mixer data */
typedef struct opl3sa2_mixerdata_tag {
	unsigned short cfg_port;
	unsigned short padding;
	unsigned int   volume_l;
	unsigned int   volume_r;
	unsigned int   mic;
	unsigned int   bass_l;
	unsigned int   bass_r;
	unsigned int   treble_l;
	unsigned int   treble_r;
	unsigned int   wide_l;
	unsigned int   wide_r;
} opl3sa2_mixerdata;
static opl3sa2_mixerdata opl3sa2_data[OPL3SA2_CARDS_MAX];

static struct address_info cfg[OPL3SA2_CARDS_MAX];
static struct address_info cfg_mss[OPL3SA2_CARDS_MAX];
static struct address_info cfg_mpu[OPL3SA2_CARDS_MAX];

/* Our parameters */
static int __initdata io	= -1;
static int __initdata mss_io	= -1;
static int __initdata mpu_io	= -1;
static int __initdata irq	= -1;
static int __initdata dma	= -1;
static int __initdata dma2	= -1;
static int __initdata ymode	= -1;
static int __initdata loopback	= -1;

#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE
/* PnP specific parameters */
static int __initdata isapnp = 1;
static int __initdata multiple = 1;

/* PnP devices */
struct pci_dev* opl3sa2_dev[OPL3SA2_CARDS_MAX];

/* Whether said devices have been activated */
static int opl3sa2_activated[OPL3SA2_CARDS_MAX];
#else
static int __initdata isapnp; /* = 0 */
static int __initdata multiple; /* = 0 */
#endif

MODULE_DESCRIPTION("Module for OPL3-SA2 and SA3 sound cards (uses AD1848 MSS driver).");
MODULE_AUTHOR("Scott Murray <scott@spiteful.org>");
MODULE_LICENSE("GPL");


MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "Set I/O base of OPL3-SA2 or SA3 card (usually 0x370.  Address must be even and must be from 0x100 to 0xFFE)");

MODULE_PARM(mss_io, "i");
MODULE_PARM_DESC(mss_io, "Set MSS (audio) I/O base (0x530, 0xE80, or other. Address must end in 0 or 4 and must be from 0x530 to 0xF48)");

MODULE_PARM(mpu_io, "i");
MODULE_PARM_DESC(mpu_io, "Set MIDI I/O base (0x330 or other. Address must be even and must be from 0x300 to 0x334)");

MODULE_PARM(irq, "i");
MODULE_PARM_DESC(mss_irq, "Set MSS (audio) IRQ (5, 7, 9, 10, 11, 12)");

MODULE_PARM(dma, "i");
MODULE_PARM_DESC(dma, "Set MSS (audio) first DMA channel (0, 1, 3)");

MODULE_PARM(dma2, "i");
MODULE_PARM_DESC(dma2, "Set MSS (audio) second DMA channel (0, 1, 3)");

MODULE_PARM(ymode, "i");
MODULE_PARM_DESC(ymode, "Set Yamaha 3D enhancement mode (0 = Desktop/Normal, 1 = Notebook PC (1), 2 = Notebook PC (2), 3 = Hi-Fi)");

MODULE_PARM(loopback, "i");
MODULE_PARM_DESC(loopback, "Set A/D input source. Useful for echo cancellation (0 = Mic Rch (default), 1 = Mono output loopback)");

#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE
MODULE_PARM(isapnp, "i");
MODULE_PARM_DESC(isapnp, "When set to 0, ISA PnP support will be disabled");

MODULE_PARM(multiple, "i");
MODULE_PARM_DESC(multiple, "When set to 0, will not search for multiple cards");
#endif


/*
 * Standard read and write functions
*/

static inline void opl3sa2_write(unsigned short port,
				 unsigned char  index,
				 unsigned char  data)
{
	outb_p(index, port);
	outb(data, port + 1);
}


static inline void opl3sa2_read(unsigned short port,
				unsigned char  index,
				unsigned char* data)
{
	outb_p(index, port);
	*data = inb(port + 1);
}


/*
 * All of the mixer functions...
 */

static void opl3sa2_set_volume(opl3sa2_mixerdata* devc, int left, int right)
{
	static unsigned char scale[101] = {
		0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0e, 0x0e, 0x0e,
		0x0e, 0x0e, 0x0e, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0c,
		0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0b, 0x0b, 0x0b, 0x0b,
		0x0b, 0x0b, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x09, 0x09,
		0x09, 0x09, 0x09, 0x09, 0x09, 0x08, 0x08, 0x08, 0x08, 0x08,
		0x08, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06,
		0x06, 0x06, 0x06, 0x06, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
		0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03,
		0x03, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00
	};
	unsigned char vol;

	vol = scale[left];

	/* If level is zero, turn on mute */
	if(!left)
		vol |= 0x80;

	opl3sa2_write(devc->cfg_port, OPL3SA2_MASTER_LEFT, vol);

	vol = scale[right];

	/* If level is zero, turn on mute */
	if(!right)
		vol |= 0x80;

	opl3sa2_write(devc->cfg_port, OPL3SA2_MASTER_RIGHT, vol);
}


static void opl3sa2_set_mic(opl3sa2_mixerdata* devc, int level)
{
	unsigned char vol = 0x1F;

	if((level >= 0) && (level <= 100))
		vol = 0x1F - (unsigned char) (32 * level / 101);

	/* If level is zero, turn on mute */
	if(!level)
		vol |= 0x80;

	opl3sa2_write(devc->cfg_port, OPL3SA2_MIC, vol);
}


static void opl3sa3_set_bass(opl3sa2_mixerdata* devc, int left, int right)
{
	unsigned char bass;

	bass = left ? ((unsigned char) (8 * left / 101)) : 0; 
	bass |= (right ? ((unsigned char) (8 * right / 101)) : 0) << 4;

	opl3sa2_write(devc->cfg_port, OPL3SA3_BASS, bass);
}


static void opl3sa3_set_treble(opl3sa2_mixerdata* devc, int left, int right)
{	
	unsigned char treble;

	treble = left ? ((unsigned char) (8 * left / 101)) : 0; 
	treble |= (right ? ((unsigned char) (8 * right / 101)) : 0) << 4;

	opl3sa2_write(devc->cfg_port, OPL3SA3_TREBLE, treble);
}


static void opl3sa3_set_wide(opl3sa2_mixerdata* devc, int left, int right)
{	
	unsigned char wide;

	wide = left ? ((unsigned char) (8 * left / 101)) : 0; 
	wide |= (right ? ((unsigned char) (8 * right / 101)) : 0) << 4;

	opl3sa2_write(devc->cfg_port, OPL3SA3_WIDE, wide);
}


static void opl3sa2_mixer_reset(opl3sa2_mixerdata* devc, int card)
{
	if(devc) {
		opl3sa2_set_volume(devc, DEFAULT_VOLUME, DEFAULT_VOLUME);
		devc->volume_l = devc->volume_r = DEFAULT_VOLUME;

		opl3sa2_set_mic(devc, DEFAULT_MIC);
		devc->mic = DEFAULT_MIC;

		if(chipset[card] == CHIPSET_OPL3SA3) {
			opl3sa3_set_bass(devc, DEFAULT_TIMBRE, DEFAULT_TIMBRE);
			devc->bass_l = devc->bass_r = DEFAULT_TIMBRE;
			opl3sa3_set_treble(devc, DEFAULT_TIMBRE, DEFAULT_TIMBRE);
			devc->treble_l = devc->treble_r = DEFAULT_TIMBRE;
		}
	}
}


static inline void arg_to_vol_mono(unsigned int vol, int* value)
{
	int left;
	
	left = vol & 0x00ff;
	if (left > 100)
		left = 100;
	*value = left;
}


static inline void arg_to_vol_stereo(unsigned int vol, int* aleft, int* aright)
{
	arg_to_vol_mono(vol, aleft);
	arg_to_vol_mono(vol >> 8, aright);
}


static inline int ret_vol_mono(int vol)
{
	return ((vol << 8) | vol);
}


static inline int ret_vol_stereo(int left, int right)
{
	return ((right << 8) | left);
}


static int opl3sa2_mixer_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	int cmdf = cmd & 0xff;

	opl3sa2_mixerdata* devc = (opl3sa2_mixerdata*) mixer_devs[dev]->devc;
	
	switch(cmdf) {
		case SOUND_MIXER_VOLUME:
		case SOUND_MIXER_MIC:
		case SOUND_MIXER_DEVMASK:
		case SOUND_MIXER_STEREODEVS: 
		case SOUND_MIXER_RECMASK:
		case SOUND_MIXER_RECSRC:
		case SOUND_MIXER_CAPS: 
			break;

		default:
			return -EINVAL;
	}
	
	if(((cmd >> 8) & 0xff) != 'M')
		return -EINVAL;
		
	if(_SIOC_DIR (cmd) & _SIOC_WRITE) {
		switch (cmdf) {
			case SOUND_MIXER_VOLUME:
				arg_to_vol_stereo(*(unsigned int*)arg,
						  &devc->volume_l, &devc->volume_r); 
				opl3sa2_set_volume(devc, devc->volume_l, devc->volume_r);
				*(int*)arg = ret_vol_stereo(devc->volume_l, devc->volume_r);
				return 0;
		  
			case SOUND_MIXER_MIC:
				arg_to_vol_mono(*(unsigned int*)arg, &devc->mic);
				opl3sa2_set_mic(devc, devc->mic);
				*(int*)arg = ret_vol_mono(devc->mic);
				return 0;

			default:
				return -EINVAL;
		}
	}
	else {
		/*
		 * Return parameters
		 */
		switch (cmdf) {
			case SOUND_MIXER_DEVMASK:
				*(int*)arg = (SOUND_MASK_VOLUME | SOUND_MASK_MIC);
				return 0;
		  
			case SOUND_MIXER_STEREODEVS:
				*(int*)arg = SOUND_MASK_VOLUME;
				return 0;
		  
			case SOUND_MIXER_RECMASK:
				/* No recording devices */
				return (*(int*)arg = 0);

			case SOUND_MIXER_CAPS:
				*(int*)arg = SOUND_CAP_EXCL_INPUT;
				return 0;

			case SOUND_MIXER_RECSRC:
				/* No recording source */
				return (*(int*)arg = 0);

			case SOUND_MIXER_VOLUME:
				*(int*)arg = ret_vol_stereo(devc->volume_l, devc->volume_r);
				return 0;
			  
			case SOUND_MIXER_MIC:
				*(int*)arg = ret_vol_mono(devc->mic);
				return 0;

			default:
				return -EINVAL;
		}
	}
}
/* opl3sa2_mixer_ioctl end */


static int opl3sa3_mixer_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	int cmdf = cmd & 0xff;

	opl3sa2_mixerdata* devc = (opl3sa2_mixerdata*) mixer_devs[dev]->devc;

	switch(cmdf) {
		case SOUND_MIXER_BASS:
		case SOUND_MIXER_TREBLE:
		case SOUND_MIXER_DIGITAL1:
		case SOUND_MIXER_DEVMASK:
		case SOUND_MIXER_STEREODEVS: 
			break;

		default:
			return opl3sa2_mixer_ioctl(dev, cmd, arg);
	}

	if(((cmd >> 8) & 0xff) != 'M')
		return -EINVAL;
		
	if(_SIOC_DIR (cmd) & _SIOC_WRITE) {
		switch (cmdf) {
			case SOUND_MIXER_BASS:
				arg_to_vol_stereo(*(unsigned int*)arg,
						  &devc->bass_l, &devc->bass_r); 
				opl3sa3_set_bass(devc, devc->bass_l, devc->bass_r);
				*(int*)arg = ret_vol_stereo(devc->bass_l, devc->bass_r);
				return 0;
		  
			case SOUND_MIXER_TREBLE:
				arg_to_vol_stereo(*(unsigned int*)arg,
						  &devc->treble_l, &devc->treble_r); 
				opl3sa3_set_treble(devc, devc->treble_l, devc->treble_r);
				*(int*)arg = ret_vol_stereo(devc->treble_l, devc->treble_r);
				return 0;

			case SOUND_MIXER_DIGITAL1:
				arg_to_vol_stereo(*(unsigned int*)arg,
						  &devc->wide_l, &devc->wide_r); 
				opl3sa3_set_wide(devc, devc->wide_l, devc->wide_r);
				*(int*)arg = ret_vol_stereo(devc->wide_l, devc->wide_r);
				return 0;

			default:
				return -EINVAL;
		}
	}
	else			
	{
		/*
		 * Return parameters
		 */
		switch (cmdf) {
			case SOUND_MIXER_DEVMASK:
				*(int*)arg = (SOUND_MASK_VOLUME | SOUND_MASK_MIC |
					      SOUND_MASK_BASS | SOUND_MASK_TREBLE |
					      SOUND_MASK_DIGITAL1);
				return 0;
		  
			case SOUND_MIXER_STEREODEVS:
				*(int*)arg = (SOUND_MASK_VOLUME | SOUND_MASK_BASS |
					      SOUND_MASK_TREBLE | SOUND_MASK_DIGITAL1);
				return 0;
		  
			case SOUND_MIXER_BASS:
				*(int*)arg = ret_vol_stereo(devc->bass_l, devc->bass_r);
				return 0;
			  
			case SOUND_MIXER_TREBLE:
				*(int*)arg = ret_vol_stereo(devc->treble_l, devc->treble_r);
				return 0;

			case SOUND_MIXER_DIGITAL1:
				*(int*)arg = ret_vol_stereo(devc->wide_l, devc->wide_r);
				return 0;

			default:
				return -EINVAL;
		}
	}
}
/* opl3sa3_mixer_ioctl end */


static struct mixer_operations opl3sa2_mixer_operations =
{
	owner:	THIS_MODULE,
	id:	"OPL3-SA2",
	name:	"Yamaha OPL3-SA2",
	ioctl:	opl3sa2_mixer_ioctl
};

static struct mixer_operations opl3sa3_mixer_operations =
{
	owner:	THIS_MODULE,
	id:	"OPL3-SA3",
	name:	"Yamaha OPL3-SA3",
	ioctl:	opl3sa3_mixer_ioctl
};

/* End of mixer-related stuff */


/*
 * Component probe, attach, unload functions
 */

static inline int __init probe_opl3sa2_mpu(struct address_info* hw_config)
{
	return probe_mpu401(hw_config);
}


static inline void __init attach_opl3sa2_mpu(struct address_info* hw_config)
{
	attach_mpu401(hw_config, THIS_MODULE);
}


static inline void __exit unload_opl3sa2_mpu(struct address_info *hw_config)
{
	unload_mpu401(hw_config);
}


static inline int __init probe_opl3sa2_mss(struct address_info* hw_config)
{
	return probe_ms_sound(hw_config);
}


static void __init attach_opl3sa2_mss(struct address_info* hw_config)
{
	int initial_mixers;

	initial_mixers = num_mixers;
	attach_ms_sound(hw_config, THIS_MODULE);	/* Slot 0 */
	if(hw_config->slots[0] != -1) {
		/* Did the MSS driver install? */
		if(num_mixers == (initial_mixers + 1)) {
			/* The MSS mixer is installed, reroute mixers appropiately */
			AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_CD);
			AD1848_REROUTE(SOUND_MIXER_LINE2, SOUND_MIXER_SYNTH);
			AD1848_REROUTE(SOUND_MIXER_LINE3, SOUND_MIXER_LINE);
		}
		else {
			printk(KERN_ERR "opl3sa2: MSS mixer not installed?\n");
		}
	}
}


static inline void __exit unload_opl3sa2_mss(struct address_info* hw_config)
{
	unload_ms_sound(hw_config);
}


static int __init probe_opl3sa2(struct address_info* hw_config, int card)
{
	unsigned char misc;
	unsigned char tmp;
	unsigned char version;
	char tag;

	/*
	 * Verify that the I/O port range is free.
	 */
	if(check_region(hw_config->io_base, 2)) {
		printk(KERN_ERR "opl3sa2: Control I/O port %#x not free\n",
		       hw_config->io_base);
		return 0;
	}

	/*
	 * Check if writing to the read-only version bits of the miscellaneous
	 * register succeeds or not (it should not).
	 */
	opl3sa2_read(hw_config->io_base, OPL3SA2_MISC, &misc);
	opl3sa2_write(hw_config->io_base, OPL3SA2_MISC, misc ^ 0x07);
	opl3sa2_read(hw_config->io_base, OPL3SA2_MISC, &tmp);
	if(tmp != misc) {
		printk(KERN_ERR "opl3sa2: Control I/O port %#x is not a YMF7xx chipset!\n",
		       hw_config->io_base);
		return 0;
	}

	/*
	 * Check if the MIC register is accessible.
	 */
	opl3sa2_read(hw_config->io_base, OPL3SA2_MIC, &tmp);
	opl3sa2_write(hw_config->io_base, OPL3SA2_MIC, 0x8a);
	opl3sa2_read(hw_config->io_base, OPL3SA2_MIC, &tmp);
	if((tmp & 0x9f) != 0x8a) {
		printk(KERN_ERR
		       "opl3sa2: Control I/O port %#x is not a YMF7xx chipset!\n",
		       hw_config->io_base);
		return 0;
	}
	opl3sa2_write(hw_config->io_base, OPL3SA2_MIC, tmp);

	/*
	 * Determine chipset type (SA2 or SA3)
	 *
	 * This is done by looking at the chipset version in the lower 3 bits
	 * of the miscellaneous register.
	 */
	version = misc & 0x07;
	printk(KERN_DEBUG "opl3sa2: chipset version = %#x\n", version);
	switch(version) {
		case 0:
			chipset[card] = CHIPSET_UNKNOWN;
			tag = '?'; /* silence compiler warning */
			printk(KERN_ERR
			       "opl3sa2: Unknown Yamaha audio controller version\n");
			break;

		case VERSION_YMF711:
			chipset[card] = CHIPSET_OPL3SA2;
			tag = '2';
			printk(KERN_INFO "opl3sa2: Found OPL3-SA2 (YMF711)\n");
			break;

		case VERSION_YMF715:
			chipset[card] = CHIPSET_OPL3SA3;
			tag = '3';
			printk(KERN_INFO
			       "opl3sa2: Found OPL3-SA3 (YMF715 or YMF719)\n");
			break;

		case VERSION_YMF715B:
			chipset[card] = CHIPSET_OPL3SA3;
			tag = '3';
			printk(KERN_INFO
			       "opl3sa2: Found OPL3-SA3 (YMF715B or YMF719B)\n");
			break;

		case VERSION_YMF715E:
		default:
			chipset[card] = CHIPSET_OPL3SA3;
			tag = '3';
			printk(KERN_INFO
			       "opl3sa2: Found OPL3-SA3 (YMF715E or YMF719E)\n");
			break;
	}

	if(chipset[card] != CHIPSET_UNKNOWN) {
		/* Generate a pretty name */
		sprintf(chipset_name[card], "OPL3-SA%c", tag);
		return 1;
	}
	return 0;
}


static void __init attach_opl3sa2(struct address_info* hw_config, int card)
{
   	request_region(hw_config->io_base, 2, chipset_name[card]);

	/* Initialize IRQ configuration to IRQ-B: -, IRQ-A: WSS+MPU+OPL3 */
	opl3sa2_write(hw_config->io_base, OPL3SA2_IRQ_CONFIG, 0x0d);

	/* Initialize DMA configuration */
	if(hw_config->dma2 == hw_config->dma) {
		/* Want DMA configuration DMA-B: -, DMA-A: WSS-P+WSS-R */
		opl3sa2_write(hw_config->io_base, OPL3SA2_DMA_CONFIG, 0x03);
	}
	else {
		/* Want DMA configuration DMA-B: WSS-R, DMA-A: WSS-P */
		opl3sa2_write(hw_config->io_base, OPL3SA2_DMA_CONFIG, 0x21);
	}
}


static void __init attach_opl3sa2_mixer(struct address_info *hw_config, int card)
{
	struct mixer_operations* mixer_operations;
	opl3sa2_mixerdata* devc;

	/* Install master mixer */
	if(chipset[card] == CHIPSET_OPL3SA3) {
		mixer_operations = &opl3sa3_mixer_operations;
	}
	else {
		mixer_operations = &opl3sa2_mixer_operations;
	}

	if((devc = &opl3sa2_data[card])) {
		devc->cfg_port = hw_config->io_base;

		opl3sa2_mixer[card] = sound_install_mixer(MIXER_DRIVER_VERSION,
							  mixer_operations->name,
							  mixer_operations,
							  sizeof(struct mixer_operations),
							  devc);
		if(opl3sa2_mixer[card] < 0) {
			printk(KERN_ERR "opl3sa2: Could not install %s master mixer\n",
				 mixer_operations->name);
		}
		else
			opl3sa2_mixer_reset(devc, card);
	}
}


static void __init opl3sa2_clear_slots(struct address_info* hw_config)
{
	int i;

	for(i = 0; i < 6; i++) {
		hw_config->slots[i] = -1;
	}
}


static void __init opl3sa2_set_ymode(struct address_info* hw_config, int ymode)
{
	/*
	 * Set the Yamaha 3D enhancement mode (aka Ymersion) if asked to and
	 * it's supported.
	 *
	 * 0: Desktop (aka normal)   5-12 cm speakers
	 * 1: Notebook PC mode 1     3 cm speakers
	 * 2: Notebook PC mode 2     1.5 cm speakers
	 * 3: Hi-fi                  16-38 cm speakers
	 */
	if(ymode >= 0 && ymode <= 3) {
		unsigned char sys_ctrl;

		opl3sa2_read(hw_config->io_base, OPL3SA2_SYS_CTRL, &sys_ctrl);
		sys_ctrl = (sys_ctrl & 0xcf) | ((ymode & 3) << 4);
		opl3sa2_write(hw_config->io_base, OPL3SA2_SYS_CTRL, sys_ctrl);
	}
	else {
		printk(KERN_ERR "opl3sa2: not setting ymode, it must be one of 0,1,2,3\n");
	}
}


static void __init opl3sa2_set_loopback(struct address_info* hw_config, int loopback)
{
	if(loopback >= 0 && loopback <= 1) {
		unsigned char misc;

		opl3sa2_read(hw_config->io_base, OPL3SA2_MISC, &misc);
		misc = (misc & 0xef) | ((loopback & 1) << 4);
		opl3sa2_write(hw_config->io_base, OPL3SA2_MISC, misc);
	}
	else {
		printk(KERN_ERR "opl3sa2: not setting loopback, it must be either 0 or 1\n");
	}
}


static void __exit unload_opl3sa2(struct address_info* hw_config, int card)
{
        /* Release control ports */
	release_region(hw_config->io_base, 2);

	/* Unload mixer */
	if(opl3sa2_mixer[card] >= 0)
		sound_unload_mixerdev(opl3sa2_mixer[card]);
}


#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE

struct isapnp_device_id isapnp_opl3sa2_list[] __initdata = {
	{	ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('Y','M','H'), ISAPNP_FUNCTION(0x0021),
		0 },
	{0}
};

MODULE_DEVICE_TABLE(isapnp, isapnp_opl3sa2_list);

static int __init opl3sa2_isapnp_probe(struct address_info* hw_cfg,
				       struct address_info* mss_cfg,
				       struct address_info* mpu_cfg,
				       int card)
{
	static struct pci_dev* dev;
	int ret;

	/* Find and configure device */
	dev = isapnp_find_dev(NULL,
			      ISAPNP_VENDOR('Y','M','H'),
			      ISAPNP_FUNCTION(0x0021),
			      dev);
	if(dev == NULL) {
		return -ENODEV;
	}

	/*
	 * If device is active, assume configured with /proc/isapnp
	 * and use anyway. Any other way to check this?
	 */
	ret = dev->prepare(dev);
	if(ret && ret != -EBUSY) {
		printk(KERN_ERR "opl3sa2: ISA PnP found device that could not be autoconfigured.\n");
		return -ENODEV;
	}
	if(ret == -EBUSY) {
		opl3sa2_activated[card] = 1;
	}
	else {
		if(dev->activate(dev) < 0) {
			printk(KERN_WARNING "opl3sa2: ISA PnP activate failed\n");
			opl3sa2_activated[card] = 0;
			return -ENODEV;
		}

		printk(KERN_DEBUG
		       "opl3sa2: Activated ISA PnP card %d (active=%d)\n",
		       card, dev->active);

	}

	/* Our own config: */
	hw_cfg->io_base = dev->resource[4].start;
	hw_cfg->irq     = dev->irq_resource[0].start;
	hw_cfg->dma     = dev->dma_resource[0].start;
	hw_cfg->dma2    = dev->dma_resource[1].start;
	
	/* The MSS config: */
	mss_cfg->io_base      = dev->resource[1].start;
	mss_cfg->irq          = dev->irq_resource[0].start;
	mss_cfg->dma          = dev->dma_resource[0].start;
	mss_cfg->dma2         = dev->dma_resource[1].start;
	mss_cfg->card_subtype = 1; /* No IRQ or DMA setup */

	mpu_cfg->io_base       = dev->resource[3].start;
	mpu_cfg->irq           = dev->irq_resource[0].start;
	mpu_cfg->dma           = -1;
	mpu_cfg->dma2          = -1;
	mpu_cfg->always_detect = 1; /* It's there, so use shared IRQs */

	/* Call me paranoid: */
	opl3sa2_clear_slots(hw_cfg);
	opl3sa2_clear_slots(mss_cfg);
	opl3sa2_clear_slots(mpu_cfg);

	opl3sa2_dev[card] = dev;

	return 0;
}
#endif /* CONFIG_ISAPNP || CONFIG_ISAPNP_MODULE */

/* End of component functions */


/*
 * Install OPL3-SA2 based card(s).
 *
 * Need to have ad1848 and mpu401 loaded ready.
 */
static int __init init_opl3sa2(void)
{
        int card;
	int max;

	/* Sanitize isapnp and multiple settings */
	isapnp = isapnp != 0 ? 1 : 0;
	multiple = multiple != 0 ? 1 : 0;
	
	max = (multiple && isapnp) ? OPL3SA2_CARDS_MAX : 1;
	for(card = 0; card < max; card++, opl3sa2_cards_num++) {
#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE
		/*
		 * Please remember that even with CONFIG_ISAPNP defined one
		 * should still be able to disable PNP support for this 
		 * single driver!
		 */
		if(isapnp && opl3sa2_isapnp_probe(&cfg[card],
						  &cfg_mss[card],
						  &cfg_mpu[card],
						  card) < 0) {
			if(!opl3sa2_cards_num)
				printk(KERN_INFO "opl3sa2: No PnP cards found\n");
			if(io == -1)
				break;
			isapnp=0;
			printk(KERN_INFO "opl3sa2: Search for a card at 0x%d.\n", io);
			/* Fall through */
		}
#endif
		/* If a user wants an I/O then assume they meant it */
		
		if(!isapnp) {
			if(io == -1 || irq == -1 || dma == -1 ||
			   dma2 == -1 || mss_io == -1) {
				printk(KERN_ERR
				       "opl3sa2: io, mss_io, irq, dma, and dma2 must be set\n");
				return -EINVAL;
			}

			/*
			 * Our own config:
			 * (NOTE: IRQ and DMA aren't used, so they're set to
			 *  give pretty output from conf_printf. :)
			 */
			cfg[card].io_base = io;
			cfg[card].irq     = irq;
			cfg[card].dma     = dma;
			cfg[card].dma2    = dma2;
	
			/* The MSS config: */
			cfg_mss[card].io_base      = mss_io;
			cfg_mss[card].irq          = irq;
			cfg_mss[card].dma          = dma;
			cfg_mss[card].dma2         = dma2;
			cfg_mss[card].card_subtype = 1; /* No IRQ or DMA setup */

			cfg_mpu[card].io_base       = mpu_io;
			cfg_mpu[card].irq           = irq;
			cfg_mpu[card].dma           = -1;
			cfg_mpu[card].always_detect = 1; /* Use shared IRQs */

			/* Call me paranoid: */
			opl3sa2_clear_slots(&cfg[card]);
			opl3sa2_clear_slots(&cfg_mss[card]);
			opl3sa2_clear_slots(&cfg_mpu[card]);
		}

		if(!probe_opl3sa2(&cfg[card], card) ||
		   !probe_opl3sa2_mss(&cfg_mss[card])) {
			/*
			 * If one or more cards are already registered, don't
			 * return an error but print a warning.  Note, this
			 * should never really happen unless the hardware or
			 * ISA PnP screwed up.
			 */
			if(opl3sa2_cards_num) {
				printk(KERN_WARNING
				       "opl3sa2: There was a problem probing one "
				       " of the ISA PNP cards, continuing\n");
				opl3sa2_cards_num--;
				continue;
			} else
				return -ENODEV;
		}

		attach_opl3sa2(&cfg[card], card);
		conf_printf(chipset_name[card], &cfg[card]);
		attach_opl3sa2_mss(&cfg_mss[card]);
		attach_opl3sa2_mixer(&cfg[card], card);

		/*
		 * Set the Yamaha 3D enhancement mode (aka Ymersion) if asked to and
		 * it's supported.
		 */
		if(ymode != -1) {
			if(chipset[card] == CHIPSET_OPL3SA2) {
				printk(KERN_ERR
				       "opl3sa2: ymode not supported on OPL3-SA2\n");
			}
			else {
				opl3sa2_set_ymode(&cfg[card], ymode);
			}
		}


		/* Set A/D input to Mono loopback if asked to. */
		if(loopback != -1) {
			opl3sa2_set_loopback(&cfg[card], loopback);
		}
		
		/* Attach MPU if we've been asked to do so */
		if(cfg_mpu[card].io_base != -1) {
			if(probe_opl3sa2_mpu(&cfg_mpu[card])) {
				attach_opl3sa2_mpu(&cfg_mpu[card]);
			}
		}
	}

	if(isapnp) {
		printk(KERN_NOTICE "opl3sa2: %d PnP card(s) found.\n", opl3sa2_cards_num);
	}

	return 0;
}


/*
 * Uninstall OPL3-SA2 based card(s).
 */
static void __exit cleanup_opl3sa2(void)
{
	int card;

	for(card = 0; card < opl3sa2_cards_num; card++) {
	        if(cfg_mpu[card].slots[1] != -1) {
			unload_opl3sa2_mpu(&cfg_mpu[card]);
		}
		unload_opl3sa2_mss(&cfg_mss[card]);
		unload_opl3sa2(&cfg[card], card);

#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE
		if(opl3sa2_activated[card] && opl3sa2_dev[card]) {
			opl3sa2_dev[card]->deactivate(opl3sa2_dev[card]);

			printk(KERN_DEBUG
			       "opl3sa2: Deactivated ISA PnP card %d (active=%d)\n",
			       card, opl3sa2_dev[card]->active);
		}
#endif
	}
}

module_init(init_opl3sa2);
module_exit(cleanup_opl3sa2);

#ifndef MODULE
static int __init setup_opl3sa2(char *str)
{
	/* io, irq, dma, dma2,... */
#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE
	int ints[11];
#else
	int ints[9];
#endif
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io       = ints[1];
	irq      = ints[2];
	dma      = ints[3];
	dma2     = ints[4];
	mss_io   = ints[5];
	mpu_io   = ints[6];
	ymode    = ints[7];
	loopback = ints[8];
#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE
	isapnp   = ints[9];
	multiple = ints[10];
#endif
	return 1;
}

__setup("opl3sa2=", setup_opl3sa2);
#endif
