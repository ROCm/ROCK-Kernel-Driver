/* orinoco.h
 * 
 * Common definitions to all pieces of the various orinoco
 * drivers
 */

#ifndef _ORINOCO_H
#define _ORINOCO_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/version.h>
#include "hermes.h"

/* Workqueue / task queue backwards compatibility stuff */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,41)
#include <linux/workqueue.h>
#else
#include <linux/tqueue.h>
#define work_struct tq_struct
#define INIT_WORK INIT_TQUEUE
#define schedule_work schedule_task
#endif

/* Interrupt handler backwards compatibility stuff */
#ifndef IRQ_NONE

#define IRQ_NONE
#define IRQ_HANDLED
typedef void irqreturn_t;

#endif

/* To enable debug messages */
//#define ORINOCO_DEBUG		3

#ifndef ETH_P_ECONET
#define ETH_P_ECONET   0x0018    /* needed for 2.2.x kernels */
#endif

#define ETH_P_80211_RAW        (ETH_P_ECONET + 1)

#ifndef ARPHRD_IEEE80211
#define ARPHRD_IEEE80211 801     /* kernel 2.4.6 */
#endif

#ifndef ARPHRD_IEEE80211_PRISM  /* kernel 2.4.18 */
#define ARPHRD_IEEE80211_PRISM 802
#endif

#if (! defined (WIRELESS_EXT)) || (WIRELESS_EXT < 10)
#error "orinoco driver requires Wireless extensions v10 or later."
#endif /* (! defined (WIRELESS_EXT)) || (WIRELESS_EXT < 10) */
#define WIRELESS_SPY		// enable iwspy support

#define ORINOCO_MAX_KEY_SIZE	14
#define ORINOCO_MAX_KEYS	4

struct orinoco_key {
	u16 len;	/* always stored as little-endian */
	char data[ORINOCO_MAX_KEY_SIZE];
} __attribute__ ((packed));

#define ORINOCO_INTEN	 	( HERMES_EV_RX | HERMES_EV_ALLOC | HERMES_EV_TX | \
				HERMES_EV_TXEXC | HERMES_EV_WTERR | HERMES_EV_INFO | \
				HERMES_EV_INFDROP )

#define WLAN_DEVNAMELEN_MAX 16

/* message data item for INT, BOUNDEDINT, ENUMINT */
typedef struct p80211item_uint32
{
	uint32_t		did		__attribute__ ((packed));
	uint16_t		status	__attribute__ ((packed));
	uint16_t		len		__attribute__ ((packed));
	uint32_t		data	__attribute__ ((packed));
} __attribute__ ((packed)) p80211item_uint32_t;

typedef struct p80211msg
{
	uint32_t	msgcode		__attribute__ ((packed));
	uint32_t	msglen		__attribute__ ((packed));
	uint8_t	devname[WLAN_DEVNAMELEN_MAX]	__attribute__ ((packed));
} __attribute__ ((packed)) p80211msg_t;

#define DIDmsg_lnxind_wlansniffrm 0x0041
#define DIDmsg_lnxind_wlansniffrm_hosttime 0x1041
#define DIDmsg_lnxind_wlansniffrm_mactime 0x2041
#define DIDmsg_lnxind_wlansniffrm_channel 0x3041
#define DIDmsg_lnxind_wlansniffrm_rssi 0x4041
#define DIDmsg_lnxind_wlansniffrm_sq 0x5041
#define DIDmsg_lnxind_wlansniffrm_signal 0x6041
#define DIDmsg_lnxind_wlansniffrm_noise 0x7041
#define DIDmsg_lnxind_wlansniffrm_rate 0x8041
#define DIDmsg_lnxind_wlansniffrm_istx 0x9041
#define DIDmsg_lnxind_wlansniffrm_frmlen 0xA041

typedef struct p80211msg_lnxind_wlansniffrm
{
	uint32_t		msgcode;
	uint32_t		msglen;
	uint8_t		    devname[WLAN_DEVNAMELEN_MAX];
	p80211item_uint32_t	hosttime;
	p80211item_uint32_t	mactime;
	p80211item_uint32_t	channel;
	p80211item_uint32_t	rssi;
	p80211item_uint32_t	sq;
	p80211item_uint32_t	signal;
	p80211item_uint32_t	noise;
	p80211item_uint32_t	rate;
	p80211item_uint32_t	istx;
	p80211item_uint32_t	frmlen;
} __attribute__ ((packed)) p80211msg_lnxind_wlansniffrm_t;

