/*
 *  arch/ppc/kernel/feature.c
 *
 *  Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *                     Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <asm/errno.h>
#include <asm/ohare.h>
#include <asm/heathrow.h>
#include <asm/keylargo.h>
#include <asm/uninorth.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/feature.h>
#include <asm/dbdma.h>
#include <asm/machdep.h>

#undef DEBUG_FEATURE

#define MAX_FEATURE_CONTROLLERS		2
#define FREG(c,r)			(&(((c)->reg)[(r)>>2]))

/* Keylargo reg. access. */
#define KL_FCR(r)	(keylargo_base + ((r) >> 2))
#define KL_IN(r)	(in_le32(KL_FCR(r)))
#define KL_OUT(r,v)	(out_le32(KL_FCR(r), (v)))
#define KL_BIS(r,v)	(KL_OUT((r), KL_IN(r) | (v)))
#define KL_BIC(r,v)	(KL_OUT((r), KL_IN(r) & ~(v)))

/* Uni-N reg. access. Note that Uni-N regs are big endian */
#define UN_REG(r)	(uninorth_base + ((r) >> 2))
#define UN_IN(r)	(in_be32(UN_REG(r)))
#define UN_OUT(r,v)	(out_be32(UN_REG(r), (v)))
#define UN_BIS(r,v)	(UN_OUT((r), UN_IN(r) | (v)))
#define UN_BIC(r,v)	(UN_OUT((r), UN_IN(r) & ~(v)))

typedef struct feature_bit {
	int		reg;		/* reg. offset from mac-io base */
	unsigned int	polarity;	/* 0 = normal, 1 = inverse */
	unsigned int	mask;		/* bit mask */
} fbit;

/* Those features concern for OHare-based PowerBooks (2400, 3400, 3500)
 */
static fbit feature_bits_ohare_pbook[] = {
	{0x38,0,0},			/* FEATURE_null */
	{0x38,0,OH_SCC_RESET},		/* FEATURE_Serial_reset */
	{0x38,0,OH_SCC_ENABLE},		/* FEATURE_Serial_enable */
	{0x38,0,OH_SCCA_IO},		/* FEATURE_Serial_IO_A */
	{0x38,0,OH_SCCB_IO},		/* FEATURE_Serial_IO_B */
	{0x38,0,OH_FLOPPY_ENABLE},	/* FEATURE_SWIM3_enable */
	{0x38,0,OH_MESH_ENABLE},	/* FEATURE_MESH_enable */
	{0x38,0,OH_IDE0_ENABLE},	/* FEATURE_IDE0_enable */
	{0x38,1,OH_IDE0_RESET_N},	/* FEATURE_IDE0_reset */
	{0x38,0,OH_IOBUS_ENABLE},	/* FEATURE_IOBUS_enable */
	{0x38,1,OH_BAY_RESET_N},	/* FEATURE_Mediabay_reset */
	{0x38,1,OH_BAY_POWER_N},	/* FEATURE_Mediabay_power */
	{0x38,0,OH_BAY_PCI_ENABLE},	/* FEATURE_Mediabay_PCI_enable */
	{0x38,0,OH_BAY_IDE_ENABLE},	/* FEATURE_IDE1_enable */
	{0x38,1,OH_IDE1_RESET_N},	/* FEATURE_IDE1_reset */
	{0x38,0,OH_BAY_FLOPPY_ENABLE},	/* FEATURE_Mediabay_floppy_enable */
	{0x38,0,0},			/* FEATURE_BMac_reset */
	{0x38,0,0},			/* FEATURE_BMac_IO_enable */
	{0x38,0,0},			/* FEATURE_Modem_power */
	{0x38,0,0},			/* FEATURE_Slow_SCC_PCLK */
	{0x38,0,0},			/* FEATURE_Sound_Power */
	{0x38,0,0},			/* FEATURE_Sound_CLK_Enable */
	{0x38,0,0},			/* FEATURE_IDE2_enable */
	{0x38,0,0},			/* FEATURE_IDE2_reset */
	{0x38,0,0},			/* FEATURE_Mediabay_IDE_switch */
	{0x38,0,0},			/* FEATURE_Mediabay_content */
	{0x38,0,0},			/* FEATURE_Airport_reset */
};

/* Those bits concern heathrow-based desktop machines (Beige G3s). We have removed
 * the SCC related bits and init them once. They have proven to occasionally cause
 * problems with the desktop units.
 */
static fbit feature_bits_heathrow[] = {
	{0x38,0,0},			/* FEATURE_null */
	{0x38,0,0},			/* FEATURE_Serial_reset */
	{0x38,0,0},			/* FEATURE_Serial_enable */
	{0x38,0,0},			/* FEATURE_Serial_IO_A */
	{0x38,0,0},			/* FEATURE_Serial_IO_B */
	{0x38,0,HRW_SWIM_ENABLE},	/* FEATURE_SWIM3_enable */
	{0x38,0,HRW_MESH_ENABLE},	/* FEATURE_MESH_enable */
	{0x38,0,HRW_IDE0_ENABLE},	/* FEATURE_IDE0_enable */
	{0x38,1,HRW_IDE0_RESET_N},	/* FEATURE_IDE0_reset */
	{0x38,0,HRW_IOBUS_ENABLE},	/* FEATURE_IOBUS_enable */
	{0x38,1,0},			/* FEATURE_Mediabay_reset */
	{0x38,1,0},			/* FEATURE_Mediabay_power */
	{0x38,0,0},			/* FEATURE_Mediabay_PCI_enable */
	{0x38,0,HRW_BAY_IDE_ENABLE},	/* FEATURE_IDE1_enable */
	{0x38,1,HRW_IDE1_RESET_N},	/* FEATURE_IDE1_reset */
	{0x38,0,0},			/* FEATURE_Mediabay_floppy_enable */
	{0x38,0,HRW_BMAC_RESET},	/* FEATURE_BMac_reset */
	{0x38,0,HRW_BMAC_IO_ENABLE},	/* FEATURE_BMac_IO_enable */
	{0x38,1,0},			/* FEATURE_Modem_power */
	{0x38,0,HRW_SLOW_SCC_PCLK},	/* FEATURE_Slow_SCC_PCLK */
	{0x38,1,0},			/* FEATURE_Sound_Power */
	{0x38,0,0},			/* FEATURE_Sound_CLK_Enable */
	{0x38,0,0},			/* FEATURE_IDE2_enable */
	{0x38,0,0},			/* FEATURE_IDE2_reset */
	{0x38,0,0},			/* FEATURE_Mediabay_IDE_switch */
	{0x38,0,0},			/* FEATURE_Mediabay_content */
	{0x38,0,0},			/* FEATURE_Airport_reset */
};

