/* Linux ISDN subsystem, network interface support code
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef __ISDN_NET_LIB_H__
#define __ISDN_NET_LIB_H__

#include <linux/isdn.h>

typedef struct isdn_net_local_s isdn_net_local;
typedef struct isdn_net_dev_s isdn_net_dev;

struct isdn_netif_ops {
	int			(*hard_start_xmit) (struct sk_buff *skb,
						    struct net_device *dev);
	int			(*hard_header) (struct sk_buff *skb,
						struct net_device *dev,
						unsigned short type,
						void *daddr,
						void *saddr,
						unsigned len);
	int			(*do_ioctl)(struct net_device *dev,
					    struct ifreq *ifr, int cmd);

	unsigned short		flags;	/* interface flags (a la BSD)	*/
	unsigned short		type;	/* interface hardware type	*/
	unsigned char		addr_len;/* hardware address length	*/
	void                    (*receive)(struct isdn_net_local_s *,
					   struct isdn_net_dev_s *,
					   struct sk_buff *);
	void                    (*connected)(struct isdn_net_dev_s *);
	void                    (*disconnected)(struct isdn_net_dev_s *);
	int                     (*bind)(struct isdn_net_dev_s *);
	void                    (*unbind)(struct isdn_net_dev_s *);
	int                     (*init)(struct isdn_net_local_s *);
	void                    (*cleanup)(struct isdn_net_local_s *);
	int                     (*open)(struct isdn_net_local_s *);
	void                    (*close)(struct isdn_net_local_s *);
};

/* our interface to isdn_common.c */
void isdn_net_lib_init(void);
void isdn_net_lib_exit(void);
void isdn_net_hangup_all(void);
int  isdn_net_ioctl(struct inode *, struct file *, uint, ulong);
int  isdn_net_find_icall(struct isdn_slot *slot, setup_parm *setup);

/* provided for interface types to use */
void isdn_net_writebuf_skb(isdn_net_dev *, struct sk_buff *skb);
void isdn_net_write_super(isdn_net_dev *, struct sk_buff *skb);
void isdn_net_online(isdn_net_dev *idev);
void isdn_net_offline(isdn_net_dev *idev);
int  isdn_net_start_xmit(struct sk_buff *skb, struct net_device *ndev);
void isdn_netif_rx(isdn_net_dev *idev, struct sk_buff *skb, u16 protocol);
isdn_net_dev *isdn_net_get_xmit_dev(isdn_net_local *mlp);
int  isdn_net_hangup(isdn_net_dev *);
int  isdn_net_autodial(struct sk_buff *skb, struct net_device *ndev);
int  isdn_net_dial_req(isdn_net_dev *);
int  register_isdn_netif(int encap, struct isdn_netif_ops *ops);

/* ====================================================================== */

/* Feature- and status-flags for a net-interface */
#define ISDN_NET_SECURE     0x02       /* Accept calls from phonelist only  */
#define ISDN_NET_CALLBACK   0x04       /* activate callback                 */
#define ISDN_NET_CBHUP      0x08       /* hangup before callback            */
#define ISDN_NET_CBOUT      0x10       /* remote machine does callback      */

#define ISDN_NET_MAGIC      0x49344C02 /* for paranoia-checking             */

/* Phone-list-element */
struct isdn_net_phone {
	struct list_head list;
	char num[ISDN_MSNLEN];
};

/* per network interface data (dev->priv) */

struct isdn_net_local_s {
  ulong                  magic;
  struct net_device      dev;          /* interface to upper levels        */
  struct net_device_stats stats;       /* Ethernet Statistics              */
  struct isdn_netif_ops *ops;
  void                  *inl_priv;     /* interface types can put their
					  private data here                */
  int                    flags;        /* Connection-flags                 */
  int                    dialmax;      /* Max. Number of Dial-retries      */
  int	        	 dialtimeout;  /* How long shall we try on dialing */
  int			 dialwait;     /* wait after failed attempt        */

  int                    cbdelay;      /* Delay before Callback starts     */
  char                   msn[ISDN_MSNLEN]; /* MSNs/EAZs for this interface */

