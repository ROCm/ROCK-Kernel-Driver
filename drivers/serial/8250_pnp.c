/*
 *  linux/drivers/char/serial_8250_pnp.c
 *
 *  Probe module for 8250/16550-type ISAPNP serial ports.
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2001 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 *  $Id: serial_8250_pnp.c,v 1.9 2002/02/18 19:20:29 rmk Exp $
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/isapnp.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/serial.h>
#include <linux/serialP.h>

#include <asm/bitops.h>
#include <asm/byteorder.h>
#include <asm/serial.h>

#include "serial_8250.h"

struct pnpbios_device_id
{
	char id[8];
	unsigned long driver_data;
};

static const struct pnpbios_device_id pnp_dev_table[] = {
	/* Archtek America Corp. */
	/* Archtek SmartLink Modem 3334BT Plug & Play */
	{	"AAC000F",		0	},
	/* Anchor Datacomm BV */
	/* SXPro 144 External Data Fax Modem Plug & Play */
	{	"ADC0001",		0	},
	/* SXPro 288 External Data Fax Modem Plug & Play */
	{	"ADC0002",		0	},
	/* Rockwell 56K ACF II Fax+Data+Voice Modem */
	{	"AKY1021",		SPCI_FL_NO_SHIRQ	},
	/* AZT3005 PnP SOUND DEVICE */
	{	"AZT4001",		0	},
	/* Best Data Products Inc. Smart One 336F PnP Modem */
	{	"BDP3336",		0	},
	/*  Boca Research */
	/* Boca Complete Ofc Communicator 14.4 Data-FAX */
	{	"BRI0A49",		0	},
	/* Boca Research 33,600 ACF Modem */
	{	"BRI1400",		0	},
	/* Boca 33.6 Kbps Internal FD34FSVD */
	{	"BRI3400",		0	},
	/* Boca 33.6 Kbps Internal FD34FSVD */
	{	"BRI0A49",		0	},
	/* Best Data Products Inc. Smart One 336F PnP Modem */
	{	"BDP3336",		0	},
	/* Computer Peripherals Inc */
	/* EuroViVa CommCenter-33.6 SP PnP */
	{	"CPI4050",		0	},
	/* Creative Labs */
	/* Creative Labs Phone Blaster 28.8 DSVD PnP Voice */
	{	"CTL3001",		0	},
	/* Creative Labs Modem Blaster 28.8 DSVD PnP Voice */
	{	"CTL3011",		0	},
	/* Creative */
	/* Creative Modem Blaster Flash56 DI5601-1 */
	{	"DMB1032",		0	},
	/* Creative Modem Blaster V.90 DI5660 */
	{	"DMB2001",		0	},
	/* FUJITSU */
	/* Fujitsu 33600 PnP-I2 R Plug & Play */
	{	"FUJ0202",		0	},
	/* Fujitsu FMV-FX431 Plug & Play */
	{	"FUJ0205",		0	},
	/* Fujitsu 33600 PnP-I4 R Plug & Play */
	{	"FUJ0206",		0	},
	/* Fujitsu Fax Voice 33600 PNP-I5 R Plug & Play */
	{	"FUJ0209",		0	},
	/* Archtek America Corp. */
	/* Archtek SmartLink Modem 3334BT Plug & Play */
	{	"GVC000F",		0	},
	/* Hayes */
	/* Hayes Optima 288 V.34-V.FC + FAX + Voice Plug & Play */
	{	"HAY0001",		0	},
	/* Hayes Optima 336 V.34 + FAX + Voice PnP */
	{	"HAY000C",		0	},
	/* Hayes Optima 336B V.34 + FAX + Voice PnP */
	{	"HAY000D",		0	},
	/* Hayes Accura 56K Ext Fax Modem PnP */
	{	"HAY5670",		0	},
	/* Hayes Accura 56K Ext Fax Modem PnP */
	{	"HAY5674",		0	},
	/* Hayes Accura 56K Fax Modem PnP */
	{	"HAY5675",		0	},
	/* Hayes 288, V.34 + FAX */
	{	"HAYF000",		0	},
	/* Hayes Optima 288 V.34 + FAX + Voice, Plug & Play */
	{	"HAYF001",		0	},
	/* IBM */
	/* IBM Thinkpad 701 Internal Modem Voice */
	{	"IBM0033",		0	},
	/* Intertex */
	/* Intertex 28k8 33k6 Voice EXT PnP */
	{	"IXDC801",		0	},
	/* Intertex 33k6 56k Voice EXT PnP */
	{	"IXDC901",		0	},
	/* Intertex 28k8 33k6 Voice SP EXT PnP */
	{	"IXDD801",		0	},
	/* Intertex 33k6 56k Voice SP EXT PnP */
	{	"IXDD901",		0	},
	/* Intertex 28k8 33k6 Voice SP INT PnP */
	{	"IXDF401",		0	},
	/* Intertex 28k8 33k6 Voice SP EXT PnP */
	{	"IXDF801",		0	},
	/* Intertex 33k6 56k Voice SP EXT PnP */
	{	"IXDF901",		0	},
	/* Kortex International */
	/* KORTEX 28800 Externe PnP */
	{	"KOR4522",		0	},
	/* KXPro 33.6 Vocal ASVD PnP */
	{	"KORF661",		0	},
	/* Lasat */
	/* LASAT Internet 33600 PnP */
	{	"LAS4040",		0	},
	/* Lasat Safire 560 PnP */
	{	"LAS4540",		0	},
	/* Lasat Safire 336  PnP */
	{	"LAS5440",		0	},
	/* Microcom, Inc. */
	/* Microcom TravelPorte FAST V.34 Plug & Play */
	{	"MNP0281",		0	},
	/* Microcom DeskPorte V.34 FAST or FAST+ Plug & Play */
	{	"MNP0336",		0	},
	/* Microcom DeskPorte FAST EP 28.8 Plug & Play */
	{	"MNP0339",		0	},
	/* Microcom DeskPorte 28.8P Plug & Play */
	{	"MNP0342",		0	},
	/* Microcom DeskPorte FAST ES 28.8 Plug & Play */
	{	"MNP0500",		0	},
	/* Microcom DeskPorte FAST ES 28.8 Plug & Play */
	{	"MNP0501",		0	},
	/* Microcom DeskPorte 28.8S Internal Plug & Play */
	{	"MNP0502",		0	},
	/* Motorola */
	/* Motorola BitSURFR Plug & Play */
	{	"MOT1105",		0	},
	/* Motorola TA210 Plug & Play */
	{	"MOT1111",		0	},
	/* Motorola HMTA 200 (ISDN) Plug & Play */
	{	"MOT1114",		0	},
	/* Motorola BitSURFR Plug & Play */
	{	"MOT1115",		0	},
	/* Motorola Lifestyle 28.8 Internal */
	{	"MOT1190",		0	},
	/* Motorola V.3400 Plug & Play */
	{	"MOT1501",		0	},
	/* Motorola Lifestyle 28.8 V.34 Plug & Play */
	{	"MOT1502",		0	},
	/* Motorola Power 28.8 V.34 Plug & Play */
	{	"MOT1505",		0	},
	/* Motorola ModemSURFR External 28.8 Plug & Play */
	{	"MOT1509",		0	},
	/* Motorola Premier 33.6 Desktop Plug & Play */
	{	"MOT150A",		0	},
	/* Motorola VoiceSURFR 56K External PnP */
	{	"MOT150F",		0	},
	/* Motorola ModemSURFR 56K External PnP */
	{	"MOT1510",		0	},
	/* Motorola ModemSURFR 56K Internal PnP */
	{	"MOT1550",		0	},
	/* Motorola ModemSURFR Internal 28.8 Plug & Play */
	{	"MOT1560",		0	},
	/* Motorola Premier 33.6 Internal Plug & Play */
	{	"MOT1580",		0	},
	/* Motorola OnlineSURFR 28.8 Internal Plug & Play */
	{	"MOT15B0",		0	},
	/* Motorola VoiceSURFR 56K Internal PnP */
	{	"MOT15F0",		0	},
	/* Com 1 */
	/*  Deskline K56 Phone System PnP */
	{	"MVX00A1",		0	},
	/* PC Rider K56 Phone System PnP */
	{	"MVX00F2",		0	},
	/* Pace 56 Voice Internal Plug & Play Modem */
	{	"PMC2430",		0	},
	/* Generic */
	/* Generic standard PC COM port	 */
	{	"PNP0500",		0	},
	/* Generic 16550A-compatible COM port */
	{	"PNP0501",		0	},
	/* Compaq 14400 Modem */
	{	"PNPC000",		0	},
	/* Compaq 2400/9600 Modem */
	{	"PNPC001",		0	},
	/* Dial-Up Networking Serial Cable between 2 PCs */
	{	"PNPC031",		0	},
	/* Dial-Up Networking Parallel Cable between 2 PCs */
	{	"PNPC032",		0	},
	/* Standard 9600 bps Modem */
	{	"PNPC100",		0	},
	/* Standard 14400 bps Modem */
	{	"PNPC101",		0	},
	/*  Standard 28800 bps Modem*/
	{	"PNPC102",		0	},
	/*  Standard Modem*/
	{	"PNPC103",		0	},
	/*  Standard 9600 bps Modem*/
	{	"PNPC104",		0	},
	/*  Standard 14400 bps Modem*/
	{	"PNPC105",		0	},
	/*  Standard 28800 bps Modem*/
	{	"PNPC106",		0	},
	/*  Standard Modem */
	{	"PNPC107",		0	},
	/* Standard 9600 bps Modem */
	{	"PNPC108",		0	},
	/* Standard 14400 bps Modem */
	{	"PNPC109",		0	},
	/* Standard 28800 bps Modem */
	{	"PNPC10A",		0	},
	/* Standard Modem */
	{	"PNPC10B",		0	},
	/* Standard 9600 bps Modem */
	{	"PNPC10C",		0	},
	/* Standard 14400 bps Modem */
	{	"PNPC10D",		0	},
	/* Standard 28800 bps Modem */
	{	"PNPC10E",		0	},
	/* Standard Modem */
	{	"PNPC10F",		0	},
	/* Standard PCMCIA Card Modem */
	{	"PNP2000",		0	},
	/* Rockwell */
	/* Modular Technology */
	/* Rockwell 33.6 DPF Internal PnP */
	/* Modular Technology 33.6 Internal PnP */
	{	"ROK0030",		0	},
	/* Kortex International */
	/* KORTEX 14400 Externe PnP */
	{	"ROK0100",		0	},
	/* Viking Components, Inc */
	/* Viking 28.8 INTERNAL Fax+Data+Voice PnP */
	{	"ROK4920",		0	},
	/* Rockwell */
	/* British Telecom */
	/* Modular Technology */
	/* Rockwell 33.6 DPF External PnP */
	/* BT Prologue 33.6 External PnP */
	/* Modular Technology 33.6 External PnP */
	{	"RSS00A0",		0	},
	/* Viking 56K FAX INT */
	{	"RSS0262",		0	},
	/* SupraExpress 28.8 Data/Fax PnP modem */
	{	"SUP1310",		0	},
	/* SupraExpress 33.6 Data/Fax PnP modem */
	{	"SUP1421",		0	},
	/* SupraExpress 33.6 Data/Fax PnP modem */
	{	"SUP1590",		0	},
	/* SupraExpress 33.6 Data/Fax PnP modem */
	{	"SUP1760",		0	},
	/* Phoebe Micro */
	/* Phoebe Micro 33.6 Data Fax 1433VQH Plug & Play */
	{	"TEX0011",		0	},
	/* Archtek America Corp. */
	/* Archtek SmartLink Modem 3334BT Plug & Play */
	{	"UAC000F",		0	},
	/* 3Com Corp. */
	/* Gateway Telepath IIvi 33.6 */
	{	"USR0000",		0	},
	/*  Sportster Vi 14.4 PnP FAX Voicemail */
	{	"USR0004",		0	},
	/* U.S. Robotics 33.6K Voice INT PnP */
	{	"USR0006",		0	},
	/* U.S. Robotics 33.6K Voice EXT PnP */
	{	"USR0007",		0	},
	/* U.S. Robotics 33.6K Voice INT PnP */
	{	"USR2002",		0	},
	/* U.S. Robotics 56K Voice INT PnP */
	{	"USR2070",		0	},
	/* U.S. Robotics 56K Voice EXT PnP */
	{	"USR2080",		0	},
	/* U.S. Robotics 56K FAX INT */
	{	"USR3031",		0	},
	/* U.S. Robotics 56K Voice INT PnP */
	{	"USR3070",		0	},
	/* U.S. Robotics 56K Voice EXT PnP */
	{	"USR3080",		0	},
	/* U.S. Robotics 56K Voice INT PnP */
	{	"USR3090",		0	},
	/* U.S. Robotics 56K Message  */
	{	"USR9100",		0	},
	/* U.S. Robotics 56K FAX EXT PnP*/
	{	"USR9160",		0	},
	/* U.S. Robotics 56K FAX INT PnP*/
	{	"USR9170",		0	},
	/* U.S. Robotics 56K Voice EXT PnP*/
	{	"USR9180",		0	},
	/* U.S. Robotics 56K Voice INT PnP*/
	{	"USR9190",		0	},
	{	"",			0	}
};