/* Those bits concern heathrow-based PowerBooks (wallstreet/mainstreet).
 * Heathrow-based desktop macs (Beige G3s) are _not_ handled here
 */
static fbit feature_bits_wallstreet[] = {
	{0x38,0,0},			/* FEATURE_null */
	{0x38,0,HRW_RESET_SCC},		/* FEATURE_Serial_reset */
	{0x38,0,HRW_SCC_ENABLE},	/* FEATURE_Serial_enable */
	{0x38,0,HRW_SCCA_IO},		/* FEATURE_Serial_IO_A */
	{0x38,0,HRW_SCCB_IO},		/* FEATURE_Serial_IO_B */
	{0x38,0,HRW_SWIM_ENABLE},	/* FEATURE_SWIM3_enable */
	{0x38,0,HRW_MESH_ENABLE},	/* FEATURE_MESH_enable */
	{0x38,0,HRW_IDE0_ENABLE},	/* FEATURE_IDE0_enable */
	{0x38,1,HRW_IDE0_RESET_N},	/* FEATURE_IDE0_reset */
	{0x38,0,HRW_IOBUS_ENABLE},	/* FEATURE_IOBUS_enable */
	{0x38,1,HRW_BAY_RESET_N},	/* FEATURE_Mediabay_reset */
	{0x38,1,HRW_BAY_POWER_N},	/* FEATURE_Mediabay_power */
	{0x38,0,HRW_BAY_PCI_ENABLE},	/* FEATURE_Mediabay_PCI_enable */
	{0x38,0,HRW_BAY_IDE_ENABLE},	/* FEATURE_IDE1_enable */
	{0x38,1,HRW_IDE1_RESET_N},	/* FEATURE_IDE1_reset */
	{0x38,0,HRW_BAY_FLOPPY_ENABLE},	/* FEATURE_Mediabay_floppy_enable */
	{0x38,0,HRW_BMAC_RESET},	/* FEATURE_BMac_reset */
	{0x38,0,HRW_BMAC_IO_ENABLE},	/* FEATURE_BMac_IO_enable */
	{0x38,1,HRW_MODEM_POWER_N},	/* FEATURE_Modem_power */
	{0x38,0,HRW_SLOW_SCC_PCLK},	/* FEATURE_Slow_SCC_PCLK */
	{0x38,1,HRW_SOUND_POWER_N},	/* FEATURE_Sound_Power */
	{0x38,0,HRW_SOUND_CLK_ENABLE},	/* FEATURE_Sound_CLK_Enable */
	{0x38,0,0},			/* FEATURE_IDE2_enable */
	{0x38,0,0},			/* FEATURE_IDE2_reset */
	{0x38,0,0},			/* FEATURE_Mediabay_IDE_switch */
	{0x38,0,0},			/* FEATURE_Mediabay_content */
	{0x38,0,0},			/* FEATURE_Airport_reset */
};

/*
 * Those bits are from a 1999 G3 PowerBook, with a paddington chip.
 * Mostly the same as the heathrow. They are used on both PowerBooks
 * and desktop machines using the paddington chip
 */
static fbit feature_bits_paddington[] = {
	{0x38,0,0},			/* FEATURE_null */
	{0x38,0,PADD_RESET_SCC},	/* FEATURE_Serial_reset */
	{0x38,0,HRW_SCC_ENABLE},	/* FEATURE_Serial_enable */
	{0x38,0,HRW_SCCA_IO},		/* FEATURE_Serial_IO_A */
	{0x38,0,HRW_SCCB_IO},		/* FEATURE_Serial_IO_B */
	{0x38,0,HRW_SWIM_ENABLE},	/* FEATURE_SWIM3_enable */
	{0x38,0,HRW_MESH_ENABLE},	/* FEATURE_MESH_enable */
	{0x38,0,HRW_IDE0_ENABLE},	/* FEATURE_IDE0_enable */
	{0x38,1,HRW_IDE0_RESET_N},	/* FEATURE_IDE0_reset */
	{0x38,0,HRW_IOBUS_ENABLE},	/* FEATURE_IOBUS_enable */
	{0x38,1,HRW_BAY_RESET_N},	/* FEATURE_Mediabay_reset */
	{0x38,1,HRW_BAY_POWER_N},	/* FEATURE_Mediabay_power */
	{0x38,0,HRW_BAY_PCI_ENABLE},	/* FEATURE_Mediabay_PCI_enable */
	{0x38,0,HRW_BAY_IDE_ENABLE},	/* FEATURE_IDE1_enable */
	{0x38,1,HRW_IDE1_RESET_N},	/* FEATURE_IDE1_reset */
	{0x38,0,HRW_BAY_FLOPPY_ENABLE},	/* FEATURE_Mediabay_floppy_enable */
	{0x38,0,HRW_BMAC_RESET},	/* FEATURE_BMac_reset */
	{0x38,0,HRW_BMAC_IO_ENABLE},	/* FEATURE_BMac_IO_enable */
	{0x38,1,PADD_MODEM_POWER_N},	/* FEATURE_Modem_power */
	{0x38,0,HRW_SLOW_SCC_PCLK},	/* FEATURE_Slow_SCC_PCLK */
	{0x38,1,HRW_SOUND_POWER_N},	/* FEATURE_Sound_Power */
	{0x38,0,HRW_SOUND_CLK_ENABLE},	/* FEATURE_Sound_CLK_Enable */
	{0x38,0,0},			/* FEATURE_IDE2_enable */
	{0x38,0,0},			/* FEATURE_IDE2_reset */
	{0x38,0,0},			/* FEATURE_Mediabay_IDE_switch */
	{0x38,0,0},			/* FEATURE_Mediabay_content */
	{0x38,0,0},			/* FEATURE_Airport_reset */
};

/* Those bits are for Core99 machines (iBook,G4,iMacSL/DV,Pismo,...).
 * Note: Different sets may be needed for iBook, especially for sound
 */
