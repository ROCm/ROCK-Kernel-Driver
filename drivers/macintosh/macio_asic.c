/*
 * Bus & driver management routines for devices within
 * a MacIO ASIC. Interface to new driver model mostly
 * stolen from the PCI version.
 * 
 * TODO:
 * 
 *  - Don't probe below media bay by default, but instead provide
 *    some hooks for media bay to dynamically add/remove it's own
 *    sub-devices.
 */
 
#include <linux/config.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/machdep.h>
#include <asm/macio.h>
#include <asm/pmac_feature.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

static struct macio_chip      *macio_on_hold;

static int
macio_bus_match(struct device *dev, struct device_driver *drv) 
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * macio_drv = to_macio_driver(drv);
	const struct of_match * matches = macio_drv->match_table;

	if (!matches) 
		return 0;

	return of_match_device(matches, &macio_dev->ofdev) != NULL;
}

struct macio_dev *macio_dev_get(struct macio_dev *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;
	tmp = get_device(&dev->ofdev.dev);
	if (tmp)
		return to_macio_device(tmp);
	else
		return NULL;
}

void macio_dev_put(struct macio_dev *dev)
{
	if (dev)
		put_device(&dev->ofdev.dev);
}


static int macio_device_probe(struct device *dev)
{
	int error = -ENODEV;
	struct macio_driver *drv;
	struct macio_dev *macio_dev;
	const struct of_match *match;

	drv = to_macio_driver(dev->driver);
	macio_dev = to_macio_device(dev);

	if (!drv->probe)
		return error;

	macio_dev_get(macio_dev);

	match = of_match_device(drv->match_table, &macio_dev->ofdev);
	if (match)
		error = drv->probe(macio_dev, match);
	if (error)
		macio_dev_put(macio_dev);

	return error;
}

static int macio_device_remove(struct device *dev)
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * drv = to_macio_driver(macio_dev->ofdev.dev.driver);

	if (drv && drv->remove)
		drv->remove(macio_dev);
	macio_dev_put(macio_dev);

	return 0;
}

static int macio_device_suspend(struct device *dev, u32 state)
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * drv;
	int error = 0;

	if (macio_dev->ofdev.dev.driver == NULL)
		return 0;
	drv = to_macio_driver(macio_dev->ofdev.dev.driver);
	if (drv->suspend)
		error = drv->suspend(macio_dev, state);
	return error;
}

static int macio_device_resume(struct device * dev)
{
	struct macio_dev * macio_dev = to_macio_device(dev);
	struct macio_driver * drv;
	int error = 0;

	if (macio_dev->ofdev.dev.driver == NULL)
		return 0;
	drv = to_macio_driver(macio_dev->ofdev.dev.driver);
	if (drv->resume)
		error = drv->resume(macio_dev);
	return error;
}

struct bus_type macio_bus_type = {
       .name	= "macio",
       .match	= macio_bus_match,
       .suspend	= macio_device_suspend,
       .resume	= macio_device_resume,
};

static int __init
macio_bus_driver_init(void)
{
	return bus_register(&macio_bus_type);
}

postcore_initcall(macio_bus_driver_init);


/**
 * macio_release_dev - free a macio device structure when all users of it are finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this macio device are
 * done. This currently means never as we don't hot remove any macio device yet,
 * though that will happen with mediabay based devices in a later implementation.
 */
static void macio_release_dev(struct device *dev)
{
	struct macio_dev *mdev;

        mdev = to_macio_device(dev);
	kfree(mdev);
}

/**
 * macio_add_one_device - Add one device from OF node to the device tree
 * @chip: pointer to the macio_chip holding the device
 * @np: pointer to the device node in the OF tree
 * @in_bay: set to 1 if device is part of a media-bay
 *
 * When media-bay is changed to hotswap drivers, this function will
 * be exposed to the bay driver some way...
 */
static struct macio_dev * macio_add_one_device(struct macio_chip *chip, struct device *parent,
		     struct device_node *np, struct macio_dev *in_bay)
{
	struct macio_dev *dev;
	u32 *reg;
	
	if (np == NULL)
		return NULL;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
	memset(dev, 0, sizeof(*dev));

	dev->bus = &chip->lbus;
	dev->media_bay = in_bay;
	dev->ofdev.node = np;
	dev->ofdev.dma_mask = 0xffffffffUL;
	dev->ofdev.dev.dma_mask = &dev->ofdev.dma_mask;
	dev->ofdev.dev.parent = parent;
	dev->ofdev.dev.bus = &macio_bus_type;
	dev->ofdev.dev.release = macio_release_dev;

	/* MacIO itself has a different reg, we use it's PCI base */
	if (np == chip->of_node) {
		sprintf(dev->ofdev.dev.bus_id, "%1d.%08lx:%.8s", chip->lbus.index,
#ifdef CONFIG_PCI
			pci_resource_start(chip->lbus.pdev, 0),
#else
			0, /* NuBus may want to do something better here */
#endif
			np->name);
	} else {
		reg = (u32 *)get_property(np, "reg", NULL);
		sprintf(dev->ofdev.dev.bus_id, "%1d.%08x:%.8s", chip->lbus.index,
			reg ? *reg : 0, np->name);
	}

	if (of_device_register(&dev->ofdev) != 0) {
		kfree(dev);
		return NULL;
	}

	return dev;
}

static int macio_skip_device(struct device_node *np)
{
	if (strncmp(np->name, "battery", 7) == 0)
		return 1;
	if (strncmp(np->name, "escc-legacy", 11) == 0)
		return 1;
	return 0;
}

