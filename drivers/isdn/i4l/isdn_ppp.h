/* Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995,96 by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include "isdn_net_lib.h"

extern struct file_operations isdn_ppp_fops;
extern struct isdn_netif_ops isdn_ppp_ops;

int isdn_ppp_init(void);
void isdn_ppp_cleanup(void);
int isdn_ppp_dial_slave(char *);
int isdn_ppp_hangup_slave(char *);

struct inl_ppp {
	unsigned long debug;
	struct slcompress *slcomp;
	struct ippp_ccp *ccp;         /* CCP for this channel */
	unsigned int mp_cfg;
	struct sk_buff_head mp_frags; /* fragments list */
	u32 mp_rxseq;                 /* last processed packet seq # */
	u32 mp_txseq;                 /* current tx seq # */
};

struct ind_ppp {
	struct ipppd *ipppd;          /* /dev/ipppX which controls us */
	unsigned int pppcfg;
	unsigned long debug;
	struct ippp_ccp *ccp;         /* CCP for this channel (multilink) */
	u32 mp_rxseq;                 /* last seq no seen on this channel */
};

void
isdn_ppp_frame_log(char *info, char *data, int len, int maxlen, 
		   int unit, int slot);

int
isdn_ppp_strip_proto(struct sk_buff *skb, u16 *proto);

void
ippp_push_proto(struct ind_ppp *ind_ppp, struct sk_buff *skb, u16 proto);

void
ippp_xmit(isdn_net_dev *idev, struct sk_buff *skb);

void
ippp_receive(isdn_net_dev *idev, struct sk_buff *skb, u16 proto);

struct sk_buff *
isdn_ppp_dev_alloc_skb(void *priv, int len, int gfp_mask);

#define IPPP_MAX_HEADER 10
