#ifndef LLC_IF_H
#define LLC_IF_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
/* Defines LLC interface to network layer */
/* Available primitives */
#include <linux/if.h>

#define LLC_DATAUNIT_PRIM	0
#define LLC_CONN_PRIM		1
#define LLC_DATA_PRIM		2
#define LLC_DISC_PRIM		3
#define LLC_RESET_PRIM		4
#define LLC_FLOWCONTROL_PRIM	5
#define LLC_DISABLE_PRIM	6
#define LLC_XID_PRIM		7
#define LLC_TEST_PRIM		8
#define LLC_SAP_ACTIVATION      9
#define LLC_SAP_DEACTIVATION   10

#define LLC_NBR_PRIMITIVES     11

#define LLC_IND			1
#define LLC_CONFIRM		2

/* Primitive type */
#define LLC_PRIM_TYPE_REQ	1
#define LLC_PRIM_TYPE_IND	2
#define LLC_PRIM_TYPE_RESP	3
#define LLC_PRIM_TYPE_CONFIRM	4

/* Reset reasons, remote entity or local LLC */
#define LLC_RESET_REASON_REMOTE	1
#define LLC_RESET_REASON_LOCAL	2

/* Disconnect reasons */
#define LLC_DISC_REASON_RX_DM_RSP_PDU	0
#define LLC_DISC_REASON_RX_DISC_CMD_PDU	1
#define LLC_DISC_REASON_ACK_TMR_EXP	2

/* Confirm reasons */
#define LLC_STATUS_CONN		0 /* connect confirm & reset confirm */
#define LLC_STATUS_DISC		1 /* connect confirm & reset confirm */
#define LLC_STATUS_FAILED	2 /* connect confirm & reset confirm */
#define LLC_STATUS_IMPOSSIBLE	3 /* connect confirm */
#define LLC_STATUS_RECEIVED	4 /* data conn */
#define LLC_STATUS_REMOTE_BUSY	5 /* data conn */
#define LLC_STATUS_REFUSE	6 /* data conn */
#define LLC_STATUS_CONFLICT	7 /* disconnect conn */
#define LLC_STATUS_RESET_DONE	8 /*  */

/* Structures and types */
/* SAP/MAC Address pair */
struct llc_addr {
	u8 lsap;
	u8 mac[IFHWADDRLEN];
};

/* Primitive-specific data */
struct llc_prim_conn {
	struct llc_addr	   saddr;	/* used by request only */
	struct llc_addr	   daddr;	/* used by request only */
	u8		   status;	/* reason for failure */
	u8		   pri;		/* service_class */
	struct net_device *dev;
	struct sock	  *sk;		/* returned from REQUEST */
	void		  *handler;	/* upper layer use,
					   stored in llc_opt->handler */
	u16		   link;
	struct sk_buff	  *skb;		/* received SABME  */
};

struct llc_prim_disc {
	struct sock *sk;
	u16	     link;
	u8	     reason;		/* not used by request */
};

struct llc_prim_reset {
	struct sock *sk;
	u16	     link;
	u8	     reason;		/* used only by indicate */
};

struct llc_prim_flow_ctrl {
	struct sock *sk;
	u16	     link;
	u32	     amount;
};

struct llc_prim_data {
	struct sock    *sk;
	u16		link;
	u8		pri;
	struct sk_buff *skb;		/* pointer to frame */
	u8	 	status;		/* reason */
};

 /* Sending data in conection-less mode */
struct llc_prim_unit_data {
	struct llc_addr	saddr;
	struct llc_addr	daddr;
	u8		pri;
	struct sk_buff *skb;		/* pointer to frame */
	u8		lfb;		/* largest frame bit (TR) */
};

struct llc_prim_xid {
	struct llc_addr saddr;
	struct llc_addr daddr;
	u8		pri;
	struct sk_buff *skb;
};

struct llc_prim_test {
	struct llc_addr	saddr;
	struct llc_addr	daddr;
	u8		pri;
	struct sk_buff *skb;		/* pointer to frame */
};

union llc_u_prim_data {
	struct llc_prim_conn	  conn;
	struct llc_prim_disc	  disc;
	struct llc_prim_reset	  res;
	struct llc_prim_flow_ctrl fc;
	struct llc_prim_data	  data;		/* data */
	struct llc_prim_unit_data udata;	/* unit data */
	struct llc_prim_xid	  xid;
	struct llc_prim_test	  test;
};

struct llc_sap;

/* Information block passed with all called primitives */
struct llc_prim_if_block {
	struct llc_sap	      *sap;
	u8		       prim;
	union llc_u_prim_data *data;
};
typedef int (*llc_prim_call_t)(struct llc_prim_if_block *prim_if);

extern struct llc_sap *llc_sap_open(llc_prim_call_t network_indicate,
				    llc_prim_call_t network_confirm, u8 lsap);
extern void llc_sap_close(struct llc_sap *sap);
#endif /* LLC_IF_H */
