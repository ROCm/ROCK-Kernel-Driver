/* orinoco.h
 * 
 * Common definitions to all pieces of the various orinoco
 * drivers
 */

#ifndef _ORINOCO_H
#define _ORINOCO_H

/* To enable debug messages */
//#define ORINOCO_DEBUG		3

#if (! defined (WIRELESS_EXT)) || (WIRELESS_EXT < 10)
#error "orinoco_cs requires Wireless extensions v10 or later."
#endif /* (! defined (WIRELESS_EXT)) || (WIRELESS_EXT < 10) */
#define WIRELESS_SPY		// enable iwspy support


#define DLDWD_MIN_MTU		256
#define DLDWD_MAX_MTU		(HERMES_FRAME_LEN_MAX - ENCAPS_OVERHEAD)

#define LTV_BUF_SIZE		128
#define USER_BAP		0
#define IRQ_BAP			1
#define DLDWD_MACPORT		0
#define IRQ_LOOP_MAX		10
#define TX_NICBUF_SIZE		2048
#define TX_NICBUF_SIZE_BUG	1585		/* Bug in Intel firmware */
#define MAX_KEYS		4
#define MAX_KEY_SIZE		14
#define LARGE_KEY_SIZE		13
#define SMALL_KEY_SIZE		5
#define MAX_FRAME_SIZE		2304

typedef struct dldwd_key {
	uint16_t len;
	char data[MAX_KEY_SIZE];
} __attribute__ ((packed)) dldwd_key_t;

typedef dldwd_key_t dldwd_keys_t[MAX_KEYS];

/*====================================================================*/


typedef struct dldwd_priv {
	void* card;	/* Pointer to card dependant structure */
	/* card dependant extra reset code (i.e. bus/interface specific */
	int (*card_reset_handler)(struct dldwd_priv *);

	spinlock_t lock;
	long state;
#define DLDWD_STATE_INIRQ 0
#define DLDWD_STATE_DOIRQ 1
	int hw_ready;	/* HW may be suspended by platform */

	/* Net device stuff */
	struct net_device ndev;
	struct net_device_stats stats;
	struct iw_statistics wstats;


	/* Hardware control variables */
	hermes_t hw;
	uint16_t txfid;

	/* Capabilities of the hardware/firmware */
	int firmware_type;
#define FIRMWARE_TYPE_LUCENT 1
#define FIRMWARE_TYPE_INTERSIL 2
#define FIRMWARE_TYPE_SYMBOL 3
	int has_ibss, has_port3, prefer_port3, has_ibss_any, ibss_port;
	int has_wep, has_big_wep;
	int has_mwo;
	int has_pm;
	int has_preamble;
	int need_card_reset, broken_reset, broken_allocate;
	uint16_t channel_mask;

	/* Current configuration */
	uint32_t iw_mode;
	int port_type, allow_ibss;
	uint16_t wep_on, wep_restrict, tx_key;
	dldwd_keys_t keys;
 	char nick[IW_ESSID_MAX_SIZE+1];
	char desired_essid[IW_ESSID_MAX_SIZE+1];
	uint16_t frag_thresh, mwo_robust;
	uint16_t channel;
	uint16_t ap_density, rts_thresh;
	uint16_t tx_rate_ctrl;
	uint16_t pm_on, pm_mcast, pm_period, pm_timeout;
	uint16_t preamble;

	int promiscuous, allmulti, mc_count;

#ifdef WIRELESS_SPY
	int			spy_number;
	u_char			spy_address[IW_MAX_SPY][ETH_ALEN];
	struct iw_quality	spy_stat[IW_MAX_SPY];
#endif

	/* /proc based debugging stuff */
	struct proc_dir_entry *dir_dev;
	struct proc_dir_entry *dir_regs;
	struct proc_dir_entry *dir_recs;
} dldwd_priv_t;

/*====================================================================*/

extern struct list_head dldwd_instances;

#ifdef ORINOCO_DEBUG
extern int dldwd_debug;
#define DEBUG(n, args...) if (dldwd_debug>(n)) printk(KERN_DEBUG args)
#define DEBUGMORE(n, args...) do { if (dldwd_debug>(n)) printk(args); } while (0)
#else
#define DEBUG(n, args...) do { } while (0)
#define DEBUGMORE(n, args...) do { } while (0)
#endif	/* ORINOCO_DEBUG */

#define TRACE_ENTER(devname) DEBUG(2, "%s: -> " __FUNCTION__ "()\n", devname);
#define TRACE_EXIT(devname)  DEBUG(2, "%s: <- " __FUNCTION__ "()\n", devname);

#define MAX(a, b) ( (a) > (b) ? (a) : (b) )
#define MIN(a, b) ( (a) < (b) ? (a) : (b) )

#define RUP_EVEN(a) ( (a) % 2 ? (a) + 1 : (a) )

/* struct net_device methods */
extern int dldwd_init(struct net_device *dev);
extern int dldwd_xmit(struct sk_buff *skb, struct net_device *dev);
extern void dldwd_tx_timeout(struct net_device *dev);

extern int dldwd_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
extern int dldwd_change_mtu(struct net_device *dev, int new_mtu);
extern void dldwd_set_multicast_list(struct net_device *dev);

/* utility routines */
extern void dldwd_shutdown(dldwd_priv_t *dev);
extern int dldwd_reset(dldwd_priv_t *dev);
extern int dldwd_setup(dldwd_priv_t* priv);
extern int dldwd_proc_dev_init(dldwd_priv_t *dev);
extern void dldwd_proc_dev_cleanup(dldwd_priv_t *priv);
extern void dldwd_interrupt(int irq, void * dev_id, struct pt_regs *regs);

#endif
