#ifndef __MACIO_ASIC_H__
#define __MACIO_ASIC_H__

#include <linux/device.h>

extern struct bus_type macio_bus_type;

/* MacIO device driver is defined later */
struct macio_driver;
struct macio_chip;

#define MACIO_DEV_COUNT_RESOURCE	8
#define MACIO_DEV_COUNT_IRQS		8

/*
 * the macio_bus structure is used to describe a "virtual" bus
 * within a MacIO ASIC. It's typically provided by a macio_pci_asic
 * PCI device, but could be provided differently as well (nubus
 * machines using a fake OF tree).
 */
struct macio_bus
{
	struct macio_chip	*chip;		/* macio_chip (private use) */
	struct pci_dev		*pdev;		/* PCI device hosting this bus */
	struct list_head	devices;	/* list of devices on this bus */
};

/*
 * the macio_dev structure is used to describe a device
 * within an Apple MacIO ASIC.
 */
struct macio_dev
{
	struct macio_bus	*bus;		/* virtual bus this device is on */

	struct device_node	*node;		/* OF node */	
	struct macio_driver	*driver;	/* which driver allocated this device */
	void			*driver_data;	/* placeholder for driver specific stuffs */
	struct resource		resources[MACIO_DEV_COUNT_RESOURCE]; /* I/O */
	int			irqs[MACIO_DEV_COUNT_IRQS];
	
	struct device		dev;		/* Generic device interface */
};
#define	to_macio_device(d) container_of(d, struct macio_dev, dev)

/*
 * Struct used for matching a device
 */
struct macio_match
{
	char	*name;
	char	*type;
	char	*compatible;
};
#define MACIO_ANY_MATCH		((char *)-1L)

/*
 * A driver for a mac-io chip based device
 */
struct macio_driver
{
	struct list_head	node;
	char			*name;
	struct macio_match	*match_table;

	int	(*probe)(struct macio_dev* dev, const struct macio_match *match);
	int	(*remove)(struct macio_dev* dev);

	int	(*suspend)(struct macio_dev* dev, u32 state, u32 level);
	int	(*resume)(struct macio_dev* dev, u32 level);
	int	(*shutdown)(struct macio_dev* dev);

	struct device_driver	driver;
};
#define	to_macio_driver(drv) container_of(drv,struct macio_driver, driver)

extern int macio_register_driver(struct macio_driver *);
extern void macio_unregister_driver(struct macio_driver *);

#endif /* __MACIO_ASIC_H__ */
