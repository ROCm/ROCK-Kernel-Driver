#ifndef LLC_CONN_H
#define LLC_CONN_H
/*
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/timer.h>
#include <net/llc_if.h>

#undef DEBUG_LLC_CONN_ALLOC

struct llc_timer {
	struct timer_list timer;
	u8		  running;	/* timer is running or no */
	u16		  expire;	/* timer expire time */
};

struct llc_opt {
	struct list_head    node;		/* entry in sap->sk_list.list */
	struct sock	    *sk;		/* sock that has this llc_opt */
	void		    *handler;		/* for upper layers usage */
	u8		    state;		/* state of connection */
	struct llc_sap	    *sap;		/* pointer to parent SAP */
	struct llc_addr	    laddr;		/* lsap/mac pair */
	struct llc_addr	    daddr;		/* dsap/mac pair */
	struct net_device   *dev;		/* device to send to remote */
	u8		    retry_count;	/* number of retries */
	u8		    ack_must_be_send;
	u8		    first_pdu_Ns;
	u8		    npta;
	struct llc_timer    ack_timer;
	struct llc_timer    pf_cycle_timer;
	struct llc_timer    rej_sent_timer;
	struct llc_timer    busy_state_timer;	/* ind busy clr at remote LLC */
	u8		    vS;			/* seq# next in-seq I-PDU tx'd*/
	u8		    vR;			/* seq# next in-seq I-PDU rx'd*/
	u32		    n2;			/* max nbr re-tx's for timeout*/
	u32		    n1;			/* max nbr octets in I PDU */
	u8		    k;			/* tx window size; max = 127 */
	u8		    rw;			/* rx window size; max = 127 */
	u8		    p_flag;		/* state flags */
	u8		    f_flag;
	u8		    s_flag;
	u8		    data_flag;
	u8		    remote_busy_flag;
	u8		    cause_flag;
	struct sk_buff_head pdu_unack_q;	/* PUDs sent/waiting ack */
	u16		    link;		/* network layer link number */
	u8		    X;			/* a temporary variable */
	u8		    ack_pf;		/* this flag indicates what is
						   the P-bit of acknowledge */
	u8		    failed_data_req; /* recognize that already exist a
						failed llc_data_req_handler
						(tx_buffer_full or unacceptable
						state */
	u8		    dec_step;
	u8		    inc_cntr;
	u8		    dec_cntr;
	u8		    connect_step;
	u8		    last_nr;	   /* NR of last pdu recieved */
	u32		    rx_pdu_hdr;	   /* used for saving header of last pdu
					      received and caused sending FRMR.
					      Used for resending FRMR */
#ifdef DEBUG_LLC_CONN_ALLOC
	char *f_alloc,	/* function that allocated this connection */
	     *f_free;	/* function that freed this connection */
	int l_alloc,	/* line that allocated this connection */
	    l_free;	/* line that freed this connection */
#endif
};

#define llc_sk(__sk) ((struct llc_opt *)(__sk)->protinfo)

struct llc_conn_state_ev;

extern struct sock *__llc_sock_alloc(void);
extern void __llc_sock_free(struct sock *sk, u8 free);

#ifdef DEBUG_LLC_CONN_ALLOC
#define dump_stack() printk(KERN_INFO "call trace: %p, %p, %p\n",	\
				__builtin_return_address(0),		\
				__builtin_return_address(1),		\
				__builtin_return_address(2));
#define llc_sock_alloc()	({					\
	struct sock *__sk = __llc_sock_alloc();				\
	if (__sk) {							\
		llc_sk(__sk)->f_alloc = __FUNCTION__;			\
		llc_sk(__sk)->l_alloc = __LINE__;			\
	}								\
	__sk;})
#define __llc_sock_assert(__sk)						\
	if (llc_sk(__sk)->f_free) {					\
		printk(KERN_ERR						\
		       "%p conn (alloc'd @ %s(%d)) "			\
		       "already freed @ %s(%d) "			\
		       "being used again @ %s(%d)\n",			\
		       llc_sk(__sk),					\
		       llc_sk(__sk)->f_alloc, llc_sk(__sk)->l_alloc,	\
		       llc_sk(__sk)->f_free, llc_sk(__sk)->l_free,	\
		       __FUNCTION__, __LINE__);				\
		dump_stack();
#define llc_sock_free(__sk)						\
{									\
	__llc_sock_assert(__sk)						\
	} else {							\
		__llc_sock_free(__sk, 0);				\
		llc_sk(__sk)->f_free = __FUNCTION__;			\
		llc_sk(__sk)->l_free = __LINE__;			\
	}								\
}
#define llc_sock_assert(__sk)						\
{									\
	__llc_sock_assert(__sk);					\
	return; }							\
}
#define llc_sock_assert_ret(__sk, __ret)				\
{									\
	__llc_sock_assert(__sk);					\
	return __ret; }							\
}
#else /* DEBUG_LLC_CONN_ALLOC */
#define llc_sock_alloc() __llc_sock_alloc()
#define llc_sock_free(__sk) __llc_sock_free(__sk, 1)
#define llc_sock_assert(__sk)
#define llc_sock_assert_ret(__sk)
#endif /* DEBUG_LLC_CONN_ALLOC */

extern void llc_sock_reset(struct sock *sk);
extern int llc_sock_init(struct sock *sk);

/* Access to a connection */
extern int llc_conn_send_ev(struct sock *sk, struct sk_buff *skb);
extern void llc_conn_send_pdu(struct sock *sk, struct sk_buff *skb);
extern void llc_conn_rtn_pdu(struct sock *sk, struct sk_buff *skb);
extern void llc_conn_free_ev(struct sk_buff *skb);
extern void llc_conn_resend_i_pdu_as_cmd(struct sock *sk, u8 nr,
					 u8 first_p_bit);
extern void llc_conn_resend_i_pdu_as_rsp(struct sock *sk, u8 nr,
					 u8 first_f_bit);
extern int llc_conn_remove_acked_pdus(struct sock *conn, u8 nr,
				      u16 *how_many_unacked);
extern struct sock *llc_find_sock(struct llc_sap *sap, struct llc_addr *daddr,
				  struct llc_addr *laddr);
extern u8 llc_data_accept_state(u8 state);
extern void llc_build_offset_table(void);
#endif /* LLC_CONN_H */
