/*
 *  linux/drivers/char/8250_pci.c
 *
 *  Probe module for 8250/16550-type PCI serial ports.
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2001 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 *  $Id: 8250_pci.c,v 1.19 2002/07/21 21:32:30 rmk Exp $
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/serial.h>

/* 2.4.6 compatibility cruft - to be removed with the old serial.c code */
#define pci_board __pci_board
#include <linux/serialP.h>
#undef pci_board

#include <asm/bitops.h>
#include <asm/byteorder.h>
#include <asm/serial.h>

#include "8250.h"


#ifndef IS_PCI_REGION_IOPORT
#define IS_PCI_REGION_IOPORT(dev, r) (pci_resource_flags((dev), (r)) & \
				      IORESOURCE_IO)
#endif
#ifndef IS_PCI_REGION_IOMEM
#define IS_PCI_REGION_IOMEM(dev, r) (pci_resource_flags((dev), (r)) & \
				      IORESOURCE_MEM)
#endif
#ifndef PCI_IRQ_RESOURCE
#define PCI_IRQ_RESOURCE(dev, r) ((dev)->irq_resource[r].start)
#endif

#ifndef pci_get_subvendor
#define pci_get_subvendor(dev) ((dev)->subsystem_vendor)
#define pci_get_subdevice(dev)  ((dev)->subsystem_device)
#endif

struct serial_private {
	unsigned int nr;
	struct pci_board *board;
	int line[0];
};

struct pci_board {
	int flags;
	int num_ports;
	int base_baud;
	int uart_offset;
	int reg_shift;
	int (*init_fn)(struct pci_dev *dev, struct pci_board *board,
			int enable);
	int first_uart_offset;
};

static int
get_pci_port(struct pci_dev *dev, struct pci_board *board,
	     struct serial_struct *req, int idx)
{
	unsigned long port;
	int base_idx;
	int max_port;
	int offset;

	base_idx = SPCI_FL_GET_BASE(board->flags);
	if (board->flags & SPCI_FL_BASE_TABLE)
		base_idx += idx;

	if (board->flags & SPCI_FL_REGION_SZ_CAP) {
		max_port = pci_resource_len(dev, base_idx) / 8;
		if (idx >= max_port)
			return 1;
	}
			
	offset = board->first_uart_offset;

	/*
	 * Timedia/SUNIX uses a mixture of BARs and offsets
	 * Ugh, this is ugly as all hell --- TYT
	 */
	if (dev->vendor == PCI_VENDOR_ID_TIMEDIA)
		switch(idx) {
		case 0:
			base_idx = 0;
			break;
		case 1:
			base_idx = 0;
			offset = 8;
			break;
		case 2:
			base_idx = 1; 
			break;
		case 3:
			base_idx = 1;
			offset = 8;
			break;
		case 4: /* BAR 2 */
		case 5: /* BAR 3 */
		case 6: /* BAR 4 */
		case 7: /* BAR 5 */
			base_idx = idx - 2;
		}

	/* AFAVLAB uses a different mixture of BARs and offsets */
	/* Not that ugly ;) -- HW */
	if (dev->vendor == PCI_VENDOR_ID_AFAVLAB && idx >= 4) {
		base_idx = 4;
		offset = (idx - 4) * 8;
	}

	/* Some Titan cards are also a little weird */
	if (dev->vendor == PCI_VENDOR_ID_TITAN &&
	    (dev->device == PCI_DEVICE_ID_TITAN_400L ||
	     dev->device == PCI_DEVICE_ID_TITAN_800L)) {
		switch (idx) {
		case 0: base_idx = 1;
			break;
		case 1: base_idx = 2;
			break;
		default:
			base_idx = 4;
			offset = 8 * (idx - 2);
		}
	}
  
	port =  pci_resource_start(dev, base_idx) + offset;

	if ((board->flags & SPCI_FL_BASE_TABLE) == 0)
		port += idx * (board->uart_offset ? board->uart_offset : 8);

	if (IS_PCI_REGION_IOPORT(dev, base_idx)) {
		req->port = port;
		if (HIGH_BITS_OFFSET)
			req->port_high = port >> HIGH_BITS_OFFSET;
		else
			req->port_high = 0;
		return 0;
	}
	req->io_type = SERIAL_IO_MEM;
	req->iomem_base = ioremap(port, board->uart_offset);
	if (req->iomem_base == NULL)
		return -ENOMEM;
	req->iomem_reg_shift = board->reg_shift;
	req->port = 0;
	return 0;
}

static _INLINE_ int
get_pci_irq(struct pci_dev *dev, struct pci_board *board, int idx)
{
	int base_idx;

	if ((board->flags & SPCI_FL_IRQRESOURCE) == 0)
		return dev->irq;

	base_idx = SPCI_FL_GET_IRQBASE(board->flags);
	if (board->flags & SPCI_FL_IRQ_TABLE)
		base_idx += idx;
	
	return PCI_IRQ_RESOURCE(dev, base_idx);
}