static void inline avoid_irq_share(struct pci_dev *dev)
{
	unsigned int map = 0x1FF8;
	struct isapnp_irq *irq;
	struct isapnp_resources *res = dev->sysdata;

	serial8250_get_irq_map(&map);

	for ( ; res; res = res->alt)
		for (irq = res->irq; irq; irq = irq->next)
			irq->map = map;
}

static char *modem_names[] __devinitdata = {
	"MODEM", "Modem", "modem", "FAX", "Fax", "fax",
	"56K", "56k", "K56", "33.6", "28.8", "14.4",
	"33,600", "28,800", "14,400", "33.600", "28.800", "14.400",
	"33600", "28800", "14400", "V.90", "V.34", "V.32", 0
};

static int __devinit check_name(char *name)
{
	char **tmp;

	for (tmp = modem_names; *tmp; tmp++)
		if (strstr(name, *tmp))
			return 1;

	return 0;
}

static int inline check_compatible_id(struct pci_dev *dev)
{
	int i;
	for (i = 0; i < DEVICE_COUNT_COMPATIBLE; i++)
		if ((dev->vendor_compatible[i] ==
		     ISAPNP_VENDOR('P', 'N', 'P')) &&
		    (swab16(dev->device_compatible[i]) >= 0xc000) &&
		    (swab16(dev->device_compatible[i]) <= 0xdfff))
			return 0;
	return 1;
}

