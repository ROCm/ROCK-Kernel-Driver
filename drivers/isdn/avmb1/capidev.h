/*
 * $Id: capidev.h,v 1.6.6.1 2001/05/17 20:41:51 kai Exp $
 *
 * CAPI 2.0 Interface for Linux
 *
 * (c) Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 */

struct capidev {
	struct capidev *next;
	struct file    *file;
	__u16		applid;
	__u16		errcode;
	unsigned int    minor;

	struct sk_buff_head recv_queue;
	wait_queue_head_t recv_wait;

	/* Statistic */
	unsigned long	nrecvctlpkt;
	unsigned long	nrecvdatapkt;
	unsigned long	nsentctlpkt;
	unsigned long	nsentdatapkt;
};
