/*
	drivers/net/tulip/media.c

	Maintained by Jeff Garzik <jgarzik@mandrakesoft.com>
	Copyright 2000  The Linux Kernel Team
	Written/copyright 1994-1999 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	Please refer to Documentation/networking/tulip.txt for more
	information on this driver.

*/

#include "tulip.h"


/* This is a mysterious value that can be written to CSR11 in the 21040 (only)
   to support a pre-NWay full-duplex signaling mechanism using short frames.
   No one knows what it should be, but if left at its default value some
   10base2(!) packets trigger a full-duplex-request interrupt. */
#define FULL_DUPLEX_MAGIC	0x6969


/* MII transceiver control section.
   Read and write the MII registers using software-generated serial
   MDIO protocol.  See the MII specifications or DP83840A data sheet
   for details. */

int tulip_mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;
	int read_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int retval = 0;
	long ioaddr = dev->base_addr;
	long mdio_addr = ioaddr + CSR9;

	if (tp->chip_id == LC82C168) {
		int i = 1000;
		outl(0x60020000 + (phy_id<<23) + (location<<18), ioaddr + 0xA0);
		inl(ioaddr + 0xA0);
		inl(ioaddr + 0xA0);
		while (--i > 0)
			if ( ! ((retval = inl(ioaddr + 0xA0)) & 0x80000000))
				return retval & 0xffff;
		return 0xffff;
	}

	if (tp->chip_id == COMET) {
		if (phy_id == 1) {
			if (location < 7)
				return inl(ioaddr + 0xB4 + (location<<2));
			else if (location == 17)
				return inl(ioaddr + 0xD0);
			else if (location >= 29 && location <= 31)
				return inl(ioaddr + 0xD4 + ((location-29)<<2));
		}
		return 0xffff;
	}

	/* Establish sync by sending at least 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((inl(mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	return (retval>>1) & 0xffff;
}

void tulip_mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;
	int cmd = (0x5002 << 16) | (phy_id << 23) | (location<<18) | value;
	long ioaddr = dev->base_addr;
	long mdio_addr = ioaddr + CSR9;

	if (tp->chip_id == LC82C168) {
		int i = 1000;
		outl(cmd, ioaddr + 0xA0);
		do
			if ( ! (inl(ioaddr + 0xA0) & 0x80000000))
				break;
		while (--i > 0);
		return;
	}

	if (tp->chip_id == COMET) {
		if (phy_id != 1)
			return;
		if (location < 7)
			outl(value, ioaddr + 0xB4 + (location<<2));
		else if (location == 17)
			outl(value, ioaddr + 0xD0);
		else if (location >= 29 && location <= 31)
			outl(value, ioaddr + 0xD4 + ((location-29)<<2));
		return;
	}

	/* Establish sync by sending 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;
		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
}


/* Set up the transceiver control registers for the selected media type. */
void tulip_select_media(struct net_device *dev, int startup)
{
	long ioaddr = dev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	struct mediatable *mtable = tp->mtable;
	u32 new_csr6;
	int i;

	if (mtable) {
		struct medialeaf *mleaf = &mtable->mleaf[tp->cur_index];
		unsigned char *p = mleaf->leafdata;
		switch (mleaf->type) {
		case 0:					/* 21140 non-MII xcvr. */
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: Using a 21140 non-MII transceiver"
					   " with control setting %2.2x.\n",
					   dev->name, p[1]);
			dev->if_port = p[0];
			if (startup)
				outl(mtable->csr12dir | 0x100, ioaddr + CSR12);
			outl(p[1], ioaddr + CSR12);
			new_csr6 = 0x02000000 | ((p[2] & 0x71) << 18);
			break;
		case 2: case 4: {
			u16 setup[5];
			u32 csr13val, csr14val, csr15dir, csr15val;
			for (i = 0; i < 5; i++)
				setup[i] = get_u16(&p[i*2 + 1]);

			dev->if_port = p[0] & 15;
			if (tulip_media_cap[dev->if_port] & MediaAlwaysFD)
				tp->full_duplex = 1;

			if (startup && mtable->has_reset) {
				struct medialeaf *rleaf = &mtable->mleaf[mtable->has_reset];
				unsigned char *rst = rleaf->leafdata;
				if (tulip_debug > 1)
					printk(KERN_DEBUG "%s: Resetting the transceiver.\n",
						   dev->name);
				for (i = 0; i < rst[0]; i++)
					outl(get_u16(rst + 1 + (i<<1)) << 16, ioaddr + CSR15);
			}
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: 21143 non-MII %s transceiver control "
					   "%4.4x/%4.4x.\n",
					   dev->name, medianame[dev->if_port], setup[0], setup[1]);
			if (p[0] & 0x40) {	/* SIA (CSR13-15) setup values are provided. */
				csr13val = setup[0];
				csr14val = setup[1];
				csr15dir = (setup[3]<<16) | setup[2];
				csr15val = (setup[4]<<16) | setup[2];
				outl(0, ioaddr + CSR13);
				outl(csr14val, ioaddr + CSR14);
				outl(csr15dir, ioaddr + CSR15);	/* Direction */
				outl(csr15val, ioaddr + CSR15);	/* Data */
				outl(csr13val, ioaddr + CSR13);
			} else {
				csr13val = 1;
				csr14val = 0x0003FF7F;
				csr15dir = (setup[0]<<16) | 0x0008;
				csr15val = (setup[1]<<16) | 0x0008;
				if (dev->if_port <= 4)
					csr14val = t21142_csr14[dev->if_port];
				if (startup) {
					outl(0, ioaddr + CSR13);
					outl(csr14val, ioaddr + CSR14);
				}
				outl(csr15dir, ioaddr + CSR15);	/* Direction */
				outl(csr15val, ioaddr + CSR15);	/* Data */
				if (startup) outl(csr13val, ioaddr + CSR13);
			}
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s:  Setting CSR15 to %8.8x/%8.8x.\n",
					   dev->name, csr15dir, csr15val);
			if (mleaf->type == 4)
				new_csr6 = 0x82020000 | ((setup[2] & 0x71) << 18);
			else
				new_csr6 = 0x82420000;
			break;
		}
		case 1: case 3: {
			int phy_num = p[0];
			int init_length = p[1];
			u16 *misc_info;
			u16 to_advertise;

			dev->if_port = 11;
			new_csr6 = 0x020E0000;
			if (mleaf->type == 3) {	/* 21142 */
				u16 *init_sequence = (u16*)(p+2);
				u16 *reset_sequence = &((u16*)(p+3))[init_length];
				int reset_length = p[2 + init_length*2];
				misc_info = reset_sequence + reset_length;
				if (startup)
					for (i = 0; i < reset_length; i++)
						outl(get_u16(&reset_sequence[i]) << 16, ioaddr + CSR15);
				for (i = 0; i < init_length; i++)
					outl(get_u16(&init_sequence[i]) << 16, ioaddr + CSR15);
			} else {
				u8 *init_sequence = p + 2;
				u8 *reset_sequence = p + 3 + init_length;
				int reset_length = p[2 + init_length];
				misc_info = (u16*)(reset_sequence + reset_length);
				if (startup) {
					outl(mtable->csr12dir | 0x100, ioaddr + CSR12);
					for (i = 0; i < reset_length; i++)
						outl(reset_sequence[i], ioaddr + CSR12);
				}
				for (i = 0; i < init_length; i++)
					outl(init_sequence[i], ioaddr + CSR12);
			}
			to_advertise = (get_u16(&misc_info[1]) & tp->to_advertise) | 1;
			tp->advertising[phy_num] = to_advertise;
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s:  Advertising %4.4x on PHY %d (%d).\n",
					   dev->name, to_advertise, phy_num, tp->phys[phy_num]);
			/* Bogus: put in by a committee?  */
			tulip_mdio_write(dev, tp->phys[phy_num], 4, to_advertise);
			break;
		}
		case 5: case 6: {
			u16 setup[5];
			u32 csr13val, csr14val, csr15dir, csr15val;
			for (i = 0; i < 5; i++)
				setup[i] = get_u16(&p[i*2 + 1]);

			if (startup && mtable->has_reset) {
				struct medialeaf *rleaf = &mtable->mleaf[mtable->has_reset];
				unsigned char *rst = rleaf->leafdata;
				if (tulip_debug > 1)
					printk(KERN_DEBUG "%s: Resetting the transceiver.\n",
						   dev->name);
				for (i = 0; i < rst[0]; i++)
					outl(get_u16(rst + 1 + (i<<1)) << 16, ioaddr + CSR15);
			}

			break;
		}
		default:
			printk(KERN_DEBUG "%s:  Invalid media table selection %d.\n",
					   dev->name, mleaf->type);
			new_csr6 = 0x020E0000;
		}
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: Using media type %s, CSR12 is %2.2x.\n",
				   dev->name, medianame[dev->if_port],
				   inl(ioaddr + CSR12) & 0xff);
	} else if (tp->chip_id == DC21041) {
		int port = dev->if_port <= 4 ? dev->if_port : 0;
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: 21041 using media %s, CSR12 is %4.4x.\n",
				   dev->name, medianame[port == 3 ? 12: port],
				   inl(ioaddr + CSR12));
		outl(0x00000000, ioaddr + CSR13); /* Reset the serial interface */
		outl(t21041_csr14[port], ioaddr + CSR14);
		outl(t21041_csr15[port], ioaddr + CSR15);
		outl(t21041_csr13[port], ioaddr + CSR13);
		new_csr6 = 0x80020000;
	} else if (tp->chip_id == LC82C168) {
		if (startup && ! tp->medialock)
			dev->if_port = tp->mii_cnt ? 11 : 0;
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: PNIC PHY status is %3.3x, media %s.\n",
				   dev->name, inl(ioaddr + 0xB8), medianame[dev->if_port]);
		if (tp->mii_cnt) {
			new_csr6 = 0x810C0000;
			outl(0x0001, ioaddr + CSR15);
			outl(0x0201B07A, ioaddr + 0xB8);
		} else if (startup) {
			/* Start with 10mbps to do autonegotiation. */
			outl(0x32, ioaddr + CSR12);
			new_csr6 = 0x00420000;
			outl(0x0001B078, ioaddr + 0xB8);
			outl(0x0201B078, ioaddr + 0xB8);
		} else if (dev->if_port == 3  ||  dev->if_port == 5) {
			outl(0x33, ioaddr + CSR12);
			new_csr6 = 0x01860000;
			/* Trigger autonegotiation. */
			outl(startup ? 0x0201F868 : 0x0001F868, ioaddr + 0xB8);
		} else {
			outl(0x32, ioaddr + CSR12);
			new_csr6 = 0x00420000;
			outl(0x1F078, ioaddr + 0xB8);
		}
	} else if (tp->chip_id == DC21040) {					/* 21040 */
		/* Turn on the xcvr interface. */
		int csr12 = inl(ioaddr + CSR12);
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: 21040 media type is %s, CSR12 is %2.2x.\n",
				   dev->name, medianame[dev->if_port], csr12);
		if (tulip_media_cap[dev->if_port] & MediaAlwaysFD)
			tp->full_duplex = 1;
		new_csr6 = 0x20000;
		/* Set the full duplux match frame. */
		outl(FULL_DUPLEX_MAGIC, ioaddr + CSR11);
		outl(0x00000000, ioaddr + CSR13); /* Reset the serial interface */
		if (t21040_csr13[dev->if_port] & 8) {
			outl(0x0705, ioaddr + CSR14);
			outl(0x0006, ioaddr + CSR15);
		} else {
			outl(0xffff, ioaddr + CSR14);
			outl(0x0000, ioaddr + CSR15);
		}
		outl(0x8f01 | t21040_csr13[dev->if_port], ioaddr + CSR13);
	} else {					/* Unknown chip type with no media table. */
		if (tp->default_port == 0)
			dev->if_port = tp->mii_cnt ? 11 : 3;
		if (tulip_media_cap[dev->if_port] & MediaIsMII) {
			new_csr6 = 0x020E0000;
		} else if (tulip_media_cap[dev->if_port] & MediaIsFx) {
			new_csr6 = 0x028600000;
		} else
			new_csr6 = 0x038600000;
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: No media description table, assuming "
				   "%s transceiver, CSR12 %2.2x.\n",
				   dev->name, medianame[dev->if_port],
				   inl(ioaddr + CSR12));
	}

	tp->csr6 = new_csr6 | (tp->csr6 & 0xfdff) | (tp->full_duplex ? 0x0200 : 0);
	return;
}

