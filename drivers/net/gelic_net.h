/*
 *  PS3 Platfom gelic network driver.
 *
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * Copyright 2007 Sony Corporation.
 *
 *  this file is based on: spider_net.h
 *
 * Network device driver for Cell Processor-Based Blade
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Authors : Utz Bacher <utz.bacher@de.ibm.com>
 *           Jens Osterkamp <Jens.Osterkamp@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef _GELIC_NET_H
#define _GELIC_NET_H

#include <linux/wireless.h>
#include <net/ieee80211.h>

#define GELIC_NET_DRV_NAME "Gelic Network Driver"
#define GELIC_NET_DRV_VERSION "1.0"

#define GELIC_NET_ETHTOOL               /* use ethtool */

/* ioctl */
#define GELIC_NET_GET_MODE              (SIOCDEVPRIVATE + 0)
#define GELIC_NET_SET_MODE              (SIOCDEVPRIVATE + 1)

/* descriptors */
#define GELIC_NET_RX_DESCRIPTORS        128 /* num of descriptors */
#define GELIC_NET_TX_DESCRIPTORS        128 /* num of descriptors */

#define GELIC_NET_MAX_MTU               2308
#define GELIC_NET_MIN_MTU               64
#define GELIC_NET_RXBUF_ALIGN           128
#define GELIC_NET_RX_CSUM_DEFAULT       1 /* hw chksum */
#define GELIC_NET_WATCHDOG_TIMEOUT      5*HZ
#define GELIC_NET_NAPI_WEIGHT           (GELIC_NET_RX_DESCRIPTORS)
#define GELIC_NET_BROADCAST_ADDR        0xffffffffffffL
#define GELIC_NET_VLAN_POS              (VLAN_ETH_ALEN * 2)
#define GELIC_NET_VLAN_MAX              4
#define GELIC_NET_MC_COUNT_MAX          32 /* multicast address list */

enum gelic_net_int0_status {
	GELIC_NET_GDTDCEINT  = 24,
	GELIC_NET_GRFANMINT  = 28,
};

/* GHIINT1STS bits */
enum gelic_net_int1_status {
	GELIC_NET_GDADCEINT = 14,
};

/* interrupt mask */
#define GELIC_NET_TXINT                   (1L << (GELIC_NET_GDTDCEINT + 32))

#define GELIC_NET_RXINT0                  (1L << (GELIC_NET_GRFANMINT + 32))
#define GELIC_NET_RXINT1                  (1L << GELIC_NET_GDADCEINT)
#define GELIC_NET_RXINT                   (GELIC_NET_RXINT0 | GELIC_NET_RXINT1)

 /* RX descriptor data_status bits */
#define GELIC_NET_RXDMADU	0x80000000 /* destination MAC addr unknown */
#define GELIC_NET_RXLSTFBF	0x40000000 /* last frame buffer            */
#define GELIC_NET_RXIPCHK	0x20000000 /* IP checksum performed        */
#define GELIC_NET_RXTCPCHK	0x10000000 /* TCP/UDP checksup performed   */
#define GELIC_NET_RXIPSPKT	0x08000000 /* IPsec packet   */
#define GELIC_NET_RXIPSAHPRT	0x04000000 /* IPsec AH protocol performed */
#define GELIC_NET_RXIPSESPPRT	0x02000000 /* IPsec ESP protocol performed */
#define GELIC_NET_RXSESPAH	0x01000000 /*
					    * IPsec ESP protocol auth
					    * performed
					    */

#define GELIC_NET_RXWTPKT	0x00C00000 /*
					    * wakeup trigger packet
					    * 01: Magic Packet (TM)
					    * 10: ARP packet
					    * 11: Multicast MAC addr
					    */
#define GELIC_NET_RXVLNPKT	0x00200000 /* VLAN packet */
/* bit 20..16 reserved */
#define GELIC_NET_RXRECNUM	0x0000ff00 /* reception receipt number */
/* bit 7..0 reserved */