/*
 * Some PCI serial cards using the PLX 9050 PCI interface chip require
 * that the card interrupt be explicitly enabled or disabled.  This
 * seems to be mainly needed on card using the PLX which also use I/O
 * mapped memory.
 */
static int __devinit
pci_plx9050_fn(struct pci_dev *dev, struct pci_board *board, int enable)
{
	u8 *p, irq_config = 0;

	if (enable) {
		irq_config = 0x41;
		if (dev->vendor == PCI_VENDOR_ID_PANACOM)
			irq_config = 0x43;
		if ((dev->vendor == PCI_VENDOR_ID_PLX) &&
		    (dev->device == PCI_DEVICE_ID_PLX_ROMULUS)) {
			/*
			 * As the megawolf cards have the int pins active
			 * high, and have 2 UART chips, both ints must be
			 * enabled on the 9050. Also, the UARTS are set in
			 * 16450 mode by default, so we have to enable the
			 * 16C950 'enhanced' mode so that we can use the
			 * deep FIFOs
			 */
			irq_config = 0x5b;
		}
	}

	/*
	 * enable/disable interrupts
	 */
	p = ioremap(pci_resource_start(dev, 0), 0x80);
	if (p == NULL)
		return -ENOMEM;
	writel(irq_config, (unsigned long)p + 0x4c);

	/*
	 * Read the register back to ensure that it took effect.
	 */
	readl((unsigned long)p + 0x4c);
	iounmap(p);

	return 0;
}


/*
 * SIIG serial cards have an PCI interface chip which also controls
 * the UART clocking frequency. Each UART can be clocked independently
 * (except cards equiped with 4 UARTs) and initial clocking settings
 * are stored in the EEPROM chip. It can cause problems because this
 * version of serial driver doesn't support differently clocked UART's
 * on single PCI card. To prevent this, initialization functions set
 * high frequency clocking for all UART's on given card. It is safe (I
 * hope) because it doesn't touch EEPROM settings to prevent conflicts
 * with other OSes (like M$ DOS).
 *
 *  SIIG support added by Andrey Panin <pazke@mail.tp.ru>, 10/1999
 * 
 * There is two family of SIIG serial cards with different PCI
 * interface chip and different configuration methods:
 *     - 10x cards have control registers in IO and/or memory space;
 *     - 20x cards have control registers in standard PCI configuration space.
 */

#define PCI_DEVICE_ID_SIIG_1S_10x (PCI_DEVICE_ID_SIIG_1S_10x_550 & 0xfffc)
#define PCI_DEVICE_ID_SIIG_2S_10x (PCI_DEVICE_ID_SIIG_2S_10x_550 & 0xfff8)

static int __devinit
pci_siig10x_fn(struct pci_dev *dev, struct pci_board *board, int enable)
{
	u16 data, *p;

	if (!enable)
		return 0;

	switch (dev->device & 0xfff8) {
		case PCI_DEVICE_ID_SIIG_1S_10x:	/* 1S */
			data = 0xffdf;
			break;
		case PCI_DEVICE_ID_SIIG_2S_10x:	/* 2S, 2S1P */
			data = 0xf7ff;
			break;
		default:			/* 1S1P, 4S */
			data = 0xfffb;
			break;
	}

	p = ioremap(pci_resource_start(dev, 0), 0x80);
	if (p == NULL)
		return -ENOMEM;

	writew(readw((unsigned long) p + 0x28) & data, (unsigned long) p + 0x28);
	iounmap(p);
	return 0;
}

#define PCI_DEVICE_ID_SIIG_2S_20x (PCI_DEVICE_ID_SIIG_2S_20x_550 & 0xfffc)
#define PCI_DEVICE_ID_SIIG_2S1P_20x (PCI_DEVICE_ID_SIIG_2S1P_20x_550 & 0xfffc)

static int __devinit
pci_siig20x_fn(struct pci_dev *dev, struct pci_board *board, int enable)
{
	u8 data;

	if (!enable)
		return 0;

	/* Change clock frequency for the first UART. */
	pci_read_config_byte(dev, 0x6f, &data);
	pci_write_config_byte(dev, 0x6f, data & 0xef);

	/* If this card has 2 UART, we have to do the same with second UART. */
	if (((dev->device & 0xfffc) == PCI_DEVICE_ID_SIIG_2S_20x) ||
	    ((dev->device & 0xfffc) == PCI_DEVICE_ID_SIIG_2S1P_20x)) {
		pci_read_config_byte(dev, 0x73, &data);
		pci_write_config_byte(dev, 0x73, data & 0xef);
	}
	return 0;
}