static fbit feature_bits_keylargo[] = {
	{0x38,0,0},			/* FEATURE_null */
	{0x38,0,KL0_SCC_RESET},		/* FEATURE_Serial_reset */
	{0x38,0,KL0_SERIAL_ENABLE},	/* FEATURE_Serial_enable */
	{0x38,0,KL0_SCC_A_INTF_ENABLE},	/* FEATURE_Serial_IO_A */
	{0x38,0,KL0_SCC_B_INTF_ENABLE},	/* FEATURE_Serial_IO_B */
	{0x38,0,0},			/* FEATURE_SWIM3_enable */
	{0x38,0,0},			/* FEATURE_MESH_enable */
	{0x3c,0,KL1_EIDE0_ENABLE},	/* FEATURE_IDE0_enable */
 	{0x3c,1,KL1_EIDE0_RESET_N},	/* FEATURE_IDE0_reset */
	{0x38,0,0},			/* FEATURE_IOBUS_enable */
	{0x34,1,KL_MBCR_MB0_DEV_RESET},	/* FEATURE_Mediabay_reset */
	{0x34,1,KL_MBCR_MB0_DEV_POWER},	/* FEATURE_Mediabay_power */
	{0x38,0,0},			/* FEATURE_Mediabay_PCI_enable */
	{0x3c,0,KL1_EIDE1_ENABLE},	/* FEATURE_IDE1_enable */
	{0x3c,1,KL1_EIDE1_RESET_N},	/* FEATURE_IDE1_reset */
	{0x38,0,0},			/* FEATURE_Mediabay_floppy_enable */
	{0x38,0,0},			/* FEATURE_BMac_reset */
	{0x38,0,0},			/* FEATURE_BMac_IO_enable */
	{0x40,1,KL2_MODEM_POWER_N},	/* FEATURE_Modem_power */
	{0x38,0,0},			/* FEATURE_Slow_SCC_PCLK */
	{0x38,0,0},			/* FEATURE_Sound_Power */
	{0x38,0,0},			/* FEATURE_Sound_CLK_Enable */
	{0x3c,0,KL1_UIDE_ENABLE},	/* FEATURE_IDE2_enable */
	{0x3c,1,KL1_UIDE_RESET_N},	/* FEATURE_IDE2_reset */
	{0x34,0,KL_MBCR_MB0_DEV_ENABLE},/* FEATURE_Mediabay_IDE_switch */
	{0x34,0,KL_MBCR_MB0_ENABLE},	/* FEATURE_Mediabay_content */
	{0x40,1,KL2_AIRPORT_RESET_N},	/* FEATURE_Airport_reset */
};

/* definition of a feature controller object */
struct feature_controller {
	fbit*			bits;
	volatile u32*		reg;
	struct device_node*	device;
	spinlock_t		lock;
};

/* static functions */
static struct feature_controller*
feature_add_controller(struct device_node *controller_device, fbit* bits);

static struct feature_controller*
feature_lookup_controller(struct device_node *device);

static void uninorth_init(void);
static void keylargo_init(void);
#ifdef CONFIG_PMAC_PBOOK
static void heathrow_prepare_for_sleep(struct feature_controller* ctrler);
static void heathrow_wakeup(struct feature_controller* ctrler);
static void core99_prepare_for_sleep(struct feature_controller* ctrler);
static void core99_wake_up(struct feature_controller* ctrler);
#endif /* CONFIG_PMAC_PBOOK */

/* static variables */
static struct feature_controller	controllers[MAX_FEATURE_CONTROLLERS];
static int				controller_count = 0;

/* Core99 stuffs */
/*static*/ volatile u32*		uninorth_base;
static volatile u32*			keylargo_base;
static struct feature_controller*	keylargo;
static int				uninorth_rev;
static int				keylargo_rev;
static u32				board_features;
static u8				airport_pwr_regs[5];
static int				airport_pwr_state;
static struct device_node*		airport_dev;

#define FTR_NEED_OPENPIC_TWEAK		0x00000001
#define FTR_CAN_NAP			0x00000002
#define FTR_HAS_FW_POWER		0x00000004
#define FTR_CAN_SLEEP			0x00000008

static struct board_features_t {
	char*	compatible;
	u32	features;
} board_features_datas[] __init = 
{
  {	"AAPL,PowerMac G3",	0				}, /* Beige G3 */
  {	"iMac,1",		0				}, /* First iMac (gossamer) */
  {	"PowerMac1,1",		0				}, /* B&W G3 / Yikes */
  {	"PowerMac1,2",		0				}, /* B&W G3 / Yikes */
  {	"PowerMac2,1",		FTR_CAN_SLEEP			}, /* r128 based iMac */
  {	"PowerMac2,2",		FTR_HAS_FW_POWER|FTR_CAN_SLEEP	}, /* Summer 2000 iMac */
  {	"PowerMac4,1",		FTR_CAN_SLEEP			}, /* iMac "Flower Power" */
  {	"PowerMac3,1",		FTR_NEED_OPENPIC_TWEAK		}, /* Sawtooth (G4) */
  {	"PowerMac3,2",		0				}, /* G4/Dual G4 */
  {	"PowerMac3,3",		FTR_NEED_OPENPIC_TWEAK		}, /* G4/Dual G4 */
  {	"PowerMac5,1",		0				}, /* Cube */
  {	"AAPL,3400/2400",	FTR_CAN_SLEEP			}, /* 2400/3400 PowerBook */
  {	"AAPL,3500",		FTR_CAN_SLEEP			}, /* 3500 PowerBook (G3) */
  {	"AAPL,PowerBook1998",	FTR_CAN_SLEEP			}, /* Wallstreet PowerBook */
  {	"PowerBook1,1",		FTR_CAN_SLEEP			}, /* 101 (Lombard) PowerBook */
  {	"PowerBook2,1",		FTR_CAN_SLEEP 			}, /* iBook */
  {	"PowerBook2,2",		FTR_CAN_SLEEP /*| FTR_CAN_NAP*/	}, /* iBook FireWire */
  {	"PowerBook3,1",		FTR_CAN_SLEEP|FTR_CAN_NAP|	   /* PowerBook 2000 (Pismo) */
  				FTR_HAS_FW_POWER		}, 
  {	"PowerBook3,2",		FTR_CAN_NAP|FTR_CAN_SLEEP	}, /* PowerBook Titanium */
  {	"PowerBook4,1",		FTR_CAN_NAP|FTR_CAN_SLEEP	}, /* New polycarbonate iBook */
  {	NULL, 0 }
};

extern unsigned long powersave_nap;

