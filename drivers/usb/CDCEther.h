// Portions of this file taken from 
// Petko Manolov - Petkan (petkan@dce.bg)
// from his driver pegasus.h

/*
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


#define CS_INTERFACE			0x24

#define	CDC_ETHER_MAX_MTU		1536

#define	CDC_ETHER_PRESENT		0x00000001
#define	CDC_ETHER_RUNNING		0x00000002
#define	CDC_ETHER_TX_BUSY		0x00000004
#define	CDC_ETHER_RX_BUSY		0x00000008
#define	CDC_ETHER_UNPLUG		0x00000040

#define	CDC_ETHER_TX_TIMEOUT	(HZ*10)

#define	TX_UNDERRUN				0x80
#define	EXCESSIVE_COL			0x40
#define	LATE_COL				0x20
#define	NO_CARRIER				0x10
#define	LOSS_CARRIER			0x08
#define	JABBER_TIMEOUT			0x04

#define	CDC_ETHER_REQT_READ		0xc0
#define	CDC_ETHER_REQT_WRITE	0x40
#define	CDC_ETHER_REQ_GET_REGS	0xf0
#define	CDC_ETHER_REQ_SET_REGS	0xf1
#define	CDC_ETHER_REQ_SET_REG	PIPERIDER_REQ_SET_REGS
#define	ALIGN(x)		x __attribute__((aligned(L1_CACHE_BYTES)))

#define MODE_FLAG_PROMISCUOUS   (1<<0)
#define MODE_FLAG_ALL_MULTICAST (1<<1)
#define MODE_FLAG_DIRECTED      (1<<2)
#define MODE_FLAG_BROADCAST     (1<<3)
#define MODE_FLAG_MULTICAST     (1<<4)

#define SET_ETHERNET_MULTICAST_FILTER    0x40
#define SET_ETHERNET_PACKET_FILTER       0x43

typedef struct _ether_dev_t {
	struct usb_device	*usb;
	struct net_device	*net;
	struct net_device_stats	stats;
	unsigned		flags;
	int			configuration_num;
	int			bConfigurationValue;
	int			comm_interface;
	int			comm_bInterfaceNumber;
	int			comm_interface_altset_num;
	int			comm_bAlternateSetting;
	int			comm_ep_in;
	int			data_interface;
	int			data_bInterfaceNumber;
	int			data_interface_altset_num_with_traffic;
	int			data_bAlternateSetting_with_traffic;
	int			data_interface_altset_num_without_traffic;
	int			data_bAlternateSetting_without_traffic;
	int			data_ep_in;
	int			data_ep_out;
	int			data_ep_out_size;
	__u16			bcdCDC;
	__u8			iMACAddress;
	__u32			bmEthernetStatistics;
	__u16			wMaxSegmentSize;
	__u16                   mode_flags;
	__u16			wNumberMCFilters;
	__u8			bNumberPowerFilters;
	int			intr_interval;
	struct urb		rx_urb, tx_urb, intr_urb;
	unsigned char		ALIGN(rx_buff[CDC_ETHER_MAX_MTU]);
	unsigned char		ALIGN(tx_buff[CDC_ETHER_MAX_MTU]);
	unsigned char		ALIGN(intr_buff[8]);
} ether_dev_t;

#define REQ_HDR_FUNC_DESCR	0x0001
#define REQ_UNION_FUNC_DESCR	0x0002
#define REQ_ETH_FUNC_DESCR	0x0004
#define REQUIREMENTS_TOTAL	0x0007