/*
  Check the MII negotiated duplex, and change the CSR6 setting if
  required.
  Return 0 if everything is OK.
  Return < 0 if the transceiver is missing or has no link beat.
  */
int tulip_check_duplex(struct net_device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int mii_reg1, mii_reg5, negotiated, duplex;

	if (tp->full_duplex_lock)
		return 0;
	mii_reg1 = tulip_mdio_read(dev, tp->phys[0], 1);
	mii_reg5 = tulip_mdio_read(dev, tp->phys[0], 5);
	if (tulip_debug > 1)
		printk(KERN_INFO "%s: MII status %4.4x, Link partner report "
			   "%4.4x.\n", dev->name, mii_reg1, mii_reg5);
	if (mii_reg1 == 0xffff)
		return -2;
	if ((mii_reg1 & 0x0004) == 0) {
		int new_reg1 = tulip_mdio_read(dev, tp->phys[0], 1);
		if ((new_reg1 & 0x0004) == 0) {
			if (tulip_debug  > 1)
				printk(KERN_INFO "%s: No link beat on the MII interface,"
					   " status %4.4x.\n", dev->name, new_reg1);
			return -1;
		}
	}
	negotiated = mii_reg5 & tp->advertising[0];
	duplex = ((negotiated & 0x0300) == 0x0100
			  || (negotiated & 0x00C0) == 0x0040);
	/* 100baseTx-FD  or  10T-FD, but not 100-HD */
	if (tp->full_duplex != duplex) {
		tp->full_duplex = duplex;
		if (negotiated & 0x038)	/* 100mbps. */
			tp->csr6 &= ~0x00400000;
		if (tp->full_duplex) tp->csr6 |= 0x0200;
		else				 tp->csr6 &= ~0x0200;
		tulip_restart_rxtx(tp, tp->csr6);
		if (tulip_debug > 0)
			printk(KERN_INFO "%s: Setting %s-duplex based on MII"
				   "#%d link partner capability of %4.4x.\n",
				   dev->name, tp->full_duplex ? "full" : "half",
				   tp->phys[0], mii_reg5);
		return 1;
	}
	return 0;
}