void
feature_init(void)
{
	struct device_node *np;
	u32 *rev;
	int i;

	if (_machine != _MACH_Pmac)
		return;

	/* Figure out motherboard type & options */
	for(i=0;board_features_datas[i].compatible;i++) {
		if (machine_is_compatible(board_features_datas[i].compatible)) {
			board_features = board_features_datas[i].features;
			break;
		}
	}

	/* Set default value of powersave_nap on machines that support it */
	if (board_features & FTR_CAN_NAP)
		powersave_nap = 1;
		
	/* Track those poor mac-io's */
	np = find_devices("mac-io");
	while (np != NULL) {
		/* KeyLargo contains several (5 ?) FCR registers in mac-io,
		 * plus some gpio's which could eventually be handled here.
		 */
		if (device_is_compatible(np, "Keylargo")) {
			struct feature_controller* ctrler =
				feature_add_controller(np, feature_bits_keylargo);
			if (ctrler) {
				keylargo = ctrler;
				keylargo_base = ctrler->reg;
				rev = (u32 *)get_property(ctrler->device, "revision-id", NULL);
				if (rev)
					keylargo_rev = *rev;
				
				rev = (u32 *)get_property(ctrler->device, "device-id", NULL);
				if (rev && (*rev) == 0x0025)
					keylargo_rev |= KL_PANGEA_REV;
			}
		} else if (device_is_compatible(np, "paddington")) {
			feature_add_controller(np, feature_bits_paddington);
			/* We disable the modem power bit on Yikes as it can
			 * do bad things (it's the nvram power)
			 */
			if (machine_is_compatible("PowerMac1,1") ||
				machine_is_compatible("PowerMac1,2")) {
				feature_bits_paddington[FEATURE_Modem_power].mask = 0;
			}
		} else if (machine_is_compatible("AAPL,PowerBook1998")) {
			feature_add_controller(np, feature_bits_wallstreet);
		} else {
			struct feature_controller* ctrler =
				feature_add_controller(np, feature_bits_heathrow);
			if (ctrler)
				out_le32(FREG(ctrler,HEATHROW_FEATURE_REG),
					in_le32(FREG(ctrler,HEATHROW_FEATURE_REG)) | HRW_DEFAULTS);
			
		}
		np = np->next;
	}
	if (controller_count == 0)
	{
		np = find_devices("ohare");
		if (np) {
			if (find_devices("via-pmu") != NULL)
				feature_add_controller(np, feature_bits_ohare_pbook);
			else
				/* else not sure; maybe this is a Starmax? */
				feature_add_controller(np, NULL);
		}
	}

	/* Locate core99 Uni-N */
	np = find_devices("uni-n");
	if (np && np->n_addrs > 0) {
		uninorth_base = ioremap(np->addrs[0].address, 0x1000);
		uninorth_rev = in_be32(UN_REG(UNI_N_VERSION));
		printk("Uninorth at 0x%08x\n", np->addrs[0].address);
	}
	if (uninorth_base && keylargo_base)
		printk("Uni-N revision: %d, KeyLargo revision: %d\n",
			uninorth_rev, keylargo_rev);

	if (uninorth_base)
		uninorth_init();
	if (keylargo_base)
		keylargo_init();

	if (controller_count)
		printk(KERN_INFO "Registered %d feature controller(s)\n", controller_count);

#if defined(CONFIG_PMAC_PBOOK) && !defined(CONFIG_DMASOUND_AWACS)
	/* On PowerBooks, we disable the sound chip when dmasound is a module
	 * or not used at all
	 */
	if (controller_count && find_devices("via-pmu") != NULL) {
		feature_clear(controllers[0].device, FEATURE_Sound_power);
		feature_clear(controllers[0].device, FEATURE_Sound_CLK_enable);
	}
#endif
}

static struct feature_controller*
feature_add_controller(struct device_node *controller_device, fbit* bits)
{
	struct feature_controller*	controller;
	
	if (controller_count >= MAX_FEATURE_CONTROLLERS) {
		printk(KERN_INFO "Feature controller %s skipped(MAX:%d)\n",
			controller_device->full_name, MAX_FEATURE_CONTROLLERS);
		return NULL;
	}
	controller = &controllers[controller_count];

	controller->bits	= bits;
	controller->device	= controller_device;
	if (controller_device->n_addrs == 0) {
		printk(KERN_ERR "No addresses for %s\n",
			controller_device->full_name);
		return NULL;
	}

	/* We remap the entire mac-io here. Normally, this will just
	 * give us back our already existing BAT mapping
	 */
	controller->reg		= (volatile u32 *)ioremap(
		controller_device->addrs[0].address,
		controller_device->addrs[0].size);

	if (bits == NULL) {
		printk(KERN_INFO "Twiddling the magic ohare bits\n");
		out_le32(FREG(controller,OHARE_FEATURE_REG), STARMAX_FEATURES);
		return NULL;
	}

	spin_lock_init(&controller->lock);

	controller_count++;

	return controller;
}

static struct feature_controller*
feature_lookup_controller(struct device_node *device)
{
	int	i;
	
	if (device == NULL)
		return NULL;
		
	while(device)
	{
		for (i=0; i<controller_count; i++)
			if (device == controllers[i].device)
				return &controllers[i];
		device = device->parent;
	}

#ifdef DEBUG_FEATURE
	printk("feature: <%s> not found on any controller\n",
		device->name);
#endif
	
	return NULL;
}

int
feature_set(struct device_node* device, enum system_feature f)
{
	struct feature_controller*	controller;
	unsigned long			flags;
	unsigned long			value;
	fbit*				bit;

	if (f >= FEATURE_last)
		return -EINVAL;	

	controller = feature_lookup_controller(device);
	if (!controller)
		return -ENODEV;
	bit = &controller->bits[f];
	if (!bit->mask)
		return -EINVAL;
	
#ifdef DEBUG_FEATURE
	printk("feature: <%s> setting feature %d in controller @0x%x\n",
		device->name, (int)f, (unsigned int)controller->reg);
#endif

	spin_lock_irqsave(&controller->lock, flags);
	value = in_le32(FREG(controller, bit->reg));
	value = bit->polarity ? (value & ~bit->mask) : (value | bit->mask);
	out_le32(FREG(controller, bit->reg), value);
	(void)in_le32(FREG(controller, bit->reg));
	spin_unlock_irqrestore(&controller->lock, flags);
	
	return 0;
}

int
feature_clear(struct device_node* device, enum system_feature f)
{
	struct feature_controller*	controller;
	unsigned long			flags;
	unsigned long			value;
	fbit*				bit;

	if (f >= FEATURE_last)
		return -EINVAL;	

	controller = feature_lookup_controller(device);
	if (!controller)
		return -ENODEV;
	bit = &controller->bits[f];
	if (!bit->mask)
		return -EINVAL;
	
#ifdef DEBUG_FEATURE
	printk("feature: <%s> clearing feature %d in controller @0x%x\n",
		device->name, (int)f, (unsigned int)controller->reg);
#endif

	spin_lock_irqsave(&controller->lock, flags);
	value = in_le32(FREG(controller, bit->reg));
	value = bit->polarity ? (value | bit->mask) : (value & ~bit->mask);
	out_le32(FREG(controller, bit->reg), value);
	(void)in_le32(FREG(controller, bit->reg));
	spin_unlock_irqrestore(&controller->lock, flags);
	
	return 0;
}

