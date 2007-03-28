/*
 * arch/ppc/syslib/bestcomm/fec.h
 *
 * Driver for MPC52xx processor BestComm FEC controller
 *
 * Author: Dale Farnsworth <dfarnsworth@mvista.com>
 *
 * 2003-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * HISTORY:
 *
 * 2005-08-14	Converted to platform driver by
 *		Andrey Volkov <avolkov@varma-el.com>, Varma Electronics Oy
 */

#ifndef __BESTCOMM_FEC_H__
#define __BESTCOMM_FEC_H__


/* rx task vars that need to be set before enabling the task */
struct sdma_fec_rx_var {
	u32 enable;		/* (u16*) address of task's control register */
	u32 fifo;		/* (u32*) address of fec's fifo */
	u32 bd_base;		/* (struct sdma_bd*) beginning of ring buffer */
	u32 bd_last;		/* (struct sdma_bd*) end of ring buffer */
	u32 bd_start;		/* (struct sdma_bd*) current bd */
	u32 buffer_size;	/* size of receive buffer */
};

/* rx task incs that need to be set before enabling the task */
struct sdma_fec_rx_inc {
	u16 pad0;
	s16 incr_bytes;
	u16 pad1;
	s16 incr_dst;
	u16 pad2;
	s16 incr_dst_ma;
};

/* tx task vars that need to be set before enabling the task */
struct sdma_fec_tx_var {
	u32 DRD;		/* (u32*) address of self-modified DRD */
	u32 fifo;		/* (u32*) address of fec's fifo */
	u32 enable;		/* (u16*) address of task's control register */
	u32 bd_base;		/* (struct sdma_bd*) beginning of ring buffer */
	u32 bd_last;		/* (struct sdma_bd*) end of ring buffer */
	u32 bd_start;		/* (struct sdma_bd*) current bd */
	u32 buffer_size;	/* set by uCode for each packet */
};

/* tx task incs that need to be set before enabling the task */
struct sdma_fec_tx_inc {
	u16 pad0;
	s16 incr_bytes;
	u16 pad1;
	s16 incr_src;
	u16 pad2;
	s16 incr_src_ma;
};

extern int sdma_fec_rx_init(struct sdma *s, phys_addr_t fifo, int maxbufsize);
extern int sdma_fec_tx_init(struct sdma *s, phys_addr_t fifo);

extern u32 sdma_fec_rx_task[];
extern u32 sdma_fec_tx_task[];


#endif  /* __BESTCOMM_FEC_H__ */
