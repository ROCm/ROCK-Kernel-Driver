/*
 * Definitions for accessing the Feature Control Register (FCR)
 * on Power Macintoshes and similar machines.  The FCR lets us
 * enable/disable, reset, and power up/down various peripherals.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Paul Mackerras &
 *                    Ben. Herrenschmidt.
 *
 * 
 */
#ifdef __KERNEL__
#ifndef __ASM_PPC_FEATURE_H
#define __ASM_PPC_FEATURE_H

/*
 * The FCR selector for particular features vary somewhat between
 * different machines.  So we abstract a list of features here
 * and let the feature_* routines map them to the actual bits.
 */
enum system_feature {
	FEATURE_null,
	FEATURE_Serial_reset,
	FEATURE_Serial_enable,
	FEATURE_Serial_IO_A,
	FEATURE_Serial_IO_B,
	FEATURE_SWIM3_enable,
	FEATURE_MESH_enable,
	FEATURE_IDE0_enable,		/* Internal IDE */
	FEATURE_IDE0_reset,		/* Internal IDE */
	FEATURE_IOBUS_enable,		/* Internal IDE */
	FEATURE_Mediabay_reset,
	FEATURE_Mediabay_power,
	FEATURE_Mediabay_PCI_enable,
	FEATURE_IDE1_enable,		/* MediaBay IDE */
	FEATURE_IDE1_reset,		/* MediaBay IDE */
	FEATURE_Mediabay_floppy_enable,
	FEATURE_BMac_reset,
	FEATURE_BMac_IO_enable,
	FEATURE_Modem_power,
	FEATURE_Slow_SCC_PCLK,
	FEATURE_Sound_power,
	FEATURE_Sound_CLK_enable,
	FEATURE_IDE2_enable,
	FEATURE_IDE2_reset,
	FEATURE_Mediabay_IDE_switch,	/* MB IDE bus switch */
	FEATURE_Mediabay_content,	/* MB content indicator enable */
	FEATURE_Airport_reset,		/* Is it actually a reset ? */
	FEATURE_last,
};

/* Note about the device parameter: Each device gives it's own entry. If NULL,
   the feature function will just do nothing and return -EINVAL.
   The feature management will walk up the device tree until in reaches a recognized
   chip for which features can be changed and it will then apply the necessary
   features to that chip. If it's not found, -ENODEV is returned.
   Note also that feature_test/set/clear are interrupt-safe provided that they are
   called _after_ feature_init() is completed.
 */

/* Test whether a particular feature is enabled. May return -ENODEV*/
extern int	feature_test(struct device_node* device, enum system_feature f);

/* Set a particular feature. Returns 0 or -ENODEV */
extern int	feature_set(struct device_node* device, enum system_feature f);

/* Clear a particular feature */
extern int	feature_clear(struct device_node* device, enum system_feature f);

/* Initialize feature stuff */
extern void	feature_init(void);


/*
 * Additional functions related to Core99 machines
 */
extern void	feature_set_gmac_power(struct device_node* device, int power);

	/* use constants in KeyLargo.h for the reset parameter */
extern void	feature_set_gmac_phy_reset(struct device_node* device, int reset);

extern void	feature_set_usb_power(struct device_node* device, int power);

extern void 	feature_set_firewire_power(struct device_node* device, int power);

/*
 * Sleep related functions. At term, they should be high-priority notifiers
 */
extern void	feature_prepare_for_sleep(void);

extern void	feature_wake_up(void);

#endif /* __ASM_PPC_FEATURE_H */
#endif /* __KERNEL__ */