int
feature_test(struct device_node* device, enum system_feature f)
{
	struct feature_controller*	controller;
	unsigned long			value;
	fbit*				bit;

	if (f >= FEATURE_last)
		return -EINVAL;	

	controller = feature_lookup_controller(device);
	if (!controller)
		return -ENODEV;
	bit = &controller->bits[f];
	if (!bit->mask)
		return -EINVAL;
	
#ifdef DEBUG_FEATURE
	printk("feature: <%s> clearing feature %d in controller @0x%x\n",
		device->name, (int)f, (unsigned int)controller->reg);
#endif
	/* If one feature contains several bits, all of them must be set
	 * for value to be true, or all of them must be 0 if polarity is
	 * inverse
	 */
	value = (in_le32(FREG(controller, bit->reg)) & bit->mask);
	return bit->polarity ? (value == 0) : (value == bit->mask);
}

int
feature_can_sleep(void)
{
	return ((board_features & FTR_CAN_SLEEP) != 0);	
}

/*
 * Core99 functions
 * 
 * Note: We currently assume there is _one_ UniN chip and _one_ KeyLargo
 *       chip, which is the case on all Core99 machines so far
 */

/* Only one GMAC is assumed */
void
feature_set_gmac_power(struct device_node* device, int power)
{
	unsigned long flags;
	
	if (!uninorth_base || !keylargo)
		return;
		
	/* TODO: Handle save/restore of PCI config space here
	 */

	spin_lock_irqsave(&keylargo->lock, flags);
	if (power)
		UN_BIS(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_GMAC);
	else
		UN_BIC(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_GMAC);
	spin_unlock_irqrestore(&keylargo->lock, flags);
	udelay(20);
}

void
feature_gmac_phy_reset(struct device_node* device)
{
	unsigned long flags;
	
	if (!keylargo_base || !keylargo)
		return;
		
	spin_lock_irqsave(&keylargo->lock, flags);
	out_8((volatile u8 *)KL_FCR(KL_GPIO_ETH_PHY_RESET),
		KEYLARGO_GPIO_OUTPUT_ENABLE);
	(void)in_8((volatile u8 *)KL_FCR(KL_GPIO_ETH_PHY_RESET));
	spin_unlock_irqrestore(&keylargo->lock, flags);
	mdelay(10);
	spin_lock_irqsave(&keylargo->lock, flags);
	out_8((volatile u8 *)KL_FCR(KL_GPIO_ETH_PHY_RESET),
		KEYLARGO_GPIO_OUTPUT_ENABLE | KEYLARGO_GPIO_OUTOUT_DATA);
	(void)in_8((volatile u8 *)KL_FCR(KL_GPIO_ETH_PHY_RESET));
	spin_unlock_irqrestore(&keylargo->lock, flags);
	mdelay(10);
}

/* Pass the node of the correct controller, please */
void
feature_set_usb_power(struct device_node* device, int power)
{
	char* prop;
	int number;
	u32 reg;
	
	unsigned long flags;
	
	if (!keylargo_base || !keylargo)
		return;
		
	prop = (char *)get_property(device, "AAPL,clock-id", NULL);
	if (!prop)
		return;
	if (strncmp(prop, "usb0u048", strlen("usb0u048")) == 0)
		number = 0;
	else if (strncmp(prop, "usb1u148", strlen("usb1u148")) == 0)
		number = 2;
	else
		return;

	spin_lock_irqsave(&keylargo->lock, flags);
	if (power) {
		/* Turn ON */
			
		if (number == 0) {
			KL_BIC(KEYLARGO_FCR0, (KL0_USB0_PAD_SUSPEND0 | KL0_USB0_PAD_SUSPEND1));
			mdelay(1);
			KL_BIS(KEYLARGO_FCR0, KL0_USB0_CELL_ENABLE);
		} else {
			KL_BIC(KEYLARGO_FCR0, (KL0_USB1_PAD_SUSPEND0 | KL0_USB1_PAD_SUSPEND1));
			mdelay(1);
			KL_BIS(KEYLARGO_FCR0, KL0_USB1_CELL_ENABLE);
		}
		reg = KL_IN(KEYLARGO_FCR4);
		reg &=	~(KL4_SET_PORT_ENABLE(number) | KL4_SET_PORT_RESUME(number) |
			KL4_SET_PORT_CONNECT(number) | KL4_SET_PORT_DISCONNECT(number));
		reg &=	~(KL4_SET_PORT_ENABLE(number+1) | KL4_SET_PORT_RESUME(number+1) |
			KL4_SET_PORT_CONNECT(number+1) | KL4_SET_PORT_DISCONNECT(number+1));
		KL_OUT(KEYLARGO_FCR4, reg);
		(void)KL_IN(KEYLARGO_FCR4);
		udelay(10);
	} else {
		/* Turn OFF */
		
		reg = KL_IN(KEYLARGO_FCR4);
		reg |=	KL4_SET_PORT_ENABLE(number) | KL4_SET_PORT_RESUME(number) |
			KL4_SET_PORT_CONNECT(number) | KL4_SET_PORT_DISCONNECT(number);
		reg |=	KL4_SET_PORT_ENABLE(number+1) | KL4_SET_PORT_RESUME(number+1) |
			KL4_SET_PORT_CONNECT(number+1) | KL4_SET_PORT_DISCONNECT(number+1);
		KL_OUT(KEYLARGO_FCR4, reg);
		(void)KL_IN(KEYLARGO_FCR4);
		udelay(1);
		if (number == 0) {
			KL_BIC(KEYLARGO_FCR0, KL0_USB0_CELL_ENABLE);
			(void)KL_IN(KEYLARGO_FCR0);
			udelay(1);
			KL_BIS(KEYLARGO_FCR0, (KL0_USB0_PAD_SUSPEND0 | KL0_USB0_PAD_SUSPEND1));
			(void)KL_IN(KEYLARGO_FCR0);
		} else {
			KL_BIC(KEYLARGO_FCR0, KL0_USB1_CELL_ENABLE);
			(void)KL_IN(KEYLARGO_FCR0);
			udelay(1);
			KL_BIS(KEYLARGO_FCR0, (KL0_USB1_PAD_SUSPEND0 | KL0_USB1_PAD_SUSPEND1));
			(void)KL_IN(KEYLARGO_FCR0);
		}
		udelay(1);
	}
	spin_unlock_irqrestore(&keylargo->lock, flags);
}