/*
 * Given a complete unknown ISA PnP device, try to use some heuristics to
 * detect modems. Currently use such heuristic set:
 *     - dev->name or dev->bus->name must contain "modem" substring;
 *     - device must have only one IO region (8 byte long) with base adress
 *       0x2e8, 0x3e8, 0x2f8 or 0x3f8.
 *
 * Such detection looks very ugly, but can detect at least some of numerous
 * ISA PnP modems, alternatively we must hardcode all modems in pnp_devices[]
 * table.
 */
static int serial_pnp_guess_board(struct pci_dev *dev, int *flags)
{
	struct isapnp_resources *res = (struct isapnp_resources *)dev->sysdata;
	struct isapnp_resources *resa;

	if (!(check_name(dev->name) || check_name(dev->bus->name)) &&
	    !(check_compatible_id(dev)))
		return -ENODEV;

	if (!res || res->next)
		return -ENODEV;

	for (resa = res->alt; resa; resa = resa->alt) {
		struct isapnp_port *port;
		for (port = res->port; port; port = port->next)
			if ((port->size == 8) &&
			    ((port->min == 0x2f8) ||
			     (port->min == 0x3f8) ||
			     (port->min == 0x2e8) ||
			     (port->min == 0x3e8)))
				return 0;
	}

	return -ENODEV;
}

