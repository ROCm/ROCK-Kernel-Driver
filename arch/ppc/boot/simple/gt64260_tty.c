/*
 * arch/ppc/boot/simple/gt64260_tty.c
 *
 * Bootloader version of the embedded MPSC/UART driver for the GT64260[A].
 * Note: Due to 64260A errata, DMA will be used for UART input (via SDMA).
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/* This code assumes that the data cache has been disabled (L1, L2, L3). */

#include <linux/config.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>
#include <asm/gt64260_defs.h>

extern void udelay(long);
static void stop_dma(int chan);

static u32	gt64260_base = EV64260_BRIDGE_REG_BASE; /* base addr of 64260 */

inline unsigned
gt64260_in_le32(volatile unsigned *addr)
{
	unsigned ret;

	__asm__ __volatile__("lwbrx %0,0,%1; eieio" : "=r" (ret) :
				     "r" (addr), "m" (*addr));
	return ret;
}

inline void
gt64260_out_le32(volatile unsigned *addr, int val)
{
	__asm__ __volatile__("stwbrx %1,0,%2; eieio" : "=m" (*addr) :
				     "r" (val), "r" (addr));
}

#define GT64260_REG_READ(offs)						\
	(gt64260_in_le32((volatile uint *)(gt64260_base + (offs))))
#define GT64260_REG_WRITE(offs, d)					\
        (gt64260_out_le32((volatile uint *)(gt64260_base + (offs)), (int)(d)))


static struct {
	u32	sdc;
	u32	sdcm;
	u32	rx_desc;
	u32	rx_buf_ptr;
	u32	scrdp;
	u32	tx_desc;
	u32	sctdp;
	u32	sftdp;
} sdma_regs;

#define	SDMA_REGS_INIT(chan) {						\
	sdma_regs.sdc        = GT64260_SDMA_##chan##_SDC;		\
	sdma_regs.sdcm       = GT64260_SDMA_##chan##_SDCM;		\
	sdma_regs.rx_desc    = GT64260_SDMA_##chan##_RX_DESC;		\
	sdma_regs.rx_buf_ptr = GT64260_SDMA_##chan##_RX_BUF_PTR;	\
	sdma_regs.scrdp      = GT64260_SDMA_##chan##_SCRDP;		\
	sdma_regs.tx_desc    = GT64260_SDMA_##chan##_TX_DESC;		\
	sdma_regs.sctdp      = GT64260_SDMA_##chan##_SCTDP;		\
	sdma_regs.sftdp      = GT64260_SDMA_##chan##_SFTDP;		\
}

typedef struct {
	volatile u16 bufsize;
	volatile u16 bytecnt;
	volatile u32 cmd_stat;
	volatile u32 next_desc_ptr;
	volatile u32 buffer;
} gt64260_rx_desc_t;

typedef struct {
	volatile u16 bytecnt;
	volatile u16 shadow;
	volatile u32 cmd_stat;
	volatile u32 next_desc_ptr;
	volatile u32 buffer;
} gt64260_tx_desc_t;

#define	MAX_RESET_WAIT	10000
#define	MAX_TX_WAIT	10000

#define	RX_NUM_DESC	2
#define	TX_NUM_DESC	2

#define	RX_BUF_SIZE	16
#define	TX_BUF_SIZE	16

static gt64260_rx_desc_t rd[RX_NUM_DESC] __attribute__ ((aligned(32)));
static gt64260_tx_desc_t td[TX_NUM_DESC] __attribute__ ((aligned(32)));

static char rx_buf[RX_NUM_DESC * RX_BUF_SIZE] __attribute__ ((aligned(32)));
static char tx_buf[TX_NUM_DESC * TX_BUF_SIZE] __attribute__ ((aligned(32)));

static int cur_rd = 0;
static int cur_td = 0;


#define	RX_INIT_RDP(rdp) {						\
	(rdp)->bufsize = 2;						\
	(rdp)->bytecnt = 0;						\
	(rdp)->cmd_stat = GT64260_SDMA_DESC_CMDSTAT_L |			\
			  GT64260_SDMA_DESC_CMDSTAT_F |			\
			  GT64260_SDMA_DESC_CMDSTAT_O;			\
}