void 
feature_set_firewire_power(struct device_node* device, int power)
{
	unsigned long flags;

	/* TODO: Handle save/restore of PCI config space here
	 */
		
	if (!uninorth_base)
		return;
	spin_lock_irqsave(&keylargo->lock, flags);
	if (power)
		UN_BIS(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_FW);
	else
		UN_BIC(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_FW);
	udelay(20);
	spin_unlock_irqrestore(&keylargo->lock, flags);
}

/* Warning: will kill the PHY.. */
void 
feature_set_firewire_cable_power(struct device_node* device, int power)
{
	unsigned long flags;
	u8 gpioValue = power ? 0 : 4;
	
	if (!keylargo_base || !(board_features & FTR_HAS_FW_POWER))
		return;
	spin_lock_irqsave(&keylargo->lock, flags);
	out_8((volatile u8 *)KL_FCR(KL_GPIO_FW_CABLE_POWER), gpioValue);
	(void)in_8((volatile u8 *)KL_FCR(KL_GPIO_FW_CABLE_POWER));
	spin_unlock_irqrestore(&keylargo->lock, flags);		
}

#ifdef CONFIG_SMP
void
feature_core99_kick_cpu(int cpu_nr)
{
#if 1 /* New way */
	const int reset_lines[] = {	KL_GPIO_RESET_CPU0,
					KL_GPIO_RESET_CPU1,
					KL_GPIO_RESET_CPU2,
					KL_GPIO_RESET_CPU3 };
	volatile u8* reset_io;

	if (!keylargo_base || cpu_nr > 3)
		return;
	reset_io = (volatile u8*)KL_FCR(reset_lines[cpu_nr]);
	out_8(reset_io, KEYLARGO_GPIO_OUTPUT_ENABLE);
	udelay(1);
	out_8(reset_io, KEYLARGO_GPIO_OUTPUT_ENABLE | KEYLARGO_GPIO_OUTOUT_DATA);
#else
	out_8((volatile u8 *)KL_FCR(KL_GPIO_EXTINT_CPU1), KL_GPIO_EXTINT_CPU1_ASSERT);
	udelay(1);
	out_8((volatile u8 *)KL_FCR(KL_GPIO_EXTINT_CPU1), KL_GPIO_EXTINT_CPU1_RELEASE);
#endif	
}
#endif /* CONFIG_SMP */

void
feature_set_airport_power(struct device_node* device, int power)
{
	if (!keylargo_base || !airport_dev || airport_dev != device)
		return;
	if (airport_pwr_state == power)
		return;
	if (power) {
		/* Some if this is from Darwin code, some is from tracing of
		 * MacOS driver with Macsbug. Some real bit definitions would
		 * really help here...
		 */
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_0), airport_pwr_regs[0]);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_1), airport_pwr_regs[1]);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_2), airport_pwr_regs[2]);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_3), airport_pwr_regs[3]);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_4), airport_pwr_regs[4]);
		udelay(20);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_4), 5);
		udelay(20);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_4), 4);
		mdelay(20);
		KL_BIC(KEYLARGO_FCR2, KL2_AIRPORT_RESET_N);
		udelay(10);
		out_8((volatile u8*)KL_FCR(0x1a3e0), 0x41);
		udelay(10);
		KL_BIS(KEYLARGO_FCR2, KL2_AIRPORT_RESET_N);
		udelay(10);
	} else {
		airport_pwr_regs[0] = in_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_0));
		airport_pwr_regs[1] = in_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_1));
		airport_pwr_regs[2] = in_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_2));
		airport_pwr_regs[3] = in_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_3));
		airport_pwr_regs[4] = in_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_4));
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_0), 0);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_1), 0);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_2), 0);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_3), 0);
		out_8((volatile u8*)KL_FCR(KL_GPIO_AIRPORT_4), 0);
	}
	airport_pwr_state = power;	
}

/* Initialize the Core99 UniNorth host bridge and memory controller
 */
static void
uninorth_init(void)
{
	struct device_node* gmac, *fw;
	unsigned long actrl;
	
	/* Set the arbitrer QAck delay according to what Apple does
	 */
	if (uninorth_rev < 0x10) {
		actrl = UN_IN(UNI_N_ARB_CTRL) & ~UNI_N_ARB_CTRL_QACK_DELAY_MASK;
		actrl |= ((uninorth_rev < 3) ? UNI_N_ARB_CTRL_QACK_DELAY105 :
			UNI_N_ARB_CTRL_QACK_DELAY) << UNI_N_ARB_CTRL_QACK_DELAY_SHIFT;
		UN_OUT(UNI_N_ARB_CTRL, actrl);
	}
	
	/* Enable GMAC for now for PCI probing. It will be disabled
	 * later on after PCI probe
	 */
	gmac = find_devices("ethernet");
	while(gmac) {
		if (device_is_compatible(gmac, "gmac"))
			break;
		gmac = gmac->next;
	}
	if (gmac)
		feature_set_gmac_power(gmac, 1);

	/* Enable FW before PCI probe. Will be disabled later on
	 */
	fw = find_devices("firewire");
	if (fw && device_is_compatible(fw, "pci106b,18"))
		feature_set_firewire_power(fw, 1);
}

/* Initialize the Core99 KeyLargo ASIC. 
 */
static void
keylargo_init(void)
{
	struct device_node* np;
	
	KL_BIS(KEYLARGO_FCR2, KL2_MPIC_ENABLE);

	/* Lookup for an airport card, and disable it if found
	 * to save power (will be re-enabled by driver if used)
	 */
	np = find_devices("radio");
	if (np && np->parent == keylargo->device)
		airport_dev = np;

	if (airport_dev) {
		airport_pwr_state = 1;
		feature_set_airport_power(airport_dev, 0);
	}
	
}

#ifdef CONFIG_PMAC_PBOOK
void
feature_prepare_for_sleep(void)
{
	/* We assume gatwick is second */
	struct feature_controller* ctrler = &controllers[0];

	if (controller_count > 1 &&
		device_is_compatible(ctrler->device, "gatwick"))
		ctrler = &controllers[1];

	if (ctrler->bits == feature_bits_heathrow ||
		ctrler->bits == feature_bits_paddington) {
		heathrow_prepare_for_sleep(ctrler);
		return;
	}
	if (ctrler->bits == feature_bits_keylargo) {
		core99_prepare_for_sleep(ctrler);
		return;
	}
}


void
feature_wake_up(void)
{
	struct feature_controller* ctrler = &controllers[0];

	if (controller_count > 1 &&
		device_is_compatible(ctrler->device, "gatwick"))
		ctrler = &controllers[1];
	
	if (ctrler->bits == feature_bits_heathrow ||
		ctrler->bits == feature_bits_paddington) {
		heathrow_wakeup(ctrler);
		return;
	}
	if (ctrler->bits == feature_bits_keylargo) {
		core99_wake_up(ctrler);
		return;
	}
}