#define P80211ENUM_truth_false			0
#define P80211ENUM_truth_true			1
#define P80211ENUM_resultcode_success		1
#define P80211ENUM_resultcode_invalid_parameters	2
#define P80211ENUM_resultcode_not_supported	3
#define P80211ENUM_resultcode_timeout		4
#define P80211ENUM_resultcode_too_many_req	5
#define P80211ENUM_resultcode_refused		6
#define P80211ENUM_resultcode_bss_already	7
#define P80211ENUM_resultcode_invalid_access	8
#define P80211ENUM_resultcode_invalid_mibattribute	9
#define P80211ENUM_resultcode_cant_set_readonly_mib	10
#define P80211ENUM_resultcode_implementation_failure	11
#define P80211ENUM_resultcode_cant_get_writeonly_mib	12
#define P80211ENUM_msgitem_status_data_ok		0
#define P80211ENUM_msgitem_status_no_value		1
#define P80211ENUM_msgitem_status_invalid_itemname	2
#define P80211ENUM_msgitem_status_invalid_itemdata	3
#define P80211ENUM_msgitem_status_missing_itemdata	4
#define P80211ENUM_msgitem_status_incomplete_itemdata	5
#define P80211ENUM_msgitem_status_invalid_msg_did	6
#define P80211ENUM_msgitem_status_invalid_mib_did	7
#define P80211ENUM_msgitem_status_missing_conv_func	8
#define P80211ENUM_msgitem_status_string_too_long	9
#define P80211ENUM_msgitem_status_data_out_of_range	10
#define P80211ENUM_msgitem_status_string_too_short	11
#define P80211ENUM_msgitem_status_missing_valid_func	12
#define P80211ENUM_msgitem_status_unknown		13
#define P80211ENUM_msgitem_status_invalid_did		14
#define P80211ENUM_msgitem_status_missing_print_func	15

#define WLAN_GET_FC_FTYPE(n)	(((n) & 0x0C) >> 2)
#define WLAN_GET_FC_FSTYPE(n)	(((n) & 0xF0) >> 4)
#define WLAN_GET_FC_TODS(n) 	(((n) & 0x0100) >> 8)
#define WLAN_GET_FC_FROMDS(n)	(((n) & 0x0200) >> 9)

/*--- Sizes -----------------------------------------------*/
#define WLAN_ADDR_LEN			6
#define WLAN_CRC_LEN			4
#define WLAN_BSSID_LEN			6
#define WLAN_BSS_TS_LEN			8
#define WLAN_HDR_A3_LEN			24
#define WLAN_HDR_A4_LEN			30
#define WLAN_SSID_MAXLEN		32
#define WLAN_DATA_MAXLEN		2312

/*--- Frame Control Field -------------------------------------*/
/* Frame Types */
#define WLAN_FTYPE_MGMT			0x00
#define WLAN_FTYPE_CTL			0x01
#define WLAN_FTYPE_DATA			0x02

/* Frame subtypes */
/* Management */
#define WLAN_FSTYPE_ASSOCREQ		0x00
#define WLAN_FSTYPE_ASSOCRESP		0x01
#define WLAN_FSTYPE_REASSOCREQ		0x02
#define WLAN_FSTYPE_REASSOCRESP		0x03
#define WLAN_FSTYPE_PROBEREQ		0x04 
#define WLAN_FSTYPE_PROBERESP		0x05
#define WLAN_FSTYPE_BEACON		0x08
#define WLAN_FSTYPE_ATIM		0x09
#define WLAN_FSTYPE_DISASSOC		0x0a
#define WLAN_FSTYPE_AUTHEN		0x0b
#define WLAN_FSTYPE_DEAUTHEN		0x0c

/* Control */
#define WLAN_FSTYPE_PSPOLL		0x0a
#define WLAN_FSTYPE_RTS			0x0b
#define WLAN_FSTYPE_CTS			0x0c
#define WLAN_FSTYPE_ACK			0x0d
#define WLAN_FSTYPE_CFEND		0x0e
#define WLAN_FSTYPE_CFENDCFACK		0x0f

/* Data */
#define WLAN_FSTYPE_DATAONLY		0x00
#define WLAN_FSTYPE_DATA_CFACK		0x01
#define WLAN_FSTYPE_DATA_CFPOLL		0x02
#define WLAN_FSTYPE_DATA_CFACK_CFPOLL	0x03
#define WLAN_FSTYPE_NULL		0x04
#define WLAN_FSTYPE_CFACK		0x05
#define WLAN_FSTYPE_CFPOLL		0x06
#define WLAN_FSTYPE_CFACK_CFPOLL	0x07

/*----------------------------------------------------------------*/
/* Magic number, a quick test to see we're getting the desired struct */

#define P80211_IOCTL_MAGIC	(0x4a2d464dUL)

/*================================================================*/
/* Types */

/*----------------------------------------------------------------*/
/* A ptr to the following structure type is passed as the third */
/*  argument to the ioctl system call when issuing a request to */
/*  the p80211 module. */

