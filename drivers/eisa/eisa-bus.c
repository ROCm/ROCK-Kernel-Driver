/*
 * EISA bus support functions for sysfs.
 *
 * (C) 2002 Marc Zyngier <maz@wild-wind.fr.eu.org>
 *
 * This code is released under the GPL version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/eisa.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <asm/io.h>

#define EISA_DEVINFO(i,s) { .id = { .sig = i }, .name = s }

struct eisa_device_info {
	struct eisa_device_id id;
	char name[DEVICE_NAME_SIZE];
};

struct eisa_device_info __initdata eisa_table[] = {
#ifdef CONFIG_EISA_NAMES
#include "devlist.h"
#endif
};

#define EISA_INFOS (sizeof (eisa_table) / (sizeof (struct eisa_device_info)))

static void __init eisa_name_device (struct eisa_device *edev)
{
	int i;

	for (i = 0; i < EISA_INFOS; i++) {
		if (!strcmp (edev->id.sig, eisa_table[i].id.sig)) {
			strncpy (edev->dev.name,
				 eisa_table[i].name,
				 DEVICE_NAME_SIZE - 1);
			return;
		}
	}

	/* No name was found */
	sprintf (edev->dev.name, "EISA device %.7s", edev->id.sig);
}

static char __init *decode_eisa_sig(unsigned long addr)
{
        static char sig_str[EISA_SIG_LEN];
	u8 sig[4];
        u16 rev;
	int i;

	sig[0] = inb (addr);

	if (sig[0] & 0x80)
                return NULL;

	for (i = 1; i < 4; i++)
		sig[i] = inb (addr + i);
	
        sig_str[0] = ((sig[0] >> 2) & 0x1f) + ('A' - 1);
        sig_str[1] = (((sig[0] & 3) << 3) | (sig[1] >> 5)) + ('A' - 1);
        sig_str[2] = (sig[1] & 0x1f) + ('A' - 1);
        rev = (sig[2] << 8) | sig[3];
        sprintf(sig_str + 3, "%04X", rev);

        return sig_str;
}

static int eisa_bus_match (struct device *dev, struct device_driver *drv)
{
	struct eisa_device *edev = to_eisa_device (dev);
	struct eisa_driver *edrv = to_eisa_driver (drv);
	const struct eisa_device_id *eids = edrv->id_table;

	if (!eids)
		return 0;

	while (strlen (eids->sig)) {
		if (!strcmp (eids->sig, edev->id.sig))
			return 1;

		eids++;
	}

	return 0;
}

struct bus_type eisa_bus_type = {
	.name  = "eisa",
	.match = eisa_bus_match,
};

/* The default EISA device parent (virtual root device). */
static struct device eisa_bus_root = {
       .name           = "EISA Bridge",
       .bus_id         = "eisa",
};

int eisa_driver_register (struct eisa_driver *edrv)
{
	int r;
	
	edrv->driver.bus = &eisa_bus_type;
	if ((r = driver_register (&edrv->driver)) < 0)
		return r;

	return 1;
}

void eisa_driver_unregister (struct eisa_driver *edrv)
{
	bus_remove_driver (&edrv->driver);
	driver_unregister (&edrv->driver);
}

static ssize_t eisa_show_sig (struct device *dev, char *buf)
{
        struct eisa_device *edev = to_eisa_device (dev);
        return sprintf (buf,"%s\n", edev->id.sig);
}

static DEVICE_ATTR(signature, S_IRUGO, eisa_show_sig, NULL);

static void __init eisa_register_device (char *sig, int slot)
{
	struct eisa_device *edev;

	if (!(edev = kmalloc (sizeof (*edev), GFP_KERNEL)))
		return;

	memset (edev, 0, sizeof (*edev));
	memcpy (edev->id.sig, sig, 7);
	edev->slot = slot;
	edev->base_addr = 0x1000 * slot;
	eisa_name_device (edev);
	edev->dev.parent = &eisa_bus_root;
	edev->dev.bus = &eisa_bus_type;
	sprintf (edev->dev.bus_id, "00:%02X", slot);

	/* Don't register resource for slot 0, since this will surely
	 * fail... :-( */

	if (slot) {
		edev->res.name  = edev->dev.name;
		edev->res.start = edev->base_addr;
		edev->res.end   = edev->res.start + 0xfff;
		edev->res.flags = IORESOURCE_IO;

		if (request_resource (&ioport_resource, &edev->res)) {
			printk (KERN_WARNING \
				"Cannot allocate resource for EISA slot %d\n",
				slot);
			kfree (edev);
			return;
		}
	}
	
	if (device_register (&edev->dev)) {
		kfree (edev);
		return;
	}

	device_create_file (&edev->dev, &dev_attr_signature);
}

static int __init eisa_probe (void)
{
        int i, c;
        char *str;
        unsigned long slot_addr;

        printk (KERN_INFO "EISA: Probing bus...\n");
        for (c = 0, i = 0; i <= EISA_MAX_SLOTS; i++) {
                slot_addr = (0x1000 * i) + EISA_VENDOR_ID_OFFSET;
                if ((str = decode_eisa_sig (slot_addr))) {
			if (!i)
				printk (KERN_INFO "EISA: Motherboard %s detected\n",
					str);
			else {
				printk (KERN_INFO "EISA: slot %d : %s detected.\n",
					i, str);

				c++;
			}

			eisa_register_device (str, i);
                }
        }
        printk (KERN_INFO "EISA: Detected %d card%s.\n", c, c < 2 ? "" : "s");

	return 0;
}

static int __init eisa_init (void)
{
	int r;
	
	if ((r = bus_register (&eisa_bus_type)))
		return r;
	
	if ((r = device_register (&eisa_bus_root))) {
		bus_unregister (&eisa_bus_type);
		return r;
	}

	printk (KERN_INFO "EISA bus registered\n");
	return eisa_probe ();
}

postcore_initcall (eisa_init);

EXPORT_SYMBOL (eisa_bus_type);
EXPORT_SYMBOL (eisa_driver_register);
EXPORT_SYMBOL (eisa_driver_unregister);