/* Added for EKF Intel i960 serial boards */
static int __devinit
pci_inteli960ni_fn(struct pci_dev *dev, struct pci_board *board, int enable)
{
	unsigned long oldval;

	if (!(pci_get_subdevice(dev) & 0x1000))
		return -ENODEV;

	if (!enable) /* is there something to deinit? */
		return 0;
   
	/* is firmware started? */
	pci_read_config_dword(dev, 0x44, (void*) &oldval); 
	if (oldval == 0x00001000L) { /* RESET value */ 
		printk(KERN_DEBUG "Local i960 firmware missing");
		return -ENODEV;
	}
	return 0;
}

/*
 * Timedia has an explosion of boards, and to avoid the PCI table from
 * growing *huge*, we use this function to collapse some 70 entries
 * in the PCI table into one, for sanity's and compactness's sake.
 */
static unsigned short timedia_single_port[] = {
	0x4025, 0x4027, 0x4028, 0x5025, 0x5027, 0
};

static unsigned short timedia_dual_port[] = {
	0x0002, 0x4036, 0x4037, 0x4038, 0x4078, 0x4079, 0x4085,
	0x4088, 0x4089, 0x5037, 0x5078, 0x5079, 0x5085, 0x6079, 
	0x7079, 0x8079, 0x8137, 0x8138, 0x8237, 0x8238, 0x9079, 
	0x9137, 0x9138, 0x9237, 0x9238, 0xA079, 0xB079, 0xC079,
	0xD079, 0
};

static unsigned short timedia_quad_port[] = {
	0x4055, 0x4056, 0x4095, 0x4096, 0x5056, 0x8156, 0x8157, 
	0x8256, 0x8257, 0x9056, 0x9156, 0x9157, 0x9158, 0x9159, 
	0x9256, 0x9257, 0xA056, 0xA157, 0xA158, 0xA159, 0xB056,
	0xB157, 0
};

static unsigned short timedia_eight_port[] = {
	0x4065, 0x4066, 0x5065, 0x5066, 0x8166, 0x9066, 0x9166, 
	0x9167, 0x9168, 0xA066, 0xA167, 0xA168, 0
};

static struct timedia_struct {
	int num;
	unsigned short *ids;
} timedia_data[] = {
	{ 1, timedia_single_port },
	{ 2, timedia_dual_port },
	{ 4, timedia_quad_port },
	{ 8, timedia_eight_port },
	{ 0, 0 }
};

static int __devinit
pci_timedia_fn(struct pci_dev *dev, struct pci_board *board, int enable)
{
	int	i, j;
	unsigned short *ids;

	if (!enable)
		return 0;

	for (i = 0; timedia_data[i].num; i++) {
		ids = timedia_data[i].ids;
		for (j = 0; ids[j]; j++) {
			if (pci_get_subdevice(dev) == ids[j]) {
				board->num_ports = timedia_data[i].num;
				return 0;
			}
		}
	}
	return 0;
}

static int __devinit
pci_xircom_fn(struct pci_dev *dev, struct pci_board *board, int enable)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ/10);
	return 0;
}

/*
 * This is the configuration table for all of the PCI serial boards
 * which we support.  It is directly indexed by the pci_board_num_t enum
 * value, which is encoded in the pci_device_id PCI probe table's
 * driver_data member.
 */
enum pci_board_num_t {
	pbn_b0_1_115200,
	pbn_default = 0,

	pbn_b0_2_115200,
	pbn_b0_4_115200,

	pbn_b0_1_921600,
	pbn_b0_2_921600,
	pbn_b0_4_921600,

	pbn_b0_bt_1_115200,
	pbn_b0_bt_2_115200,
	pbn_b0_bt_8_115200,
	pbn_b0_bt_1_460800,
	pbn_b0_bt_2_460800,

	pbn_b1_1_115200,
	pbn_b1_2_115200,
	pbn_b1_4_115200,
	pbn_b1_8_115200,

	pbn_b1_2_921600,
	pbn_b1_4_921600,
	pbn_b1_8_921600,

	pbn_b1_2_1382400,
	pbn_b1_4_1382400,
	pbn_b1_8_1382400,

	pbn_b2_8_115200,
	pbn_b2_4_460800,
	pbn_b2_8_460800,
	pbn_b2_16_460800,
	pbn_b2_4_921600,
	pbn_b2_8_921600,

	pbn_b2_bt_1_115200,
	pbn_b2_bt_2_115200,
	pbn_b2_bt_4_115200,
	pbn_b2_bt_2_921600,

	pbn_panacom,
	pbn_panacom2,
	pbn_panacom4,
	pbn_plx_romulus,
	pbn_oxsemi,
	pbn_timedia,
	pbn_intel_i960,
	pbn_sgi_ioc3,
	pbn_nec_nile4,

