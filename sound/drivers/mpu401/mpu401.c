/*
 *  Driver for generic MPU-401 boards (UART mode only)
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *  ACPI PnP Copyright (c) 2004 by Clemens Ladisch <clemens@ladisch.de>
 *  based on 8250_acpi.c
 *  Copyright (c) 2002-2003 Matthew Wilcox for Hewlett-Packard
 *  Copyright (C) 2004 Hewlett-Packard Co
 *       Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/init.h>
#ifdef CONFIG_ACPI_BUS
#include <acpi/acpi_bus.h>
#endif
#include <sound/core.h>
#include <sound/mpu401.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#ifdef CONFIG_ACPI_BUS
#define USE_ACPI_PNP
#endif

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("MPU-401 UART");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
#ifdef USE_ACPI_PNP
static int acpipnp[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = 1 };
#endif
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* MPU-401 port number */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* MPU-401 IRQ */
#ifdef CONFIG_X86_PC9800
static int pc98ii[SNDRV_CARDS];				/* PC98-II dauther board */
#endif

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for MPU-401 device.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for MPU-401 device.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable MPU-401 device.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
#ifdef USE_ACPI_PNP
MODULE_PARM(acpipnp, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(acpipnp, "ACPI PnP detection for MPU-401 device.");
MODULE_PARM_SYNTAX(acpipnp, SNDRV_ENABLED "," SNDRV_BOOLEAN_TRUE_DESC);
#endif
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for MPU-401 device.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for MPU-401 device.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
#ifdef CONFIG_X86_PC9800
MODULE_PARM(pc98ii, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(pc98ii, "Roland MPU-PC98II support.");
MODULE_PARM_SYNTAX(pc98ii, SNDRV_BOOLEAN_FALSE_DESC);
#endif

#ifndef CONFIG_ACPI_BUS
struct acpi_device;
#endif

static snd_card_t *snd_mpu401_legacy_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef USE_ACPI_PNP

static int acpi_driver_registered;

struct mpu401_resources {
	unsigned long port;
	int irq;
};

static acpi_status __devinit snd_mpu401_acpi_resource(struct acpi_resource *res,
						      void *data)
{
	struct mpu401_resources *resources = (struct mpu401_resources *)data;

	if (res->id == ACPI_RSTYPE_IRQ) {
		if (res->data.irq.number_of_interrupts > 0) {
#ifdef CONFIG_IA64
			resources->irq = acpi_register_irq(res->data.irq.interrupts[0],
							   res->data.irq.active_high_low,
							   res->data.irq.edge_level);
#else
			resources->irq = res->data.irq.interrupts[0];
#endif
		}
	} else if (res->id == ACPI_RSTYPE_IO) {
		if (res->data.io.range_length >= 2) {
			resources->port = res->data.io.min_base_address;
		}
	}
	return AE_OK;
}

static int __devinit snd_mpu401_acpi_pnp(int dev, struct acpi_device *device)
{
	struct mpu401_resources res;
	acpi_status status;

	res.port = SNDRV_AUTO_PORT;
	res.irq = SNDRV_AUTO_IRQ;
	status = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
				     snd_mpu401_acpi_resource, &res);
	if (ACPI_FAILURE(status))
		return -ENODEV;
	if (res.port == SNDRV_AUTO_PORT || res.irq == SNDRV_AUTO_IRQ) {
		snd_printk(KERN_ERR "no port or irq in %s _CRS\n",
			   acpi_device_bid(device));
		return -ENODEV;
	}
	port[dev] = res.port;
	irq[dev] = res.irq;
	return 0;
}

#endif /* USE_ACPI_PNP */

static int __devinit snd_card_mpu401_probe(int dev, struct acpi_device *device)
{
	snd_card_t *card;
	int err;

#ifdef USE_ACPI_PNP
	if (!device) {
#endif
		if (port[dev] == SNDRV_AUTO_PORT) {
			snd_printk(KERN_ERR "specify port\n");
			return -EINVAL;
		}
		if (irq[dev] == SNDRV_AUTO_IRQ) {
			snd_printk(KERN_ERR "specify or disable IRQ port\n");
			return -EINVAL;
		}
#ifdef USE_ACPI_PNP
	}
#endif

#ifdef USE_ACPI_PNP
	if (device && (err = snd_mpu401_acpi_pnp(dev, device)) < 0)
		return err;
#endif

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	strcpy(card->driver, "MPU-401 UART");
	strcpy(card->shortname, card->driver);
	sprintf(card->longname, "%s at 0x%lx, ", card->shortname, port[dev]);
	if (irq[dev] >= 0) {
		sprintf(card->longname + strlen(card->longname), "IRQ %d", irq[dev]);
	} else {
		strcat(card->longname, "polled");
	}
#ifdef USE_ACPI_PNP
	if (device) {
		strcat(card->longname, ", bus id ");
		strlcat(card->longname, acpi_device_bid(device), sizeof(card->longname));
	}
#endif
	if (snd_mpu401_uart_new(card, 0,
#ifdef CONFIG_X86_PC9800
				pc98ii[dev] ? MPU401_HW_PC98II :
#endif
				MPU401_HW_MPU401,
				port[dev], 0,
				irq[dev], irq[dev] >= 0 ? SA_INTERRUPT : 0, NULL) < 0) {
		printk(KERN_ERR "MPU401 not detected at 0x%lx\n", port[dev]);
		snd_card_free(card);
		return -ENODEV;
	}
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
#ifdef USE_ACPI_PNP
	if (device)
		acpi_driver_data(device) = card;
	else
#endif
		snd_mpu401_legacy_cards[dev] = card;
	return 0;
}

#ifdef USE_ACPI_PNP

static int __devinit snd_mpu401_acpi_add(struct acpi_device *device)
{
	static int dev;
	int err;

	for ( ; dev < SNDRV_CARDS; ++dev) {
		if (!enable[dev] || !acpipnp[dev])
			continue;
		err = snd_card_mpu401_probe(dev, device);
		if (err < 0)
			return err;
		++dev;
		return 0;
	}
	return -ENODEV;
}

static int __devexit snd_mpu401_acpi_remove(struct acpi_device *device,
					    int type)
{
	snd_card_t *card;

	if (!device)
		return -EINVAL;
	card = (snd_card_t *)acpi_driver_data(device);
	if (!card)
		return -EINVAL;

	snd_card_disconnect(card);
	snd_card_free_in_thread(card);
	acpi_driver_data(device) = NULL;
	return 0;
}

static struct acpi_driver snd_mpu401_acpi_driver = {
	.name = "MPU-401 Driver",
	.class = "mpu401",
	.ids = "PNPB006",
	.ops = {
		.add = snd_mpu401_acpi_add,
		.remove = __devexit_p(snd_mpu401_acpi_remove),
	},
};

#endif /* USE_ACPI_PNP */

static int __init alsa_card_mpu401_init(void)
{
	int dev, cards;

#ifdef USE_ACPI_PNP
	cards = acpi_bus_register_driver(&snd_mpu401_acpi_driver);
	acpi_driver_registered = cards >= 0;
#else
	cards = 0;
#endif
	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
#ifdef USE_ACPI_PNP
		if (acpipnp[dev] && acpi_driver_registered)
			continue;
#endif
		if (snd_card_mpu401_probe(dev, NULL) >= 0)
			cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "MPU-401 device not found or device busy\n");
#endif
#ifdef USE_ACPI_PNP
		if (acpi_driver_registered)
			acpi_bus_unregister_driver(&snd_mpu401_acpi_driver);
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_mpu401_exit(void)
{
	int idx;

#ifdef USE_ACPI_PNP
	if (acpi_driver_registered)
		acpi_bus_unregister_driver(&snd_mpu401_acpi_driver);
#endif
	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_mpu401_legacy_cards[idx]);
}

module_init(alsa_card_mpu401_init)
module_exit(alsa_card_mpu401_exit)

#ifndef MODULE

/* format is: snd-mpu401=enable,index,id,acpipnp[,pc98ii],port,irq */

static int __init alsa_card_mpu401_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;
	int __attribute__ ((__unused__)) pnp = INT_MAX;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,&pnp) == 2 &&
#ifdef CONFIG_X86_PC9800
	       get_option(&str,&pc98ii[nr_dev]) == 2 &&
#endif
	       get_option_long(&str,&port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2);
#ifdef USE_ACPI_PNP
	if (pnp != INT_MAX)
		acpipnp[nr_dev] = pnp;
#endif
	nr_dev++;
	return 1;
}

__setup("snd-mpu401=", alsa_card_mpu401_setup);

#endif /* ifndef MODULE */