unsigned long
serial_init(int chan, void *ignored)
{
	u32	mpsc_adjust, sdma_adjust, brg_bcr;
	int	i;

	stop_dma(0);
	stop_dma(1);

	if (chan != 1) {
		chan = 0;  /* default to chan 0 if anything but 1 */
		mpsc_adjust = 0;
		sdma_adjust = 0;
		brg_bcr = GT64260_BRG_0_BCR;
		SDMA_REGS_INIT(0);
	}
	else {
		mpsc_adjust = 0x1000;
		sdma_adjust = 0x2000;
		brg_bcr = GT64260_BRG_1_BCR;
		SDMA_REGS_INIT(1);
	}

	/* Set up ring buffers */
	for (i=0; i<RX_NUM_DESC; i++) {
		RX_INIT_RDP(&rd[i]);
		rd[i].buffer = (u32)&rx_buf[i * RX_BUF_SIZE];
		rd[i].next_desc_ptr = (u32)&rd[i+1];
	}
	rd[RX_NUM_DESC - 1].next_desc_ptr = (u32)&rd[0];

	for (i=0; i<TX_NUM_DESC; i++) {
		td[i].bytecnt = 0;
		td[i].shadow = 0;
		td[i].buffer = (u32)&tx_buf[i * TX_BUF_SIZE];
		td[i].cmd_stat = GT64260_SDMA_DESC_CMDSTAT_F |
				 GT64260_SDMA_DESC_CMDSTAT_L;
		td[i].next_desc_ptr = (u32)&td[i+1];
	}
	td[TX_NUM_DESC - 1].next_desc_ptr = (u32)&td[0];

	/* Set MPSC Routing */
        GT64260_REG_WRITE(GT64260_MPSC_MRR, 0x3ffffe38);
        GT64260_REG_WRITE(GT64260_MPP_SERIAL_PORTS_MULTIPLEX, 0x00001102);

	/* MPSC 0/1 Rx & Tx get clocks BRG0/1 */
        GT64260_REG_WRITE(GT64260_MPSC_RCRR, 0x00000100);
        GT64260_REG_WRITE(GT64260_MPSC_TCRR, 0x00000100);

	/* clear pending interrupts */
        GT64260_REG_WRITE(GT64260_SDMA_INTR_MASK, 0);

	GT64260_REG_WRITE(GT64260_SDMA_0_SCRDP + sdma_adjust, &rd[0]);
	GT64260_REG_WRITE(GT64260_SDMA_0_SCTDP + sdma_adjust,
		&td[TX_NUM_DESC - 1]);
	GT64260_REG_WRITE(GT64260_SDMA_0_SFTDP + sdma_adjust,
		&td[TX_NUM_DESC - 1]);

	GT64260_REG_WRITE(GT64260_SDMA_0_SDC + sdma_adjust,
			  GT64260_SDMA_SDC_RFT | GT64260_SDMA_SDC_SFM |
			  GT64260_SDMA_SDC_BLMR | GT64260_SDMA_SDC_BLMT |
			  (3 << 12));

	/* Set BRG to generate proper baud rate */
	GT64260_REG_WRITE(brg_bcr, ((8 << 18) | (1 << 16) | 36));

	/* Put MPSC into UART mode, no null modem, 16x clock mode */
	GT64260_REG_WRITE(GT64260_MPSC_0_MMCRL + mpsc_adjust, 0x000004c4);
	GT64260_REG_WRITE(GT64260_MPSC_0_MMCRH + mpsc_adjust, 0x04400400);

        GT64260_REG_WRITE(GT64260_MPSC_0_CHR_1 + mpsc_adjust, 0);
        GT64260_REG_WRITE(GT64260_MPSC_0_CHR_9 + mpsc_adjust, 0);
        GT64260_REG_WRITE(GT64260_MPSC_0_CHR_10 + mpsc_adjust, 0);
        GT64260_REG_WRITE(GT64260_MPSC_0_CHR_3 + mpsc_adjust, 4);
        GT64260_REG_WRITE(GT64260_MPSC_0_CHR_4 + mpsc_adjust, 0);
        GT64260_REG_WRITE(GT64260_MPSC_0_CHR_5 + mpsc_adjust, 0);
        GT64260_REG_WRITE(GT64260_MPSC_0_CHR_6 + mpsc_adjust, 0);
        GT64260_REG_WRITE(GT64260_MPSC_0_CHR_7 + mpsc_adjust, 0);
        GT64260_REG_WRITE(GT64260_MPSC_0_CHR_8 + mpsc_adjust, 0);

	/* 8 data bits, 1 stop bit */
	GT64260_REG_WRITE(GT64260_MPSC_0_MPCR + mpsc_adjust, (3 << 12));

	GT64260_REG_WRITE(GT64260_SDMA_0_SDCM + sdma_adjust,
		GT64260_SDMA_SDCM_ERD);

	GT64260_REG_WRITE(GT64260_MPSC_0_CHR_2 + sdma_adjust,
		GT64260_MPSC_UART_CR_EH);

	udelay(100);

	return (ulong)chan;
}

