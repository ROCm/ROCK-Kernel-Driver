/*
 * serial.h: Arch-dep definitions for the Etrax100 serial driver.
 *
 * Copyright (C) 1998, 1999, 2000 Axis Communications AB
 */

#ifndef _ETRAX_SERIAL_H
#define _ETRAX_SERIAL_H

#include <linux/config.h>
#include <linux/circ_buf.h>
#include <asm/termios.h>

/* Software state per channel */

#ifdef __KERNEL__
/*
 * This is our internal structure for each serial port's state.
 * 
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

struct e100_serial {
	int                   baud;
	volatile u8 *port;             /* R_SERIALx_CTRL */
	u32 irq;                       /* bitnr in R_IRQ_MASK2 for dmaX_descr */

	volatile u8 *oclrintradr;      /* adr to R_DMA_CHx_CLR_INTR, output */
	volatile u32 *ofirstadr;       /* adr to R_DMA_CHx_FIRST, output */
	volatile u8 *ocmdadr;          /* adr to R_DMA_CHx_CMD, output */
	const volatile u8 *ostatusadr; /* adr to R_DMA_CHx_STATUS, output */
	volatile u32 *ohwswadr;        /* adr to R_DMA_CHx_HWSW, output */

	volatile u8 *iclrintradr;      /* adr to R_DMA_CHx_CLR_INTR, input */
	volatile u32 *ifirstadr;       /* adr to R_DMA_CHx_FIRST, input */
	volatile u8 *icmdadr;          /* adr to R_DMA_CHx_CMD, input */
	const volatile u8 *istatusadr; /* adr to R_DMA_CHx_STATUS, input */
	volatile u32 *ihwswadr;        /* adr to R_DMA_CHx_HWSW, input */

	int			flags; 		/* defined in tty.h */

	u8           rx_ctrl; /* shadow for R_SERIALx_REC_CTRL */
	u8           tx_ctrl; /* shadow for R_SERIALx_TR_CTRL */
	u8           iseteop; /* bit number for R_SET_EOP for the input dma */
	int          enabled;    /* Set to 1 if the port is enabled in HW config */
  
  
/* end of fields defined in rs_table[] in .c-file */
	int          uses_dma; /* Set to 1 if DMA should be used */
	unsigned char           fifo_didmagic; /* a fifo eop has been forced */

	struct etrax_dma_descr tr_descr, rec_descr;

	int                     fifo_magic; /* fifo amount - bytes left in dma buffer */

	volatile int            tr_running; /* 1 if output is running */

	struct tty_struct 	*tty;
	int			read_status_mask;
	int			ignore_status_mask;
	int			x_char;	/* xon/xoff character */
	int			close_delay;
	unsigned short	        closing_wait;
	unsigned short	        closing_wait2;
	unsigned long		event;
	unsigned long		last_active;
	int			line;
	int                     type;  /* PORT_ETRAX */
	int			count;	    /* # of fd on device */
	int			blocked_open; /* # of blocked opens */
	long			session; /* Session of opening process */
	long			pgrp; /* pgrp of opening process */
	struct circ_buf         xmit;

	struct tq_struct	tqueue;
	struct async_icount     icount;   /* error-statistics etc.*/
	struct termios	        normal_termios;
	struct termios	        callout_termios;
#ifdef DECLARE_WAITQUEUE
	wait_queue_head_t       open_wait;
        wait_queue_head_t       close_wait;
#else   
        struct wait_queue       *open_wait;
        struct wait_queue       *close_wait;
#endif  

#ifdef CONFIG_RS485
	struct rs485_control    rs485;  /* RS-485 support */
#endif
};

/* this PORT is not in the standard serial.h. it's not actually used for
 * anything since we only have one type of async serial-port anyway in this
 * system.
 */

#define PORT_ETRAX 1

/*
 * Events are used to schedule things to happen at timer-interrupt
 * time, instead of at rs interrupt time.
 */
#define RS_EVENT_WRITE_WAKEUP	0

#endif /* __KERNEL__ */

#endif /* !(_ETRAX_SERIAL_H) */
