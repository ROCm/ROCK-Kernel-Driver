/*
 *  arch/ppc/kernel/feature.c
 *
 *  Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *                     Ben. Herrenschmidt (bh40@calva.net)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  BenH: Changed implementation to work on multiple registers
 * 	  polarity is also taken into account. Removed delay (now
 * 	  responsibility of the caller). Added spinlocks.
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

#undef DEBUG_FEATURE

#define MAX_FEATURE_CONTROLLERS		2
#define MAX_FEATURE_OFFSET		0x100
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

/* I don't have an OHare machine to test with, so I left those as they
 * were. Someone with such a machine chould check out what OF says and
 * try too see if they match the heathrow ones and should be changed too
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
 * Mostly the same as the heathrow.
 */
static fbit feature_bits_paddington[] = {
	{0x38,0,0},			/* FEATURE_null */
	{0x38,0,0},			/* FEATURE_Serial_reset */
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
 */
static fbit feature_bits_keylargo[] = {
	{0x38,0,0},			/* FEATURE_null */
	{0x38,0,KL0_SCC_RESET},		/* FEATURE_Serial_reset */
	{0x38,0,KL0_SERIAL_ENABLE},	/* FEATURE_Serial_enable */
	{0x38,0,KL0_SCC_A_INTF_ENABLE},	/* FEATURE_Serial_IO_A */
	{0x38,0,KL0_SCC_B_INTF_ENABLE},	/* FEATURE_Serial_IO_B */
	{0x38,0,0},			/* FEATURE_SWIM3_enable */
	{0x38,0,0},			/* FEATURE_MESH_enable */
	{0x3c,0,0},			/* FEATURE_IDE0_enable */
 	{0x3c,1,KL1_EIDE0_RESET_N},	/* FEATURE_IDE0_reset */
	{0x38,0,0},			/* FEATURE_IOBUS_enable */
	{0x34,1,0x00000200},		/* FEATURE_Mediabay_reset */
	{0x34,1,0x00000400},		/* FEATURE_Mediabay_power */
	{0x38,0,0},			/* FEATURE_Mediabay_PCI_enable */
	{0x3c,0,0x0},			/* FEATURE_IDE1_enable */
	{0x3c,1,KL1_EIDE1_RESET_N},	/* FEATURE_IDE1_reset */
	{0x38,0,0},			/* FEATURE_Mediabay_floppy_enable */
	{0x38,0,0},			/* FEATURE_BMac_reset */
	{0x38,0,0},			/* FEATURE_BMac_IO_enable */
	{0x40,1,KL2_MODEM_POWER_N},	/* FEATURE_Modem_power */
	{0x38,0,0},			/* FEATURE_Slow_SCC_PCLK */
	{0x38,0,0},			/* FEATURE_Sound_Power */
	{0x38,0,0},			/* FEATURE_Sound_CLK_Enable */
	{0x38,0,0},			/* FEATURE_IDE2_enable */
	{0x3c,1,KL1_UIDE_RESET_N},	/* FEATURE_IDE2_reset */
	{0x34,0,KL_MBCR_MBDEV_ENABLE},	/* FEATURE_Mediabay_IDE_switch */
	{0x34,0,0x00000100},		/* FEATURE_Mediabay_content */
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

static void heathrow_prepare_for_sleep(struct feature_controller* ctrler);
static void heathrow_wakeup(struct feature_controller* ctrler);
static void keylargo_init(void);
static void uninorth_init(void);
static void core99_prepare_for_sleep(struct feature_controller* ctrler);
static void core99_wake_up(struct feature_controller* ctrler);

/* static variables */
static struct feature_controller	controllers[MAX_FEATURE_CONTROLLERS];
static int				controller_count = 0;

/* Core99 stuffs */
static volatile u32*			uninorth_base = NULL;
static volatile u32*			keylargo_base = NULL;
static int				uninorth_rev;
static int				keylargo_rev;

void
feature_init(void)
{
	struct device_node *np;
	u32 *rev;
	
	if (_machine != _MACH_Pmac)
		return;

	np = find_devices("mac-io");
	while (np != NULL) {
		/* KeyLargo contains several (5 ?) FCR registers in mac-io,
		 * plus some gpio's which could eventually be handled here.
		 */
		if (device_is_compatible(np, "Keylargo")) {
			struct feature_controller* ctrler =
				feature_add_controller(np, feature_bits_keylargo);
			if (ctrler) {
				keylargo_base = ctrler->reg;
				rev = (u32 *)get_property(ctrler->device, "revision-id", NULL);
				if (rev)
					keylargo_rev = *rev;
			}
		} else if (device_is_compatible(np, "paddington")) {
			feature_add_controller(np, feature_bits_paddington);
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

	/* Handle core99 Uni-N */
	np = find_devices("uni-n");
	if (np && np->n_addrs > 0) {
		uninorth_base = ioremap(np->addrs[0].address, 0x1000);
		uninorth_rev = in_be32(UN_REG(UNI_N_VERSION));
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

	controller->reg		= (volatile u32 *)ioremap(
		controller_device->addrs[0].address, MAX_FEATURE_OFFSET);

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
	if (!uninorth_base)
		return;
	if (power)
		UN_BIS(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_GMAC);
	else
		UN_BIC(UNI_N_CLOCK_CNTL, UNI_N_CLOCK_CNTL_GMAC);
	udelay(20);
}

void
feature_set_gmac_phy_reset(struct device_node* device, int reset)
{
	if (!keylargo_base)
		return;
	out_8((volatile u8 *)KL_FCR(KL_GPIO_ETH_PHY_RESET), reset);
	(void)in_8((volatile u8 *)KL_FCR(KL_GPIO_ETH_PHY_RESET));
}

/* Pass the node of the correct controller, please */
void
feature_set_usb_power(struct device_node* device, int power)
{
}

/* Not yet implemented */
void 
feature_set_firewire_power(struct device_node* device, int power)
{
}

/* Initialize the Core99 UniNorth host bridge and memory controller
 */
static void
uninorth_init(void)
{
	struct device_node* gmac;
	unsigned long actrl;
	
	/* Set the arbitrer QAck delay according to what Apple does
	 */
	actrl = in_be32(UN_REG(UNI_N_ARB_CTRL)) & ~UNI_N_ARB_CTRL_QACK_DELAY_MASK;
	actrl |= ((uninorth_rev < 3) ? UNI_N_ARB_CTRL_QACK_DELAY105 : UNI_N_ARB_CTRL_QACK_DELAY)
		<< UNI_N_ARB_CTRL_QACK_DELAY_SHIFT;
	UN_OUT(UNI_N_ARB_CTRL, actrl);
	
	/* 
	 * Turns OFF the gmac clock. The gmac driver will turn
	 * it back ON when the interface is enabled. This save
	 * power on portables.
	 * 
	 * Note: We could also try to turn OFF the PHY. Since this
	 * has to be done by both the gmac driver and this code,
	 * I'll probably end-up moving some of this out of the
	 * modular gmac driver into a non-modular stub containing
	 * some basic PHY management and power management stuffs
	 */
	gmac = find_devices("ethernet");

	while(gmac) {
		if (device_is_compatible(gmac, "gmac"))
			break;
		gmac = gmac->next;
	}
	if (gmac)
		feature_set_gmac_power(gmac, 0);
}

/* Initialize the Core99 KeyLargo ASIC. Currently, we just make sure
 * OpenPIC is enabled
 */
static void
keylargo_init(void)
{
	KL_BIS(KEYLARGO_FCR2, KL2_MPIC_ENABLE);
}

#ifdef CONFIG_PMAC_PBOOK
void
feature_prepare_for_sleep(void)
{
	/* We assume gatwick is second */
	struct feature_controller* ctrler = &controllers[0];

	if (!ctrler)
		return;
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

	if (!ctrler)
		return;
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
core99_prepare_for_sleep(struct feature_controller* ctrler)
{
	/* Not yet implemented */
}

static void
core99_wake_up(struct feature_controller* ctrler)
{
	/* Not yet implemented */
}
#endif /* CONFIG_PMAC_PBOOK */