#define GELIC_NET_TXDESC_TAIL		0
#define GELIC_NET_DATA_STATUS_CHK_MASK	(GELIC_NET_RXIPCHK | GELIC_NET_RXTCPCHK)

/* RX descriptor data_error bits */
/* bit 31 reserved */
#define GELIC_NET_RXALNERR	0x40000000 /* alignement error 10/100M */
#define GELIC_NET_RXOVERERR	0x20000000 /* oversize error */
#define GELIC_NET_RXRNTERR	0x10000000 /* Runt error */
#define GELIC_NET_RXIPCHKERR	0x08000000 /* IP checksum  error */
#define GELIC_NET_RXTCPCHKERR	0x04000000 /* TCP/UDP checksum  error */
#define GELIC_NET_RXUMCHSP	0x02000000 /* unmatched sp on sp */
#define GELIC_NET_RXUMCHSPI	0x01000000 /* unmatched SPI on SAD */
#define GELIC_NET_RXUMCHSAD	0x00800000 /* unmatched SAD */
#define GELIC_NET_RXIPSAHERR	0x00400000 /* auth error on AH protocol
					    * processing */
#define GELIC_NET_RXIPSESPAHERR	0x00200000 /* auth error on ESP protocol
					    * processing */
#define GELIC_NET_RXDRPPKT	0x00100000 /* drop packet */
#define GELIC_NET_RXIPFMTERR	0x00080000 /* IP packet format error */
/* bit 18 reserved */
#define GELIC_NET_RXDATAERR	0x00020000 /* IP packet format error */
#define GELIC_NET_RXCALERR	0x00010000 /* cariier extension length
					    * error */
#define GELIC_NET_RXCREXERR	0x00008000 /* carrier extention error */
#define GELIC_NET_RXMLTCST	0x00004000 /* multicast address frame */
/* bit 13..0 reserved */
#define GELIC_NET_DATA_ERROR_CHK_MASK		\
	(GELIC_NET_RXIPCHKERR | GELIC_NET_RXTCPCHKERR)


/* tx descriptor command and status */
#define GELIC_NET_DMAC_CMDSTAT_NOCS       0xa0080000 /* middle of frame */
#define GELIC_NET_DMAC_CMDSTAT_TCPCS      0xa00a0000
#define GELIC_NET_DMAC_CMDSTAT_UDPCS      0xa00b0000
#define GELIC_NET_DMAC_CMDSTAT_END_FRAME  0x00040000 /* end of frame */

#define GELIC_NET_DMAC_CMDSTAT_RXDCEIS	  0x00000002 /* descriptor chain end
						      * interrupt status */

#define GELIC_NET_DMAC_CMDSTAT_CHAIN_END  0x00000002 /* RXDCEIS:DMA stopped */
#define GELIC_NET_DMAC_CMDSTAT_NOT_IN_USE 0xb0000000
#define GELIC_NET_DESCR_IND_PROC_SHIFT    28
#define GELIC_NET_DESCR_IND_PROC_MASKO    0x0fffffff


enum gelic_net_descr_status {
	GELIC_NET_DESCR_COMPLETE            = 0x00, /* used in rx and tx */
	GELIC_NET_DESCR_RESPONSE_ERROR      = 0x01, /* used in rx and tx */
	GELIC_NET_DESCR_PROTECTION_ERROR    = 0x02, /* used in rx and tx */
	GELIC_NET_DESCR_FRAME_END           = 0x04, /* used in rx */
	GELIC_NET_DESCR_FORCE_END           = 0x05, /* used in rx and tx */
	GELIC_NET_DESCR_CARDOWNED           = 0x0a, /* used in rx and tx */
	GELIC_NET_DESCR_NOT_IN_USE                  /* any other value */
};
/* for lv1_net_control */
#define GELIC_NET_GET_MAC_ADDRESS               0x0000000000000001
#define GELIC_NET_GET_ETH_PORT_STATUS           0x0000000000000002
#define GELIC_NET_SET_NEGOTIATION_MODE          0x0000000000000003
#define GELIC_NET_GET_VLAN_ID                   0x0000000000000004