static int
pnp_init_one(struct pci_dev *dev, const struct pnpbios_device_id *ent,
	     char *slot_name)
{
	struct serial_struct serial_req;
	int ret, line, flags = ent ? ent->driver_data : 0;

	if (!ent) {
		ret = serial_pnp_guess_board(dev, &flags);
		if (ret)
			return ret;
	}

	if (dev->prepare(dev) < 0) {
		printk("serial: PNP device '%s' prepare failed\n",
			slot_name);
		return -ENODEV;
	}

	if (dev->active)
		return -ENODEV;

	if (flags & SPCI_FL_NO_SHIRQ)
		avoid_irq_share(dev);

	if (dev->activate(dev) < 0) {
		printk("serial: PNP device '%s' activate failed\n",
			slot_name);
		return -ENODEV;
	}

	memset(&serial_req, 0, sizeof(serial_req));
	serial_req.irq = dev->irq_resource[0].start;
	serial_req.port = pci_resource_start(dev, 0);
	if (HIGH_BITS_OFFSET)
		serial_req.port = pci_resource_start(dev, 0) >> HIGH_BITS_OFFSET;

#ifdef SERIAL_DEBUG_PNP
	printk("Setup PNP port: port %x, irq %d, type %d\n",
	       serial_req.port, serial_req.irq, serial_req.io_type);
#endif

	serial_req.flags = ASYNC_SKIP_TEST | ASYNC_AUTOPROBE;
	serial_req.baud_base = 115200;
	line = register_serial(&serial_req);

	if (line >= 0) {
		pci_set_drvdata(dev, (void *)(line + 1));

		/*
		 * Public health warning: remove this once the 2.5
		 * pnpbios_module_init() stuff is incorporated.
		 */
		dev->driver = (void *)pnp_dev_table;
	} else
		dev->deactivate(dev);

	return line >= 0 ? 0 : -ENODEV;
}