typedef struct p80211ioctl_req
{
	char 	name[WLAN_DEVNAMELEN_MAX] __attribute__ ((packed));
	void	*data 		__attribute__ ((packed));
	uint32_t	magic 	__attribute__ ((packed));
	uint16_t	len 	__attribute__ ((packed));
	uint32_t	result 	__attribute__ ((packed));
} __attribute__ ((packed)) p80211ioctl_req_t;

struct orinoco_private {
	void *card;	/* Pointer to card dependent structure */
	int (*hard_reset)(struct orinoco_private *);

	/* Synchronisation stuff */
	spinlock_t lock;
	int hw_unavailable;
	struct work_struct reset_work;

	/* driver state */
	int open;
	u16 last_linkstatus;
	int connected;

	/* Net device stuff */
	struct net_device *ndev;
	struct net_device_stats stats;
	struct iw_statistics wstats;

	/* Hardware control variables */
	hermes_t hw;
	u16 txfid;


	/* Capabilities of the hardware/firmware */
	int firmware_type;
#define FIRMWARE_TYPE_AGERE 1
#define FIRMWARE_TYPE_INTERSIL 2
#define FIRMWARE_TYPE_SYMBOL 3
	int has_ibss, has_port3, has_ibss_any, ibss_port;
	int has_wep, has_big_wep;
	int has_mwo;
	int has_pm;
	int has_preamble;
	int has_sensitivity;
	int nicbuf_size;
	u16 channel_mask;
	int broken_disableport;

	/* Configuration paramaters */
	u32 iw_mode;
	int prefer_port3;
	u16 wep_on, wep_restrict, tx_key;
	struct orinoco_key keys[ORINOCO_MAX_KEYS];
	int bitratemode;
 	char nick[IW_ESSID_MAX_SIZE+1];
	char desired_essid[IW_ESSID_MAX_SIZE+1];
	u16 frag_thresh, mwo_robust;
	u16 channel;
	u16 ap_density, rts_thresh;
	u16 pm_on, pm_mcast, pm_period, pm_timeout;
	u16 preamble;
#ifdef WIRELESS_SPY
	int			spy_number;
	u_char			spy_address[IW_MAX_SPY][ETH_ALEN];
	struct iw_quality	spy_stat[IW_MAX_SPY];
#endif

	/* Configuration dependent variables */
	int port_type, createibss;
	int promiscuous, mc_count;

	uint16_t		presniff_port_type;
	uint16_t		presniff_wepflags;
};

#ifdef ORINOCO_DEBUG
extern int orinoco_debug;
#define DEBUG(n, args...) do { if (orinoco_debug>(n)) printk(KERN_DEBUG args); } while(0)
#else
#define DEBUG(n, args...) do { } while (0)
#endif	/* ORINOCO_DEBUG */

#define TRACE_ENTER(devname) DEBUG(2, "%s: -> %s()\n", devname, __FUNCTION__);
#define TRACE_EXIT(devname)  DEBUG(2, "%s: <- %s()\n", devname, __FUNCTION__);

extern struct net_device *alloc_orinocodev(int sizeof_card,
					   int (*hard_reset)(struct orinoco_private *));
extern int __orinoco_up(struct net_device *dev);
extern int __orinoco_down(struct net_device *dev);
extern int orinoco_stop(struct net_device *dev);
extern int orinoco_reinit_firmware(struct net_device *dev);
extern irqreturn_t orinoco_interrupt(int irq, void * dev_id, struct pt_regs *regs);

/********************************************************************/
/* Locking and synchronization functions                            */
/********************************************************************/

/* These functions *must* be inline or they will break horribly on
 * SPARC, due to its weird semantics for save/restore flags. extern
 * inline should prevent the kernel from linking or module from
 * loading if they are not inlined. */
extern inline int orinoco_lock(struct orinoco_private *priv,
			       unsigned long *flags)
{
	spin_lock_irqsave(&priv->lock, *flags);
	if (priv->hw_unavailable) {
		printk(KERN_DEBUG "orinoco_lock() called with hw_unavailable (dev=%p)\n",
		       priv->ndev);
		spin_unlock_irqrestore(&priv->lock, *flags);
		return -EBUSY;
	}
	return 0;
}

extern inline void orinoco_unlock(struct orinoco_private *priv,
				  unsigned long *flags)
{
	spin_unlock_irqrestore(&priv->lock, *flags);
}

/*================================================================*/
/* Function Declarations */

struct ieee802_11_hdr;

void orinoco_int_rxmonitor( struct orinoco_private *dev, uint16_t rxfid, int len,
                            struct hermes_rx_descriptor *rxdesc, struct ieee802_11_hdr *hdr);

#endif /* _ORINOCO_H */