#define GELIC_NET_LINK_UP                       0x0000000000000001
#define GELIC_NET_FULL_DUPLEX                   0x0000000000000002
#define GELIC_NET_AUTO_NEG                      0x0000000000000004
#define GELIC_NET_SPEED_10                      0x0000000000000010
#define GELIC_NET_SPEED_100                     0x0000000000000020
#define GELIC_NET_SPEED_1000                    0x0000000000000040

#define GELIC_NET_VLAN_ALL                      0x0000000000000001
#define GELIC_NET_VLAN_WIRED                    0x0000000000000002
#define GELIC_NET_VLAN_WIRELESS                 0x0000000000000003
#define GELIC_NET_VLAN_PSP                      0x0000000000000004
#define GELIC_NET_VLAN_PORT0                    0x0000000000000010
#define GELIC_NET_VLAN_PORT1                    0x0000000000000011
#define GELIC_NET_VLAN_PORT2                    0x0000000000000012
#define GELIC_NET_VLAN_DAEMON_CLIENT_BSS        0x0000000000000013
#define GELIC_NET_VLAN_LIBERO_CLIENT_BSS        0x0000000000000014
#define GELIC_NET_VLAN_NO_ENTRY                 -6

#define GELIC_NET_PORT                          2 /* for port status */

/* wireless */
#define GELICW_WIRELESS_NOT_EXIST	0
#define GELICW_WIRELESS_SUPPORTED	1
#define GELICW_WIRELESS_ON		2
#define GELICW_WIRELESS_SHUTDOWN	3
/* state */
#define GELICW_STATE_DOWN		0
#define GELICW_STATE_UP			1
#define GELICW_STATE_SCANNING		2
#define GELICW_STATE_SCAN_DONE		3
#define GELICW_STATE_ASSOCIATED		4

/* cmd_send_flg */
#define GELICW_CMD_SEND_NONE		0x00
#define GELICW_CMD_SEND_COMMON		0x01
#define GELICW_CMD_SEND_ENCODE		0x02
#define GELICW_CMD_SEND_SCAN		0x04
#define GELICW_CMD_SEND_ALL		(GELICW_CMD_SEND_COMMON \
					| GELICW_CMD_SEND_ENCODE \
					| GELICW_CMD_SEND_SCAN)

#define GELICW_SCAN_INTERVAL		(HZ)

#ifdef DEBUG
#define CH_INFO_FAIL 0x0600 /* debug */
#else
#define CH_INFO_FAIL 0
#endif


/* net_control command */
#define GELICW_SET_PORT			3 /* control Ether port */
#define GELICW_GET_INFO			6 /* get supported channels */
#define GELICW_SET_CMD			9 /* set configuration */
#define GELICW_GET_RES			10 /* get command response */
#define GELICW_GET_EVENT		11 /* get event from device */
/* net_control command data buffer */
#define GELICW_DATA_BUF_SIZE		0x1000

/* GELICW_SET_CMD params */
#define GELICW_CMD_START		1
#define GELICW_CMD_STOP			2
#define GELICW_CMD_SCAN			3
#define GELICW_CMD_GET_SCAN		4
#define GELICW_CMD_SET_CONFIG		5
#define GELICW_CMD_GET_CONFIG		6
#define GELICW_CMD_SET_WEP		7
#define GELICW_CMD_GET_WEP		8
#define GELICW_CMD_SET_WPA		9
#define GELICW_CMD_GET_WPA		10
#define GELICW_CMD_GET_RSSI		11

/* GELICW_SET_PORT params */
#define GELICW_ETHER_PORT		2
#define GELICW_PORT_DOWN		0 /* Ether port off */
#define GELICW_PORT_UP			4 /* Ether port on (auto neg) */

/* interrupt status bit */
#define GELICW_DEVICE_CMD_COMP		(1UL << 31)
#define GELICW_DEVICE_EVENT_RECV	(1UL << 30)