static void
stop_dma(int chan)
{
	u32	sdma_sdcm = GT64260_SDMA_0_SDCM;
	int	i;

	if (chan == 1) {
		sdma_sdcm = GT64260_SDMA_1_SDCM;
	}

	/* Abort SDMA Rx, Tx */
	GT64260_REG_WRITE(sdma_sdcm,
		GT64260_SDMA_SDCM_AR | GT64260_SDMA_SDCM_STD);

	for (i=0; i<MAX_RESET_WAIT; i++) {
		if ((GT64260_REG_READ(sdma_sdcm) & (GT64260_SDMA_SDCM_AR |
					GT64260_SDMA_SDCM_AT)) == 0) break;
		udelay(100);
	}

	return;
}

static int
wait_for_ownership(void)
{
	int	i;

	for (i=0; i<MAX_TX_WAIT; i++) {
		if ((GT64260_REG_READ(sdma_regs.sdcm) &
					GT64260_SDMA_SDCM_TXD) == 0) break;
		udelay(1000);
	}

	return (i < MAX_TX_WAIT);
}

void
serial_putc(unsigned long com_port, unsigned char c)
{
	gt64260_tx_desc_t	*tdp;

	if (wait_for_ownership() == 0) return;

	tdp = &td[cur_td];
	if (++cur_td >= TX_NUM_DESC) cur_td = 0;

	*(unchar *)(tdp->buffer ^ 7) = c;
	tdp->bytecnt = 1;
	tdp->shadow = 1;
	tdp->cmd_stat = GT64260_SDMA_DESC_CMDSTAT_L |
		GT64260_SDMA_DESC_CMDSTAT_F | GT64260_SDMA_DESC_CMDSTAT_O;

	GT64260_REG_WRITE(sdma_regs.sctdp, tdp);
	GT64260_REG_WRITE(sdma_regs.sftdp, tdp);
	GT64260_REG_WRITE(sdma_regs.sdcm,
		GT64260_REG_READ(sdma_regs.sdcm) | GT64260_SDMA_SDCM_TXD);

	return;
}

unsigned char
serial_getc(unsigned long com_port)
{
	gt64260_rx_desc_t	*rdp;
	unchar			c = '\0';

	rdp = &rd[cur_rd];

	if ((rdp->cmd_stat & (GT64260_SDMA_DESC_CMDSTAT_O |
			      GT64260_SDMA_DESC_CMDSTAT_ES)) == 0) {
		c = *(unchar *)(rdp->buffer ^ 7);
		RX_INIT_RDP(rdp);
		if (++cur_rd >= RX_NUM_DESC) cur_rd = 0;
	}

	return c;
}

int
serial_tstc(unsigned long com_port)
{
	gt64260_rx_desc_t	*rdp;
	int			loop_count = 0;
	int			rc = 0;

	rdp = &rd[cur_rd];

	/* Go thru rcv desc's until empty looking for one with data (no error)*/
	while (((rdp->cmd_stat & GT64260_SDMA_DESC_CMDSTAT_O) == 0) &&
	       (loop_count++ < RX_NUM_DESC)) {

		/* If there was an error, reinit the desc & continue */
		if ((rdp->cmd_stat & GT64260_SDMA_DESC_CMDSTAT_ES) != 0) {
			RX_INIT_RDP(rdp);
			if (++cur_rd >= RX_NUM_DESC) cur_rd = 0;
			rdp = (gt64260_rx_desc_t *)rdp->next_desc_ptr;
		}
		else {
			rc = 1;
			break;
		}
	}

	return rc;
}

void
serial_close(unsigned long com_port)
{
	stop_dma(com_port);
	return;
}