static void pnp_remove_one(struct pci_dev *dev)
{
	int line = (int)pci_get_drvdata(dev);

	if (line) {
		pci_set_drvdata(dev, NULL);

		unregister_serial(line - 1);

		dev->deactivate(dev);
	}
}

static char hex[] = "0123456789ABCDEF";

/*
 * This function should vanish when 2.5 comes around and
 * we have pnpbios_module_init()
 */
static int pnp_init(void)
{
	const struct pnpbios_device_id *id;
	struct pci_dev *dev = NULL;
	int nr = 0, rc = -ENODEV;

#ifdef SERIAL_DEBUG_PNP
	printk("Entered probe_serial_pnp()\n");
#endif

	isapnp_for_each_dev(dev) {
		char slot_name[8];
		u32 pnpid;

		if (dev->active)
			continue;

		pnpid = dev->vendor << 16 | dev->device;
		pnpid = cpu_to_le32(pnpid);

#define HEX(id,a) hex[((id)>>a) & 15]
#define CHAR(id,a) (0x40 + (((id)>>a) & 31))
		slot_name[0] = CHAR(pnpid, 26);
		slot_name[1] = CHAR(pnpid, 21);
		slot_name[2] = CHAR(pnpid, 16);
		slot_name[3] = HEX(pnpid, 12);
		slot_name[4] = HEX(pnpid, 8);
		slot_name[5] = HEX(pnpid, 4);
		slot_name[6] = HEX(pnpid, 0);
		slot_name[7] = '\0';
		
		for (id = pnp_dev_table; id->id[0]; id++)
			if (memcmp(id->id, slot_name, 7) == 0)
				break;

		if (id->id[0])
			rc = pnp_init_one(dev, id, slot_name);
		else
			rc = pnp_init_one(dev, NULL, slot_name);

		if (rc == 0)
			nr++;
	}

#ifdef SERIAL_DEBUG_PNP
	printk("Leaving probe_serial_pnp() (probe finished)\n");
#endif

	return nr == 0 ? rc : 0;
}

static int __init serial8250_pnp_init(void)
{
	if (!isapnp_present()) {
#ifdef SERIAL_DEBUG_PNP
		printk("Leaving probe_serial_pnp() (no isapnp)\n");
#endif
		return -ENODEV;
	}
	return pnp_init();
}

static void __exit serial8250_pnp_exit(void)
{
	struct pci_dev *dev = NULL;

	isapnp_for_each_dev(dev) {
		if (dev->driver != (void *)pnp_dev_table)
			continue;
		pnp_remove_one(dev);
	}
}

module_init(serial8250_pnp_init);
module_exit(serial8250_pnp_exit);

EXPORT_NO_SYMBOLS;

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic 8250/16x50 PNPBIOS serial probe module");
MODULE_DEVICE_TABLE(pnpbios, pnp_dev_table);