static u32 save_fcr[5];
static u32 save_mbcr;
static u32 save_gpio_levels[2];
static u8 save_gpio_extint[KEYLARGO_GPIO_EXTINT_CNT];
static u8 save_gpio_normal[KEYLARGO_GPIO_CNT];
static u32 save_unin_clock_ctl;
static struct dbdma_regs save_dbdma[13];


static void
heathrow_prepare_for_sleep(struct feature_controller* ctrler)
{
	save_mbcr = in_le32(FREG(ctrler, 0x34));
	save_fcr[0] = in_le32(FREG(ctrler, 0x38));
	save_fcr[1] = in_le32(FREG(ctrler, 0x3c));

	out_le32(FREG(ctrler, 0x38), save_fcr[0] & ~HRW_IOBUS_ENABLE);
}

static void
heathrow_wakeup(struct feature_controller* ctrler)
{
	out_le32(FREG(ctrler, 0x38), save_fcr[0]);
	out_le32(FREG(ctrler, 0x3c), save_fcr[1]);
	out_le32(FREG(ctrler, 0x34), save_mbcr);
	mdelay(1);
	out_le32(FREG(ctrler, 0x38), save_fcr[0] | HRW_IOBUS_ENABLE);
	mdelay(1);
}

static void
turn_off_keylargo(void)
{
	u32 temp;

	/* For now, suspending the USB ref cause the machine to die on
	 * wakeup -- BenH
	 */
#if 0
	mdelay(1);
	KL_BIS(KEYLARGO_FCR0, KL0_USB_REF_SUSPEND);
	(void)KL_IN(KEYLARGO_FCR0);
	mdelay(1500);
#endif

	KL_BIC(KEYLARGO_FCR0,	KL0_SCCA_ENABLE | KL0_SCCB_ENABLE |
				KL0_SCC_CELL_ENABLE |
		      		KL0_IRDA_ENABLE | KL0_IRDA_CLK32_ENABLE |
		      		KL0_IRDA_CLK19_ENABLE);

	(void)KL_IN(KEYLARGO_FCR0); udelay(10);
	KL_BIS(KEYLARGO_MBCR, KL_MBCR_MB0_DEV_ENABLE);
	(void)KL_IN(KEYLARGO_MBCR); udelay(10);

	KL_BIC(KEYLARGO_FCR1,
		KL1_AUDIO_SEL_22MCLK | KL1_AUDIO_CLK_ENABLE_BIT |
		KL1_AUDIO_CLK_OUT_ENABLE | KL1_AUDIO_CELL_ENABLE |
		KL1_I2S0_CELL_ENABLE | KL1_I2S0_CLK_ENABLE_BIT |
		KL1_I2S0_ENABLE | KL1_I2S1_CELL_ENABLE |
		KL1_I2S1_CLK_ENABLE_BIT | KL1_I2S1_ENABLE |
		KL1_EIDE0_ENABLE | KL1_EIDE0_RESET_N |
		KL1_EIDE1_ENABLE | KL1_EIDE1_RESET_N |
		KL1_UIDE_ENABLE);
	(void)KL_IN(KEYLARGO_FCR1); udelay(10);

	KL_BIS(KEYLARGO_FCR2, KL2_MODEM_POWER_N);
 	udelay(10);
 	KL_BIC(KEYLARGO_FCR2, KL2_IOBUS_ENABLE);
 	udelay(10);
	temp = KL_IN(KEYLARGO_FCR3);
	if (keylargo_rev >= 2)
		temp |= (KL3_SHUTDOWN_PLL2X | KL3_SHUTDOWN_PLL_TOTAL);
		
	temp |= KL3_SHUTDOWN_PLLKW6 | KL3_SHUTDOWN_PLLKW4 |
		KL3_SHUTDOWN_PLLKW35 | KL3_SHUTDOWN_PLLKW12;
	temp &= ~(KL3_CLK66_ENABLE | KL3_CLK49_ENABLE | KL3_CLK45_ENABLE
		| KL3_CLK31_ENABLE | KL3_TIMER_CLK18_ENABLE | KL3_I2S1_CLK18_ENABLE
		| KL3_I2S0_CLK18_ENABLE | KL3_VIA_CLK16_ENABLE);
	KL_OUT(KEYLARGO_FCR3, temp);
	(void)KL_IN(KEYLARGO_FCR3); udelay(10);
}

static void
turn_off_pangea(void)
{
	u32 temp;

	KL_BIC(KEYLARGO_FCR0,	KL0_SCCA_ENABLE | KL0_SCCB_ENABLE |
				KL0_SCC_CELL_ENABLE |
				KL0_USB0_CELL_ENABLE | KL0_USB1_CELL_ENABLE);

	(void)KL_IN(KEYLARGO_FCR0); udelay(10);
	KL_BIS(KEYLARGO_MBCR, KL_MBCR_MB0_DEV_ENABLE);
	(void)KL_IN(KEYLARGO_MBCR); udelay(10);

	KL_BIC(KEYLARGO_FCR1,
		KL1_AUDIO_SEL_22MCLK | KL1_AUDIO_CLK_ENABLE_BIT |
		KL1_AUDIO_CLK_OUT_ENABLE | KL1_AUDIO_CELL_ENABLE |
		KL1_I2S0_CELL_ENABLE | KL1_I2S0_CLK_ENABLE_BIT |
		KL1_I2S0_ENABLE | KL1_I2S1_CELL_ENABLE |
		KL1_I2S1_CLK_ENABLE_BIT | KL1_I2S1_ENABLE |
		KL1_UIDE_ENABLE);
	(void)KL_IN(KEYLARGO_FCR1); udelay(10);

	KL_BIS(KEYLARGO_FCR2, KL2_MODEM_POWER_N);
 	udelay(10);
	temp = KL_IN(KEYLARGO_FCR3);
	temp |= KL3_SHUTDOWN_PLLKW6 | KL3_SHUTDOWN_PLLKW4 |
		KL3_SHUTDOWN_PLLKW35;
	temp &= ~(KL3_CLK49_ENABLE | KL3_CLK45_ENABLE
		| KL3_CLK31_ENABLE | KL3_TIMER_CLK18_ENABLE | KL3_I2S1_CLK18_ENABLE
		| KL3_I2S0_CLK18_ENABLE | KL3_VIA_CLK16_ENABLE);
	KL_OUT(KEYLARGO_FCR3, temp);
	(void)KL_IN(KEYLARGO_FCR3); udelay(10);
}