/* GELICW_GET_EVENT ID */
#define GELICW_EVENT_UNKNOWN		0x00
#define GELICW_EVENT_DEVICE_READY	0x01
#define GELICW_EVENT_SCAN_COMPLETED	0x02
#define GELICW_EVENT_DEAUTH		0x04
#define GELICW_EVENT_BEACON_LOST	0x08
#define GELICW_EVENT_CONNECTED		0x10
#define GELICW_EVENT_WPA_CONNECTED	0x20
#define GELICW_EVENT_WPA_ERROR		0x40
#define GELICW_EVENT_NO_ENTRY		(-6)

#define MAX_IW_PRIV_SIZE		32

/* structure of data buffer for lv1_net_contol */
/* wep_config: sec */
#define GELICW_WEP_SEC_NONE		0
#define GELICW_WEP_SEC_40BIT		1
#define GELICW_WEP_SEC_104BIT		2
struct wep_config {
	u16 sec;
	u8  key[4][16];
} __attribute__ ((packed));

/* wpa_config: sec */
#define GELICW_WPA_SEC_NONE		0
#define GELICW_WPA_SEC_TKIP		1
#define GELICW_WPA_SEC_AES		2
/* wpa_config: psk_type */
#define GELICW_PSK_PASSPHRASE		0
#define GELICW_PSK_64HEX		1
struct wpa_config {
	u16 sec;
	u16 psk_type;
	u8  psk_material[64]; /* key */
} __attribute__ ((packed));

/* common_config: bss_type */
#define GELICW_BSS_INFRA		0
#define GELICW_BSS_ADHOC		1
/* common_config: auth_method */
#define GELICW_AUTH_OPEN		0
#define GELICW_AUTH_SHARED		1
/* common_config: op_mode */
#define GELICW_OP_MODE_11BG		0
#define GELICW_OP_MODE_11B		1
#define GELICW_OP_MODE_11G		2
struct common_config {
	u16 scan_index; /* index of scan_desc list */
	u16 bss_type;
	u16 auth_method;
	u16 op_mode;
} __attribute__ ((packed));

/* scan_descriptor: security */
#define GELICW_SEC_TYPE_NONE		0x0000
#define GELICW_SEC_TYPE_WEP		0x0100
#define GELICW_SEC_TYPE_WEP40		0x0101
#define GELICW_SEC_TYPE_WEP104		0x0102
#define GELICW_SEC_TYPE_TKIP		0x0201
#define GELICW_SEC_TYPE_AES		0x0202
#define GELICW_SEC_TYPE_WEP_MASK	0xFF00
struct scan_desc {
	u16 size;
	u16 rssi;
	u16 channel;
	u16 beacon_period;
	u16 capability;
	u16 security;
	u64 bssid;
	u8  essid[32];
	u8  rate[16];
	u8  ext_rate[16];
	u32 reserved1;
	u32 reserved2;
	u32 reserved3;
	u32 reserved4;
} __attribute__ ((packed));

/* rssi_descriptor */
struct rssi_desc {
	u16 rssi; /* max rssi = 100 */
} __attribute__ ((packed));


struct gelicw_bss {
	u8 bssid[ETH_ALEN];
	u8 channel;
	u8 mode;
	u8 essid_len;
	u8 essid[IW_ESSID_MAX_SIZE + 1]; /* null terminated for debug msg */

	u16 capability;
	u16 beacon_interval;

	u8 rates_len;
	u8 rates[MAX_RATES_LENGTH];
	u8 rates_ex_len;
	u8 rates_ex[MAX_RATES_EX_LENGTH];
	u8 rssi;

	/* scan results have sec_info instead of rsn_ie or wpa_ie */
	u16 sec_info;
};

/* max station count of station list which hvc returns */
#define MAX_SCAN_BSS	(16)

struct gelic_wireless {
	struct gelic_net_card *card;
	struct completion cmd_done, rssi_done;
	struct work_struct work_event, work_start_done;
	struct delayed_work work_rssi, work_scan_all, work_scan_essid;
	struct delayed_work work_common, work_encode;
	struct delayed_work work_start, work_stop, work_roam;
	wait_queue_head_t waitq_cmd, waitq_scan;