	pbn_dci_pccom4,
	pbn_dci_pccom8,

	pbn_xircom_combo,

	pbn_siig10x_0,
	pbn_siig10x_1,
	pbn_siig10x_2,
	pbn_siig10x_4,
	pbn_siig20x_0,
	pbn_siig20x_2,
	pbn_siig20x_4,
	
	pbn_computone_4,
	pbn_computone_6,
	pbn_computone_8,
};

static struct pci_board pci_boards[] __devinitdata = {
	/*
	 * PCI Flags, Number of Ports, Base (Maximum) Baud Rate,
	 * Offset to get to next UART's registers,
	 * Register shift to use for memory-mapped I/O,
	 * Initialization function, first UART offset
	 */

	/* Generic serial board, pbn_b0_1_115200, pbn_default */
	{ SPCI_FL_BASE0, 1, 115200 },		/* pbn_b0_1_115200,
						   pbn_default */

	{ SPCI_FL_BASE0, 2, 115200 },		/* pbn_b0_2_115200 */
	{ SPCI_FL_BASE0, 4, 115200 },		/* pbn_b0_4_115200 */

	{ SPCI_FL_BASE0, 1, 921600 },		/* pbn_b0_1_921600 */
	{ SPCI_FL_BASE0, 2, 921600 },		/* pbn_b0_2_921600 */
	{ SPCI_FL_BASE0, 4, 921600 },		/* pbn_b0_4_921600 */

	{ SPCI_FL_BASE0 | SPCI_FL_BASE_TABLE, 1, 115200 }, /* pbn_b0_bt_1_115200 */
	{ SPCI_FL_BASE0 | SPCI_FL_BASE_TABLE, 2, 115200 }, /* pbn_b0_bt_2_115200 */
	{ SPCI_FL_BASE0 | SPCI_FL_BASE_TABLE, 8, 115200 }, /* pbn_b0_bt_8_115200 */
	{ SPCI_FL_BASE0 | SPCI_FL_BASE_TABLE, 1, 460800 }, /* pbn_b0_bt_1_460800 */
	{ SPCI_FL_BASE0 | SPCI_FL_BASE_TABLE, 2, 460800 }, /* pbn_b0_bt_2_460800 */

	{ SPCI_FL_BASE1, 1, 115200 },		/* pbn_b1_1_115200 */
	{ SPCI_FL_BASE1, 2, 115200 },		/* pbn_b1_2_115200 */
	{ SPCI_FL_BASE1, 4, 115200 },		/* pbn_b1_4_115200 */
	{ SPCI_FL_BASE1, 8, 115200 },		/* pbn_b1_8_115200 */

	{ SPCI_FL_BASE1, 2, 921600 },		/* pbn_b1_2_921600 */
	{ SPCI_FL_BASE1, 4, 921600 },		/* pbn_b1_4_921600 */
	{ SPCI_FL_BASE1, 8, 921600 },		/* pbn_b1_8_921600 */

	{ SPCI_FL_BASE1, 2, 1382400 },		/* pbn_b1_2_1382400 */
	{ SPCI_FL_BASE1, 4, 1382400 },		/* pbn_b1_4_1382400 */
	{ SPCI_FL_BASE1, 8, 1382400 },		/* pbn_b1_8_1382400 */

	{ SPCI_FL_BASE2, 8, 115200 },		/* pbn_b2_8_115200 */
	{ SPCI_FL_BASE2, 4, 460800 },		/* pbn_b2_4_460800 */
	{ SPCI_FL_BASE2, 8, 460800 },		/* pbn_b2_8_460800 */
	{ SPCI_FL_BASE2, 16, 460800 },		/* pbn_b2_16_460800 */
	{ SPCI_FL_BASE2, 4, 921600 },		/* pbn_b2_4_921600 */
	{ SPCI_FL_BASE2, 8, 921600 },		/* pbn_b2_8_921600 */

	{ SPCI_FL_BASE2 | SPCI_FL_BASE_TABLE, 1, 115200 }, /* pbn_b2_bt_1_115200 */
	{ SPCI_FL_BASE2 | SPCI_FL_BASE_TABLE, 2, 115200 }, /* pbn_b2_bt_2_115200 */
	{ SPCI_FL_BASE2 | SPCI_FL_BASE_TABLE, 4, 115200 }, /* pbn_b2_bt_4_115200 */
	{ SPCI_FL_BASE2 | SPCI_FL_BASE_TABLE, 2, 921600 }, /* pbn_b2_bt_2_921600 */