static void
core99_prepare_for_sleep(struct feature_controller* ctrler)
{
	int i;
	u8* base8;
	
	/*
	 * Save various bits of KeyLargo
	 */

	/* We power off the wireless slot in case it was not done
	 * by the driver. We don't power it on automatically however
	 */
	feature_set_airport_power(airport_dev, 0);

	/* We power off the FW cable. Should be done by the driver... */
	feature_set_firewire_cable_power(NULL, 0);
	 
	/* Save the state of the various GPIOs */
	save_gpio_levels[0] = KL_IN(KEYLARGO_GPIO_LEVELS0);
	save_gpio_levels[1] = KL_IN(KEYLARGO_GPIO_LEVELS1);
	base8 = (u8 *)KL_FCR(KEYLARGO_GPIO_EXTINT_0);
	for (i=0; i<KEYLARGO_GPIO_EXTINT_CNT; i++)
		save_gpio_extint[i] = in_8(base8+i);
	base8 = (u8 *)KL_FCR(KEYLARGO_GPIO_0);
	for (i=0; i<KEYLARGO_GPIO_CNT; i++)
		save_gpio_normal[i] = in_8(base8+i);

	/* Save the FCRs */
	save_mbcr = KL_IN(KEYLARGO_MBCR);
	save_fcr[0] = KL_IN(KEYLARGO_FCR0);
	save_fcr[1] = KL_IN(KEYLARGO_FCR1);
	save_fcr[2] = KL_IN(KEYLARGO_FCR2);
	save_fcr[3] = KL_IN(KEYLARGO_FCR3);
	save_fcr[4] = KL_IN(KEYLARGO_FCR4);

	/* Save state & config of DBDMA channels */
	for (i=0; i<13; i++) {
		volatile struct dbdma_regs* chan = (volatile struct dbdma_regs*)
			(keylargo_base + ((0x8000+i*0x100)>>2));
		save_dbdma[i].cmdptr_hi = in_le32(&chan->cmdptr_hi);
		save_dbdma[i].cmdptr = in_le32(&chan->cmdptr);
		save_dbdma[i].intr_sel = in_le32(&chan->intr_sel);
		save_dbdma[i].br_sel = in_le32(&chan->br_sel);
		save_dbdma[i].wait_sel = in_le32(&chan->wait_sel);
	}

	/*
	 * Turn off as much as we can
	 */
	if (keylargo_rev & KL_PANGEA_REV)
		turn_off_pangea();
	else
		turn_off_keylargo();
	
	/* 
	 * Put the host bridge to sleep
	 */

	save_unin_clock_ctl = UN_IN(UNI_N_CLOCK_CNTL);
	UN_OUT(UNI_N_CLOCK_CNTL, save_unin_clock_ctl &
		~(UNI_N_CLOCK_CNTL_GMAC|UNI_N_CLOCK_CNTL_FW/*|UNI_N_CLOCK_CNTL_PCI*/));
	udelay(100);
	UN_OUT(UNI_N_HWINIT_STATE, UNI_N_HWINIT_STATE_SLEEPING);
	UN_OUT(UNI_N_POWER_MGT, UNI_N_POWER_MGT_SLEEP);

	/*
	 * FIXME: A bit of black magic with OpenPIC (don't ask me why)
	 */
	if (board_features & FTR_NEED_OPENPIC_TWEAK) {
		KL_BIS(0x506e0, 0x00400000);
		KL_BIS(0x506e0, 0x80000000);
	}
}

static void
core99_wake_up(struct feature_controller* ctrler)
{
	int i;
	u8* base8;

	/*
	 * Wakeup the host bridge
	 */
	UN_OUT(UNI_N_POWER_MGT, UNI_N_POWER_MGT_NORMAL);
	udelay(10);
	UN_OUT(UNI_N_HWINIT_STATE, UNI_N_HWINIT_STATE_RUNNING);
	udelay(10);
	
	/*
	 * Restore KeyLargo
	 */
	 
	KL_OUT(KEYLARGO_MBCR, save_mbcr);
	(void)KL_IN(KEYLARGO_MBCR); udelay(10);
	KL_OUT(KEYLARGO_FCR0, save_fcr[0]);
	(void)KL_IN(KEYLARGO_FCR0); udelay(10);
	KL_OUT(KEYLARGO_FCR1, save_fcr[1]);
	(void)KL_IN(KEYLARGO_FCR1); udelay(10);
	KL_OUT(KEYLARGO_FCR2, save_fcr[2]);
	(void)KL_IN(KEYLARGO_FCR2); udelay(10);
	KL_OUT(KEYLARGO_FCR3, save_fcr[3]);
	(void)KL_IN(KEYLARGO_FCR3); udelay(10);
	KL_OUT(KEYLARGO_FCR4, save_fcr[4]);
	(void)KL_IN(KEYLARGO_FCR4); udelay(10);

	for (i=0; i<13; i++) {
		volatile struct dbdma_regs* chan = (volatile struct dbdma_regs*)
			(keylargo_base + ((0x8000+i*0x100)>>2));
		out_le32(&chan->control, (ACTIVE|DEAD|WAKE|FLUSH|PAUSE|RUN)<<16);
		while (in_le32(&chan->status) & ACTIVE)
			mb();
		out_le32(&chan->cmdptr_hi, save_dbdma[i].cmdptr_hi);
		out_le32(&chan->cmdptr, save_dbdma[i].cmdptr);
		out_le32(&chan->intr_sel, save_dbdma[i].intr_sel);
		out_le32(&chan->br_sel, save_dbdma[i].br_sel);
		out_le32(&chan->wait_sel, save_dbdma[i].wait_sel);
	}

	KL_OUT(KEYLARGO_GPIO_LEVELS0, save_gpio_levels[0]);
	KL_OUT(KEYLARGO_GPIO_LEVELS1, save_gpio_levels[1]);
	base8 = (u8 *)KL_FCR(KEYLARGO_GPIO_EXTINT_0);
	for (i=0; i<KEYLARGO_GPIO_EXTINT_CNT; i++)
		out_8(base8+i, save_gpio_extint[i]);
	base8 = (u8 *)KL_FCR(KEYLARGO_GPIO_0);
	for (i=0; i<KEYLARGO_GPIO_CNT; i++)
		out_8(base8+i, save_gpio_normal[i]);

	/* FIXME more black magic with OpenPIC ... */
	if (board_features & FTR_NEED_OPENPIC_TWEAK) {
		KL_BIC(0x506e0, 0x00400000);
		KL_BIC(0x506e0, 0x80000000);
	}

	UN_OUT(UNI_N_CLOCK_CNTL, save_unin_clock_ctl);
	udelay(100);
}
#endif /* CONFIG_PMAC_PBOOK */
