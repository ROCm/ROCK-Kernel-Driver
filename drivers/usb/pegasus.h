/*
 *  Copyright (c) 1999,2000 Petko Manolov - Petkan (petkan@dce.bg)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


#ifndef	PEGASUS_DEV

#define	PEGASUS_II		0x80000000
#define	HAS_HOME_PNA		0x40000000

#define	PEGASUS_MTU		1500
#define	PEGASUS_MAX_MTU		1536

#define	EPROM_WRITE		0x01
#define	EPROM_READ		0x02
#define	EPROM_DONE		0x04
#define	EPROM_WR_ENABLE		0x10
#define	EPROM_LOAD		0x20

#define	MII_BMCR		0x00
#define	MII_BMSR		0x01
#define	BMSR_MEDIA		0x7808
#define	MII_ANLPA		0x05
#define	ANLPA_100TX_FD		0x0100
#define	ANLPA_100TX_HD		0x0080
#define	ANLPA_10T_FD		0x0040
#define	ANLPA_10T_HD		0x0020
#define	PHY_DONE		0x80
#define	PHY_READ		0x40
#define	PHY_WRITE		0x20
#define	DEFAULT_GPIO_RESET	0x24
#define	LINKSYS_GPIO_RESET	0x24
#define	DEFAULT_GPIO_SET	0x26

#define	PEGASUS_PRESENT		0x00000001
#define	PEGASUS_RUNNING		0x00000002
#define	PEGASUS_TX_BUSY		0x00000004
#define	PEGASUS_RX_BUSY		0x00000008
#define	CTRL_URB_RUNNING	0x00000010
#define	CTRL_URB_SLEEP		0x00000020
#define	PEGASUS_UNPLUG		0x00000040
#define	ETH_REGS_CHANGE		0x40000000
#define	ETH_REGS_CHANGED	0x80000000

#define	RX_MULTICAST		2
#define	RX_PROMISCUOUS		4

#define	REG_TIMEOUT		(HZ)
#define	PEGASUS_TX_TIMEOUT	(HZ*10)

#define	TX_UNDERRUN		0x80
#define	EXCESSIVE_COL		0x40
#define	LATE_COL		0x20
#define	NO_CARRIER		0x10
#define	LOSS_CARRIER		0x08
#define	JABBER_TIMEOUT		0x04

#define	PEGASUS_REQT_READ	0xc0
#define	PEGASUS_REQT_WRITE	0x40
#define	PEGASUS_REQ_GET_REGS	0xf0
#define	PEGASUS_REQ_SET_REGS	0xf1
#define	PEGASUS_REQ_SET_REG	PEGASUS_REQ_SET_REGS
#define	ALIGN(x)		x __attribute__((aligned(L1_CACHE_BYTES)))

enum pegasus_registers {
	EthCtrl0 = 0,
	EthCtrl1 = 1,
	EthCtrl2 = 2,
	EthID = 0x10,
	Reg1d = 0x1d,
	EpromOffset = 0x20,
	EpromData = 0x21,	/* 0x21 low, 0x22 high byte */
	EpromCtrl = 0x23,
	PhyAddr = 0x25,
	PhyData = 0x26, 	/* 0x26 low, 0x27 high byte */
	PhyCtrl = 0x28,
	UsbStst = 0x2a,
	EthTxStat0 = 0x2b,
	EthTxStat1 = 0x2c,
	EthRxStat = 0x2d,
	Reg7b = 0x7b,
	Gpio0 = 0x7e,
	Gpio1 = 0x7f,
	Reg81 = 0x81,
};


typedef struct pegasus {
	struct usb_device	*usb;
	struct net_device	*net;
	struct net_device_stats	stats;
	unsigned		flags;
	unsigned		features;
	int			dev_index;
	int			intr_interval;
	struct urb		ctrl_urb, rx_urb, tx_urb, intr_urb;
	devrequest		dr;
	wait_queue_head_t	ctrl_wait;
	struct semaphore	ctrl_sem;
	unsigned char		ALIGN(rx_buff[PEGASUS_MAX_MTU]);
	unsigned char		ALIGN(tx_buff[PEGASUS_MAX_MTU]);
	unsigned char		ALIGN(intr_buff[8]);
	__u8			eth_regs[4];
	__u8			phy;
	__u8			gpio_res;
} pegasus_t;


struct usb_eth_dev {
	char	*name;
	__u16	vendor;
	__u16	device;
	__u32	private; /* LSB is gpio reset value */
};


#define VENDOR_ACCTON           0x083a
#define VENDOR_ADMTEK           0x07a6
#define VENDOR_BILLIONTON       0x08dd
#define VENDOR_COREGA           0x07aa
#define VENDOR_DLINK1           0x2001
#define VENDOR_DLINK2           0x07b8
#define VENDOR_IODATA           0x04bb
#define VENDOR_LANEED           0x056e
#define VENDOR_LINKSYS          0x066b
#define VENDOR_MELCO            0x0411
#define VENDOR_SMC              0x0707
#define VENDOR_SOHOWARE         0x15e8


#else	/* PEGASUS_DEV */


PEGASUS_DEV( "Accton USB 10/100 Ethernet Adapter", VENDOR_ACCTON, 0x1046,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "ADMtek ADM8511 \"Pegasus II\" USB Ethernet",
		VENDOR_ADMTEK, 0x8511,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "ADMtek AN986 \"Pegasus\" USB Ethernet (eval board)",
		VENDOR_ADMTEK, 0x0986,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "Billionton USB-100", VENDOR_BILLIONTON, 0x0986,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "Billionton USBLP-100", VENDOR_BILLIONTON, 0x0987,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "Billionton USBEL-100", VENDOR_BILLIONTON, 0x0988,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "Billionton USBE-100", VENDOR_BILLIONTON, 0x8511,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "Corega FEter USB-TX", VENDOR_COREGA, 0x0004,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "D-Link DSB-650TX", VENDOR_DLINK1, 0x4001,
		LINKSYS_GPIO_RESET )
PEGASUS_DEV( "D-Link DSB-650TX", VENDOR_DLINK1, 0x4002,
		LINKSYS_GPIO_RESET )
PEGASUS_DEV( "D-Link DSB-650TX(PNA)", VENDOR_DLINK1, 0x4003,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "D-Link DSB-650", VENDOR_DLINK1, 0xabc1,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "D-Link DU-E10", VENDOR_DLINK2, 0xabc1,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "D-Link DU-E100", VENDOR_DLINK2, 0x4002,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "IO DATA USB ET/TX", VENDOR_IODATA, 0x0904,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "LANEED USB Ethernet LD-USB/TX", VENDOR_LANEED, 0x4002,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "Linksys USB10TX", VENDOR_LINKSYS, 0x2202,
		LINKSYS_GPIO_RESET )
PEGASUS_DEV( "Linksys USB100TX", VENDOR_LINKSYS, 0x2203,
		LINKSYS_GPIO_RESET )
PEGASUS_DEV( "Linksys USB100TX", VENDOR_LINKSYS, 0x2204,
		LINKSYS_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "Linksys USB Ethernet Adapter", VENDOR_LINKSYS, 0x2206,
		LINKSYS_GPIO_RESET )
PEGASUS_DEV( "MELCO/BUFFALO LUA-TX", VENDOR_MELCO, 0x0001,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "SMC 202 USB Ethernet", VENDOR_SMC, 0x0200,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "SOHOware NUB100 Ethernet", VENDOR_SOHOWARE, 0x9100,
		DEFAULT_GPIO_RESET )


#endif	/* PEGASUS_DEV */