	{ SPCI_FL_BASE2, 2, 921600, /* IOMEM */		   /* pbn_panacom */
		0x400, 7, pci_plx9050_fn },
	{ SPCI_FL_BASE2 | SPCI_FL_BASE_TABLE, 2, 921600,   /* pbn_panacom2 */
		0x400, 7, pci_plx9050_fn },
	{ SPCI_FL_BASE2 | SPCI_FL_BASE_TABLE, 4, 921600,   /* pbn_panacom4 */
		0x400, 7, pci_plx9050_fn },
	{ SPCI_FL_BASE2, 4, 921600,			   /* pbn_plx_romulus */
		0x20, 2, pci_plx9050_fn, 0x03 },
		/* This board uses the size of PCI Base region 0 to
		 * signal now many ports are available */
	{ SPCI_FL_BASE0 | SPCI_FL_REGION_SZ_CAP, 32, 115200 }, /* pbn_oxsemi */
	{ SPCI_FL_BASE_TABLE, 1, 921600,		   /* pbn_timedia */
		0, 0, pci_timedia_fn },
	/* EKF addition for i960 Boards form EKF with serial port */
	{ SPCI_FL_BASE0, 32, 921600, /* max 256 ports */   /* pbn_intel_i960 */
		8<<2, 2, pci_inteli960ni_fn, 0x10000},
	{ SPCI_FL_BASE0 | SPCI_FL_IRQRESOURCE,		   /* pbn_sgi_ioc3 */
		1, 458333, 0, 0, 0, 0x20178 },

	/*
	 * NEC Vrc-5074 (Nile 4) builtin UART.
	 */
	{ SPCI_FL_BASE0, 1, 520833,			   /* pbn_nec_nile4 */
		64, 3, NULL, 0x300 },

	{ SPCI_FL_BASE3, 4, 115200, 8 },		   /* pbn_dci_pccom4 */
	{ SPCI_FL_BASE3, 8, 115200, 8 },		   /* pbn_dci_pccom8 */

	{ SPCI_FL_BASE0, 1, 115200,			   /* pbn_xircom_combo */
		0, 0, pci_xircom_fn },

	{ SPCI_FL_BASE2, 1, 460800,			   /* pbn_siig10x_0 */
		0, 0, pci_siig10x_fn },
	{ SPCI_FL_BASE2, 1, 921600,			   /* pbn_siig10x_1 */
		0, 0, pci_siig10x_fn },
	{ SPCI_FL_BASE2 | SPCI_FL_BASE_TABLE, 2, 921600,   /* pbn_siig10x_2 */
		0, 0, pci_siig10x_fn },
	{ SPCI_FL_BASE2 | SPCI_FL_BASE_TABLE, 4, 921600,   /* pbn_siig10x_4 */
		0, 0, pci_siig10x_fn },
	{ SPCI_FL_BASE0, 1, 921600,			   /* pbn_siix20x_0 */
		0, 0, pci_siig20x_fn },
	{ SPCI_FL_BASE0 | SPCI_FL_BASE_TABLE, 2, 921600,   /* pbn_siix20x_2 */
		0, 0, pci_siig20x_fn },
	{ SPCI_FL_BASE0 | SPCI_FL_BASE_TABLE, 4, 921600,   /* pbn_siix20x_4 */
		0, 0, pci_siig20x_fn },

	{ SPCI_FL_BASE0, 4, 921600, /* IOMEM */		   /* pbn_computone_4 */
		0x40, 2, NULL, 0x200 },
	{ SPCI_FL_BASE0, 6, 921600, /* IOMEM */		   /* pbn_computone_6 */
		0x40, 2, NULL, 0x200 },
	{ SPCI_FL_BASE0, 8, 921600, /* IOMEM */		   /* pbn_computone_8 */
		0x40, 2, NULL, 0x200 },
};

/*
 * Given a complete unknown PCI device, try to use some heuristics to
 * guess what the configuration might be, based on the pitiful PCI
 * serial specs.  Returns 0 on success, 1 on failure.
 */
static int __devinit
serial_pci_guess_board(struct pci_dev *dev, struct pci_board *board)
{
	int num_iomem = 0, num_port = 0, first_port = -1;
	int i;
	
	/*
	 * If it is not a communications device or the programming
	 * interface is greater than 6, give up.
	 *
	 * (Should we try to make guesses for multiport serial devices
	 * later?) 
	 */
	if ((((dev->class >> 8) != PCI_CLASS_COMMUNICATION_SERIAL) &&
	    ((dev->class >> 8) != PCI_CLASS_COMMUNICATION_MODEM)) ||
	    (dev->class & 0xff) > 6)
		return 1;

	for (i = 0; i < 6; i++) {
		if (IS_PCI_REGION_IOPORT(dev, i)) {
			num_port++;
			if (first_port == -1)
				first_port = i;
		}
		if (IS_PCI_REGION_IOMEM(dev, i))
			num_iomem++;
	}

	/*
	 * If there is 1 or 0 iomem regions, and exactly one port, use
	 * it.
	 */
	if (num_iomem <= 1 && num_port == 1) {
		board->flags = first_port;
		return 0;
	}
	return 1;
}