/**
 * macio_pci_add_devices - Adds sub-devices of mac-io to the device tree
 * @chip: pointer to the macio_chip holding the devices
 * 
 * This function will do the job of extracting devices from the
 * Open Firmware device tree, build macio_dev structures and add
 * them to the Linux device tree.
 * 
 * For now, childs of media-bay are added now as well. This will
 * change rsn though.
 */
static void macio_pci_add_devices(struct macio_chip *chip)
{
	struct device_node *np, *pnode;
	struct macio_dev *rdev, *mdev, *mbdev = NULL, *sdev = NULL;
	struct device *parent = NULL;
	
	/* Add a node for the macio bus itself */
#ifdef CONFIG_PCI
	if (chip->lbus.pdev)
		parent = &chip->lbus.pdev->dev;
#endif
	pnode = of_node_get(chip->of_node);
	if (pnode == NULL)
		return;

	rdev = macio_add_one_device(chip, parent, pnode, NULL);
	if (rdev == NULL)
		return;

	/* First scan 1st level */
	for (np = NULL; (np = of_get_next_child(pnode, np)) != NULL;) {
		if (!macio_skip_device(np)) {
			of_node_get(np);
			mdev = macio_add_one_device(chip, &rdev->ofdev.dev, np, NULL);
			if (mdev == NULL)
				of_node_put(np);
			else if (strncmp(np->name, "media-bay", 9) == 0)
				mbdev = mdev;
			else if (strncmp(np->name, "escc", 4) == 0)
				sdev = mdev;
		}
	}

	/* Add media bay devices if any */
	if (mbdev)
		for (np = NULL; (np = of_get_next_child(mbdev->ofdev.node, np)) != NULL;)
			if (!macio_skip_device(np)) {
				of_node_get(np);
				if (macio_add_one_device(chip, &mbdev->ofdev.dev, np, mbdev) == NULL)
					of_node_put(np);
			}
	/* Add serial ports if any */
	if (sdev)
		for (np = NULL; (np = of_get_next_child(sdev->ofdev.node, np)) != NULL;)
			if (!macio_skip_device(np)) {
				of_node_get(np);
				if (macio_add_one_device(chip, &sdev->ofdev.dev, np, NULL) == NULL)
					of_node_put(np);
			}
}


/**
 * macio_register_driver - Registers a new MacIO device driver
 * @drv: pointer to the driver definition structure
 */
int macio_register_driver(struct macio_driver *drv)
{
	int count = 0;

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &macio_bus_type;
	drv->driver.probe = macio_device_probe;
	drv->driver.remove = macio_device_remove;

	/* register with core */
	count = driver_register(&drv->driver);
	return count ? count : 1;
}

/**
 * macio_unregister_driver - Unregisters a new MacIO device driver
 * @drv: pointer to the driver definition structure
 */
void macio_unregister_driver(struct macio_driver *drv)
{
	driver_unregister(&drv->driver);
}

#ifdef CONFIG_PCI

static int __devinit macio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device_node* np;
	struct macio_chip* chip;
	
	if (ent->vendor != PCI_VENDOR_ID_APPLE)
		return -ENODEV;

	/* Note regarding refcounting: We assume pci_device_to_OF_node() is ported
	 * to new OF APIs and returns a node with refcount incremented. This isn't
	 * the case today, but on the other hand ppc32 doesn't do refcounting. This
	 * will have to be fixed when going to ppc64. --BenH.
	 */
	np = pci_device_to_OF_node(pdev);
	if (np == NULL)
		return -ENODEV;

	/* We also assume that pmac_feature will have done a get() on nodes stored
	 * in the macio chips array
	 */
	chip = macio_find(np, macio_unknown);
       	of_node_put(np);
	if (chip == NULL)
		return -ENODEV;

	/* XXX Need locking ??? */
	if (chip->lbus.pdev == NULL) {
		chip->lbus.pdev = pdev;
		chip->lbus.chip = chip;
		pci_set_drvdata(pdev, &chip->lbus);
		pci_set_master(pdev);
	}

	printk(KERN_INFO "MacIO PCI driver attached to %s chipset\n",
		chip->name);

	/*
	 * HACK ALERT: The WallStreet PowerBook and some OHare based machines
	 * have 2 macio ASICs. I must probe the "main" one first or IDE ordering
	 * will be incorrect. So I put on "hold" the second one since it seem to
	 * appear first on PCI
	 */
	if (chip->type == macio_gatwick || chip->type == macio_ohareII)
		if (macio_chips[0].lbus.pdev == NULL) {
			macio_on_hold = chip;
			return 0;
		}

	macio_pci_add_devices(chip);
	if (macio_on_hold && macio_chips[0].lbus.pdev != NULL) {
		macio_pci_add_devices(macio_on_hold);
		macio_on_hold = NULL;
	}

	return 0;
}

static void __devexit macio_pci_remove(struct pci_dev* pdev)
{
	panic("removing of macio-asic not supported !\n");
}

/*
 * MacIO is matched against any Apple ID, it's probe() function
 * will then decide wether it applies or not
 */
static const struct pci_device_id __devinitdata pci_ids [] = { {
	.vendor		= PCI_VENDOR_ID_APPLE,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,

	}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver macio_pci_driver = {
	.name		= (char *) "macio",
	.id_table	= pci_ids,

	.probe		= macio_pci_probe,
	.remove		= macio_pci_remove,
};

#endif /* CONFIG_PCI */

static int __init macio_module_init (void) 
{
#ifdef CONFIG_PCI
	int rc;

	rc = pci_module_init(&macio_pci_driver);
	if (rc)
		return rc;
#endif /* CONFIG_PCI */
	return 0;
}

module_init(macio_module_init);

EXPORT_SYMBOL(macio_register_driver);
EXPORT_SYMBOL(macio_unregister_driver);
EXPORT_SYMBOL(macio_dev_get);
EXPORT_SYMBOL(macio_dev_put);