	u64 cmd_tag, cmd_id;
	u8 cmd_send_flg;

	struct iw_public_data wireless_data;
	void *data_buf; /* data buffer for lv1_net_control */

	u8 wireless; /* wireless support */
	u8 state;
	u8 scan_all; /* essid scan or all scan */
	u8 essid_search; /* essid background scan */
	u8 is_assoc;

	u16 ch_info; /* supoprted channels */
	u8 wireless_mode; /* 11b/g */
	u8 channel; /* current ch */
	u8 iw_mode; /* INFRA or Ad-hoc */
	u8 rssi;
	u8 essid_len;
	u8 essid[IW_ESSID_MAX_SIZE + 1]; /* null terminated for debug msg */
	u8 nick[IW_ESSID_MAX_SIZE + 1];
	u8 bssid[ETH_ALEN];

	u8 key_index;
	u8 key[WEP_KEYS][IW_ENCODING_TOKEN_MAX]; /* 4 * 64byte */
	u8 key_len[WEP_KEYS];
	u8 key_alg; /* key algorithm  */
	u8 auth_mode; /* authenticaton mode */

	u8 bss_index; /* current bss in bss_list */
	u8 num_bss_list;
	u8 bss_key_alg; /* key alg of bss */
	u8 wap_bssid[ETH_ALEN];
	unsigned long last_scan; /* last scan time */
	struct gelicw_bss current_bss;
	struct gelicw_bss bss_list[MAX_SCAN_BSS];
};

/* size of hardware part of gelic descriptor */
#define GELIC_NET_DESCR_SIZE	(32)
struct gelic_net_descr {
	/* as defined by the hardware */
	u32 buf_addr;
	u32 buf_size;
	u32 next_descr_addr;
	u32 dmac_cmd_status;
	u32 result_size;
	u32 valid_size;	/* all zeroes for tx */
	u32 data_status;
	u32 data_error;	/* all zeroes for tx */

	/* used in the driver */
	struct sk_buff *skb;
	dma_addr_t bus_addr;
	struct gelic_net_descr *next;
	struct gelic_net_descr *prev;
	struct vlan_ethhdr vlan;
} __attribute__((aligned(32)));

struct gelic_net_descr_chain {
	/* we walk from tail to head */
	struct gelic_net_descr *head;
	struct gelic_net_descr *tail;
};

struct gelic_net_card {
	struct net_device *netdev;
	/*
	 * hypervisor requires irq_status should be
	 * 8 bytes aligned, but u64 member is
	 * always disposed in that manner
	 */
	u64 irq_status;
	u64 ghiintmask;

	struct ps3_system_bus_device *dev;
	u32 vlan_id[GELIC_NET_VLAN_MAX];
	int vlan_index;

	struct gelic_net_descr_chain tx_chain;
	struct gelic_net_descr_chain rx_chain;
	/* gurad dmac descriptor chain*/
	spinlock_t chain_lock;

	struct net_device_stats netdev_stats;
	int rx_csum;
	/* guard tx_dma_progress */
	spinlock_t tx_dma_lock;
	int tx_dma_progress;

	struct work_struct tx_timeout_task;
	atomic_t tx_timeout_task_counter;
	wait_queue_head_t waitq;

	struct gelic_net_descr *tx_top, *rx_top;
#ifdef CONFIG_GELIC_WIRELESS
	struct gelic_wireless w;
#endif
	struct gelic_net_descr descr[0];
};


extern unsigned long p_to_lp(long pa);
extern int gelicw_setup_netdev(struct net_device *netdev, int wi);
extern void gelicw_up(struct net_device *netdev);
extern int gelicw_down(struct net_device *netdev);
extern void gelicw_remove(struct net_device *netdev);
extern void gelicw_interrupt(struct net_device *netdev, u64 status);
extern int gelicw_is_associated(struct net_device *netdev);

#endif /* _GELIC_NET_H */