/*
 * return an error code to refuse.
 *
 * serial_struct is 60 bytes.
 */
static int __devinit pci_init_one(struct pci_dev *dev, const struct pci_device_id *ent)
{
	struct serial_private *priv;
	struct pci_board *board, tmp;
	struct serial_struct serial_req;
	int base_baud, rc, k;

	if (ent->driver_data >= ARRAY_SIZE(pci_boards)) {
		printk(KERN_ERR "pci_init_one: invalid driver_data: %ld\n",
			ent->driver_data);
		return -EINVAL;
	}

	board = &pci_boards[ent->driver_data];

	rc = pci_enable_device(dev);
	if (rc)
		return rc;

	if (ent->driver_data == pbn_default &&
	    serial_pci_guess_board(dev, board))
		return -ENODEV;
	else if (serial_pci_guess_board(dev, &tmp) == 0) {
		printk(KERN_INFO "Redundant entry in serial pci_table.  "
		       "Please send the output of\n"
		       "lspci -vv, this message (%d,%d,%d,%d)\n"
		       "and the manufacturer and name of "
		       "serial board or modem board\n"
		       "to serial-pci-info@lists.sourceforge.net.\n",
		       dev->vendor, dev->device,
		       pci_get_subvendor(dev), pci_get_subdevice(dev));
	}

	priv = kmalloc(sizeof(struct serial_private) +
			      sizeof(unsigned int) * board->num_ports,
			      GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/*
	 * Run the initialization function, if any
	 */
	if (board->init_fn) {
		rc = board->init_fn(dev, board, 1);
		if (rc != 0) {
			kfree(priv);
			return rc;
		}
	}

	base_baud = board->base_baud;
	if (!base_baud)
		base_baud = BASE_BAUD;
	memset(&serial_req, 0, sizeof(serial_req));
	for (k = 0; k < board->num_ports; k++) {
		serial_req.irq = get_pci_irq(dev, board, k);
		if (get_pci_port(dev, board, &serial_req, k))
			break;
#ifdef SERIAL_DEBUG_PCI
		printk("Setup PCI/PNP port: port %x, irq %d, type %d\n",
		       serial_req.port, serial_req.irq, serial_req.io_type);
#endif
		serial_req.flags = ASYNC_SKIP_TEST | ASYNC_AUTOPROBE;
		serial_req.baud_base = base_baud;
		
		priv->line[k] = register_serial(&serial_req);
		if (priv->line[k] < 0)
			break;
	}

	priv->board = board;
	priv->nr = k;

	pci_set_drvdata(dev, priv);

	return 0;
}

static void __devexit pci_remove_one(struct pci_dev *dev)
{
	struct serial_private *priv = pci_get_drvdata(dev);
	int i;

	pci_set_drvdata(dev, NULL);

	if (priv) {
		for (i = 0; i < priv->nr; i++)
			unregister_serial(priv->line[i]);

		priv->board->init_fn(dev, priv->board, 0);

		pci_disable_device(dev);

		kfree(priv);
	}
}

static struct pci_device_id serial_pci_tbl[] __devinitdata = {
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V960,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_232, 0, 0,
		pbn_b1_8_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V960,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH4_232, 0, 0,
		pbn_b1_4_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V960,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH2_232, 0, 0,
		pbn_b1_2_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_232, 0, 0,
		pbn_b1_8_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH4_232, 0, 0,
		pbn_b1_4_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH2_232, 0, 0,
		pbn_b1_2_1382400 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_485, 0, 0,
		pbn_b1_8_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_485_4_4, 0, 0,
		pbn_b1_8_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH4_485, 0, 0,
		pbn_b1_4_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH4_485_2_2, 0, 0,
		pbn_b1_4_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH2_485, 0, 0,
		pbn_b1_2_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH8_485_2_6, 0, 0,
		pbn_b1_8_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH081101V1, 0, 0,
		pbn_b1_8_921600 },
	{	PCI_VENDOR_ID_V3, PCI_DEVICE_ID_V3_V351,
		PCI_SUBVENDOR_ID_CONNECT_TECH,
		PCI_SUBDEVICE_ID_CONNECT_TECH_BH041101V1, 0, 0,
		pbn_b1_4_921600 },

	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_U530,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_1_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_UCOMM2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_2_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_UCOMM422,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_4_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_UCOMM232,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_2_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_COMM4,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_bt_4_115200 },
	{	PCI_VENDOR_ID_SEALEVEL, PCI_DEVICE_ID_SEALEVEL_COMM8,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_8_115200 },

	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_GTEK_SERIAL2,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_2_115200 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_SPCOM200,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_2_921600 },
	/* VScom SPCOM800, from sl@s.pl */
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_SPCOM800, 
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_8_921600 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_1077,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b2_4_921600 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_KEYSPAN,
		PCI_SUBDEVICE_ID_KEYSPAN_SX2, 0, 0,
		pbn_panacom },
	{	PCI_VENDOR_ID_PANACOM, PCI_DEVICE_ID_PANACOM_QUADMODEM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_panacom4 },
	{	PCI_VENDOR_ID_PANACOM, PCI_DEVICE_ID_PANACOM_DUALMODEM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_panacom2 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIFAST,
		PCI_SUBDEVICE_ID_CHASE_PCIFAST4, 0, 0, 
		pbn_b2_4_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIFAST,
		PCI_SUBDEVICE_ID_CHASE_PCIFAST8, 0, 0, 
		pbn_b2_8_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIFAST,
		PCI_SUBDEVICE_ID_CHASE_PCIFAST16, 0, 0, 
		pbn_b2_16_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIFAST,
		PCI_SUBDEVICE_ID_CHASE_PCIFAST16FMC, 0, 0, 
		pbn_b2_16_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIRAS,
		PCI_SUBDEVICE_ID_CHASE_PCIRAS4, 0, 0, 
		pbn_b2_4_460800 },
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_SUBVENDOR_ID_CHASE_PCIRAS,
		PCI_SUBDEVICE_ID_CHASE_PCIRAS8, 0, 0, 
		pbn_b2_8_460800 },
	/* Megawolf Romulus PCI Serial Card, from Mike Hudson */
	/* (Exoray@isys.ca) */
	{	PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_ROMULUS,
		0x10b5, 0x106a, 0, 0,
		pbn_plx_romulus },
	{	PCI_VENDOR_ID_QUATECH, PCI_DEVICE_ID_QUATECH_QSC100,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b1_4_115200 },
	{	PCI_VENDOR_ID_QUATECH, PCI_DEVICE_ID_QUATECH_DSC100,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b1_2_115200 },
	{	PCI_VENDOR_ID_QUATECH, PCI_DEVICE_ID_QUATECH_ESC100D,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b1_8_115200 },
	{	PCI_VENDOR_ID_QUATECH, PCI_DEVICE_ID_QUATECH_ESC100M,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b1_8_115200 },
	{	PCI_VENDOR_ID_SPECIALIX, PCI_DEVICE_ID_OXSEMI_16PCI954,
		PCI_VENDOR_ID_SPECIALIX, PCI_SUBDEVICE_ID_SPECIALIX_SPEED4, 0, 0, 
		pbn_b0_4_921600 },
	{	PCI_VENDOR_ID_OXSEMI, PCI_DEVICE_ID_OXSEMI_16PCI954,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_4_115200 },
	{	PCI_VENDOR_ID_OXSEMI, PCI_DEVICE_ID_OXSEMI_16PCI952,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_2_115200 },

	/* Digitan DS560-558, from jimd@esoft.com */
	{	PCI_VENDOR_ID_ATT, PCI_DEVICE_ID_ATT_VENUS_MODEM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b1_1_115200 },

	/* 3Com US Robotics 56k Voice Internal PCI model 5610 */
	{	PCI_VENDOR_ID_USR, 0x1008,
		PCI_ANY_ID, PCI_ANY_ID, },

	/* Titan Electronic cards */
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_100,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_1_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_200,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_2_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_400,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_4_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_800B,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
		pbn_b0_4_921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_100L,
		PCI_ANY_ID, PCI_ANY_ID,
		SPCI_FL_BASE1, 1, 921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_200L,
		PCI_ANY_ID, PCI_ANY_ID,
		SPCI_FL_BASE1 | SPCI_FL_BASE_TABLE, 2, 921600 },
	/* The 400L and 800L have a custom hack in get_pci_port */
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_400L,
		PCI_ANY_ID, PCI_ANY_ID,
		SPCI_FL_BASE_TABLE, 4, 921600 },
	{	PCI_VENDOR_ID_TITAN, PCI_DEVICE_ID_TITAN_800L,
		PCI_ANY_ID, PCI_ANY_ID,
		SPCI_FL_BASE_TABLE, 8, 921600 },

	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_10x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_10x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_10x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_10x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_1 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_10x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_1 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_10x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_1 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_10x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_10x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_10x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_10x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_10x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_10x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_10x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_4 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_10x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_4 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_10x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig10x_4 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_20x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_20x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S_20x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_20x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_20x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_20x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P1S_20x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P1S_20x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P1S_20x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_0 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_20x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_20x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S_20x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_20x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_20x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_20x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_2 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_20x_550,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_4 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_20x_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_4 },
	{	PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_4S_20x_850,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_siig20x_4 },

	/* Computone devices submitted by Doug McNash dmcnash@computone.com */
	{	PCI_VENDOR_ID_COMPUTONE, PCI_DEVICE_ID_COMPUTONE_PG,
		PCI_SUBVENDOR_ID_COMPUTONE, PCI_SUBDEVICE_ID_COMPUTONE_PG4,
		0, 0, pbn_computone_4 },
	{	PCI_VENDOR_ID_COMPUTONE, PCI_DEVICE_ID_COMPUTONE_PG,
		PCI_SUBVENDOR_ID_COMPUTONE, PCI_SUBDEVICE_ID_COMPUTONE_PG8,
		0, 0, pbn_computone_8 },
	{	PCI_VENDOR_ID_COMPUTONE, PCI_DEVICE_ID_COMPUTONE_PG,
		PCI_SUBVENDOR_ID_COMPUTONE, PCI_SUBDEVICE_ID_COMPUTONE_PG6,
		0, 0, pbn_computone_6 },

	{	PCI_VENDOR_ID_OXSEMI, PCI_DEVICE_ID_OXSEMI_16PCI95N,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, pbn_oxsemi },
	{	PCI_VENDOR_ID_TIMEDIA, PCI_DEVICE_ID_TIMEDIA_1889,
		PCI_VENDOR_ID_TIMEDIA, PCI_ANY_ID, 0, 0, pbn_timedia },

	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_DSERIAL,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_115200 },
	/* AFAVLAB serial card, from Harald Welte <laforge@gnumonks.org> */
	{	PCI_VENDOR_ID_AFAVLAB, PCI_DEVICE_ID_AFAVLAB_P028,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_8_115200 },

	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_QUATRO_A,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_115200 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_QUATRO_B,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_115200 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_PORT_PLUS,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_460800 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_QUAD_A,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_460800 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_QUAD_B,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_2_460800 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_SSERIAL,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_1_115200 },
	{	PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_PORT_650,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b0_bt_1_460800 },

	/* RAStel 2 port modem, gerg@moreton.com.au */
	{	PCI_VENDOR_ID_MORETON, PCI_DEVICE_ID_RASTEL_2PORT,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_b2_bt_2_115200 },

	/* EKF addition for i960 Boards form EKF with serial port */
	{	PCI_VENDOR_ID_INTEL, 0x1960,
		0xE4BF, PCI_ANY_ID, 0, 0,
		pbn_intel_i960 },

	/* Xircom Cardbus/Ethernet combos */
	{	PCI_VENDOR_ID_XIRCOM, PCI_DEVICE_ID_XIRCOM_X3201_MDM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_xircom_combo },

	/*
	 * Untested PCI modems, sent in from various folks...
	 */

	/* Elsa Model 56K PCI Modem, from Andreas Rath <arh@01019freenet.de> */
	{	PCI_VENDOR_ID_ROCKWELL, 0x1004,
		0x1048, 0x1500, 0, 0,
		pbn_b1_1_115200 },

	{	PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3,
		0xFF00, 0, 0, 0,
		pbn_sgi_ioc3 },

	/*
	 * NEC Vrc-5074 (Nile 4) builtin UART.
	 */
	{	PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_NILE4,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_nec_nile4 },

	{	PCI_VENDOR_ID_DCI, PCI_DEVICE_ID_DCI_PCCOM4,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_dci_pccom4 },
	{	PCI_VENDOR_ID_DCI, PCI_DEVICE_ID_DCI_PCCOM8,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		pbn_dci_pccom8 },

	{	PCI_ANY_ID, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_COMMUNICATION_SERIAL << 8,
		0xffff00, },
	{	PCI_ANY_ID, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_COMMUNICATION_MODEM << 8,
		0xffff00, },
	{	PCI_ANY_ID, PCI_ANY_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_COMMUNICATION_MULTISERIAL << 8,
		0xffff00, },
	{ 0, }
};

static struct pci_driver serial_pci_driver = {
	.name		= "serial",
	.probe		= pci_init_one,
	.remove		= __devexit_p(pci_remove_one),
	.id_table	= serial_pci_tbl,
};

static int __init serial8250_pci_init(void)
{
	return pci_module_init(&serial_pci_driver);
}

static void __exit serial8250_pci_exit(void)
{
	pci_unregister_driver(&serial_pci_driver);
}

module_init(serial8250_pci_init);
module_exit(serial8250_pci_exit);

EXPORT_NO_SYMBOLS;

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic 8250/16x50 PCI serial probe module");
MODULE_DEVICE_TABLE(pci, serial_pci_tbl);
