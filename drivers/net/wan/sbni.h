/*
 * sbni.h - header file for sbni linux device driver
 *
 * Copyright (C) 1999 Granch ltd., Yaroslav Polyakov (xenon@granch.ru).
 *
 */

/*
 * SBNI12 definitions
 *
 * Revision 2.0.0  1997/08/27
 * Initial revision
 *
 * Revision 2.1.0  1999/04/26
 * dev_priv structure changed to support balancing and some other features.
 *
 */

#ifndef __SBNI_H
#define __SBNI_H

#define SBNI_DEBUG 0

#if SBNI_DEBUG
#define DP( A ) A
#else
#define DP( A )
#endif

typedef unsigned char BOOLEAN;

#define TRUE 1
#define FALSE 0

#define	SBNI_IO_EXTENT	0x4
#define SB_MAX_BUFFER_ARRAY 1023

#define CSR0	0
#define CSR1	1

#define	DAT	2

/* CSR0 mapping */
#define BU_EMP	(1 << 1)	/* r z    */
#define	RC_CHK	(1 << 2)	/* rw     */
#define	CT_ZER	(1 << 3)	/*  w     */
#define	TR_REQ	(1 << 4)	/* rwz*   */

#define TR_RDY	(1 << 5)	/* r z    */
#define EN_INT	(1 << 6)	/* rwz* */
#define RC_RDY	(1 << 7)	/* r z    */

/* CSR1 mapping */
#define PR_RES	(1 << 7)	/*  w     */

struct sbni_csr1 {
	unsigned rxl:5;
	unsigned rate:2;
	unsigned:1;
};

#define DEF_RXL_DELTA	-1
#define DEF_RXL		0xf
#define DEF_RATE	0
#define DEF_FRAME_LEN	(1023 - 14 - 9)

#ifdef MODULE

#define SBNI_MAX_NUM_CARDS 8
#define SBNI_MAX_SLAVES 8


#endif				/* MODULE */

#define SBNI_SIG 0x5a

#define	SB_ETHER_MIN_LEN 60

#define SB_FILLING_CHAR (unsigned char)0x00
#define TR_ERROR_COUNT 32
#define CHANGE_LEVEL_START_TICKS 4
#define SBNI_INTERNAL_QUEUE_SIZE 10	/* 100 ? */

#define PACKET_FIRST_FRAME (unsigned short)0x8000
#define RECEIVE_FRAME_RESEND (unsigned short)0x0800
#define PACKET_RESEND 0x4000
#define PACKET_SEND_OK 0x3000
#define PACKET_LEN_MASK (unsigned short)0x03ff
#define PACKET_INF_MASK (unsigned short)0x7000

#define ETHER_ADDR_LEN 6

#define SBNI_TIMEOUT HZ/10	/* ticks to wait for pong or packet */
		/* sbni watchdog called SBNI_HZ times per sec. */

struct sbni_in_stats {
	unsigned int all_rx_number;
	unsigned int bad_rx_number;
	unsigned int timeout_number;
	unsigned int all_tx_number;
	unsigned int resend_tx_number;
};


/*
 *    Board-specific info in dev->priv. 
 */
struct net_local {
	struct net_device_stats stats;

	struct timer_list watchdog;
	unsigned int realframelen;	/* the current size of the SB-frame */
	unsigned int eth_trans_buffer_len;	/* tx buffer length */
	unsigned int outpos;
	unsigned int inppos;
	unsigned int frame_len;	/* The set SB-frame size */
	unsigned int tr_err;
	unsigned int timer_ticks;
	BOOLEAN last_receive_OK;
	BOOLEAN tr_resend;

	unsigned char wait_frame_number;
	unsigned char eth_trans_buffer[1520];	/* tx buffer */
	unsigned char HSCounter;	/* Reserved field */
	unsigned char eth_rcv_buffer[2600];	/* rx buffer */
	struct sbni_csr1 csr1;
	/* Internal Statistics */
	struct sbni_in_stats in_stats;

	int rxl_curr;		/* current receive level value [0..0xf] */
	int rxl_delta;		/* receive level delta (+1, -1)
				   rxl_delta == 0 - receive level
				   autodetection
				   disabled            */
	unsigned int ok_curr;	/* current ok frames received           */
	unsigned int ok_prev;	/* previous ok frames received          */
	unsigned int timeout_rxl;

	struct sk_buff_head queue;
	struct sk_buff *currframe;
	BOOLEAN waitack;

	struct net_device *m;	/* master */
	struct net_device *me;	/* me */
	struct net_local *next_lp;	/* next lp */

	int carrier;

	spinlock_t lock;
};


struct sbni_hard_header {

	/* internal sbni stuff */
	unsigned int crc;	/* 4 */
	unsigned short packetlen;	/* 2 */
	unsigned char number;	/* 1 */
	unsigned char reserv;	/* 1 */

	/* 8 */

	/* ethernet stuff */
	unsigned char h_dest[ETH_ALEN];		/* destination eth addr */
	unsigned char h_source[ETH_ALEN];	/* source ether addr    */
	unsigned short h_proto;	/* packet type ID field */
	/* +14 */
	/* 22 */

};

#define SBNI_HH_SZ 22

struct sbni_flags {
	unsigned rxl:4;
	unsigned rate:2;
	unsigned fixed_rxl:1;
	unsigned fixed_rate:1;
};

#define RCV_NO 0
#define RCV_OK 1
#define RCV_WR 2


#define SIOCDEVGETINSTATS 	SIOCDEVPRIVATE
#define SIOCDEVRESINSTATS 	SIOCDEVPRIVATE+1
#define SIOCDEVGHWSTATE   	SIOCDEVPRIVATE+2
#define SIOCDEVSHWSTATE   	SIOCDEVPRIVATE+3
#define SIOCDEVENSLAVE  	SIOCDEVPRIVATE+4
#define SIOCDEVEMANSIPATE  	SIOCDEVPRIVATE+5


#endif				/* __SBNI_H */
