#ifndef __MACIO_ASIC_H__
#define __MACIO_ASIC_H__

#include <asm/of_device.h>

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
 *
 * The pdev field can be NULL on non-PCI machines
 */
struct macio_bus
{
	struct macio_chip	*chip;		/* macio_chip (private use) */
	int			index;		/* macio chip index in system */
#ifdef CONFIG_PCI
	struct pci_dev		*pdev;		/* PCI device hosting this bus */
#endif
};

/*
 * the macio_dev structure is used to describe a device
 * within an Apple MacIO ASIC.
 */
struct macio_dev
{
	struct macio_bus	*bus;		/* macio bus this device is on */
	struct macio_dev	*media_bay;	/* Device is part of a media bay */
	struct of_device	ofdev;
};
#define	to_macio_device(d) container_of(d, struct macio_dev, ofdev.dev)
#define	of_to_macio_device(d) container_of(d, struct macio_dev, ofdev)

extern struct macio_dev *macio_dev_get(struct macio_dev *dev);
extern void macio_dev_put(struct macio_dev *dev);

/*
 * A driver for a mac-io chip based device
 */
struct macio_driver
{
	char			*name;
	struct of_match		*match_table;
	struct module		*owner;

	int	(*probe)(struct macio_dev* dev, const struct of_match *match);
	int	(*remove)(struct macio_dev* dev);

	int	(*suspend)(struct macio_dev* dev, u32 state);
	int	(*resume)(struct macio_dev* dev);
	int	(*shutdown)(struct macio_dev* dev);

	struct device_driver	driver;
};
#define	to_macio_driver(drv) container_of(drv,struct macio_driver, driver)

extern int macio_register_driver(struct macio_driver *);
extern void macio_unregister_driver(struct macio_driver *);

#endif /* __MACIO_ASIC_H__ */