  u_char                 cbhup;        /* Flag: Reject Call before Callback*/
  int                    hupflags;     /* Flags for charge-unit-hangup:    */
  int                    onhtime;      /* Time to keep link up             */

  u_char                 p_encap;      /* Packet encapsulation             */
  u_char                 l2_proto;     /* Layer-2-protocol                 */
  u_char                 l3_proto;     /* Layer-3-protocol                 */

  ulong                  slavedelay;   /* Dynamic bundling delaytime       */
  int                    triggercps;   /* BogoCPS needed for trigger slave */
  struct list_head       phone[2];     /* List of remote-phonenumbers      */
				       /* phone[0] = Incoming Numbers      */
				       /* phone[1] = Outgoing Numbers      */

  struct list_head       slaves;       /* list of all bundled channels    
					  protected by serializing config
					  ioctls / no change allowed when
					  interface is running             */
  struct list_head       online;       /* list of all bundled channels 
					  which can be used for actual
					  data (IP) transfer              
					  protected by xmit_lock           */

  spinlock_t             xmit_lock;    /* used to protect the xmit path of 
					  a net_device, including all
					  associated channels's frame_cnt  */
  struct list_head       running_devs; /* member of global running_devs    */
  atomic_t               refcnt;       /* references held by ISDN code     */

};


/* per ISDN channel (ISDN interface) data */

struct isdn_net_dev_s {
  struct isdn_slot	*isdn_slot;	/* Index to isdn device/channel     */
  struct isdn_slot	*exclusive;	/* NULL if non excl                 */
  int			pre_device;	/* Preselected isdn-device          */
  int			pre_channel;	/* Preselected isdn-channel         */

  struct timer_list	dial_timer;	/* dial events timer                */
  struct fsm_inst	fi;		/* call control state machine       */
  int			dial_event;	/* event in case of timer expiry    */
  int			dial;		/* # of phone number just dialed    */
  int			outgoing;	/* Flag: outgoing call              */
  int			dialretry;	/* Counter for Dialout-retries      */

  int			cps;		/* current speed of this interface  */
  int			transcount;	/* byte-counter for cps-calculation */
  u_long		last_jiffies;	/* when transcount was reset        */
  int			sqfull;		/* Flag: netdev-queue overloaded    */
  u_long		sqfull_stamp;	/* Start-Time of overload           */

  int			huptimer;	/* Timeout-counter for auto-hangup  */
  int			charge;		/* Counter for charging units       */
  int			charge_state;	/* ChargeInfo state machine         */
  u_long		chargetime;	/* Timer for Charging info          */
  int			chargeint;	/* Interval between charge-infos    */

  int			pppbind;	/* ippp device for bindings         */

  struct sk_buff_head	super_tx_queue;	/* List of supervisory frames to  */
					/* be transmitted asap              */
  int			frame_cnt;	/* number of frames currently       */
					/* queued in HL driver              */
  struct tasklet_struct	tlet;

  isdn_net_local	*mlp;		/* Ptr to master device for all devs*/

  struct list_head	slaves;		/* member of local->slaves          */
  struct list_head	online;		/* member of local->online          */

  char			name[10];	/* Name of device                   */
  struct list_head	global_list;	/* global list of all isdn_net_devs */
  void			*ind_priv;	/* interface types can put their
					   private data here                */
};

/* ====================================================================== */

static inline int
put_u8(unsigned char *p, u8 x)
{
	*p = x;
	return 1;
}

static inline int
put_u16(unsigned char *p, u16 x)
{
	*((u16 *)p) = htons(x);
	return 2;
}

static inline int
put_u32(unsigned char *p, u32 x)
{
	*((u32 *)p) = htonl(x);
	return 4;
}

static inline int
get_u8(unsigned char *p, u8 *x)
{
	*x = *p;
	return 1;
}

static inline int
get_u16(unsigned char *p, u16 *x)
{
	*x = ntohs(*((u16 *)p));
	return 2;
}

static inline int
get_u32(unsigned char *p, u32 *x)
{
	*x = ntohl(*((u32 *)p));
	return 4;
}


#endif
