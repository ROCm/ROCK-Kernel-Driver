/*
 * include/asm-v850/nb85e_teg.h -- NB85E-TEG cpu chip
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_NB85E_TEG_H__
#define __V850_NB85E_TEG_H__

/* The NB85E_TEG uses the NB85E cpu core.  */
#include <asm/nb85e.h>

#define CHIP		"v850e/nb85e-teg"
#define CHIP_LONG	"NEC V850E/NB85E TEG"

/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).  */
#define IRQ_INTOV(n)	(n)	/* 0-3 */
#define IRQ_INTOV_NUM	4
#define IRQ_INTCMD(n)	(0x1c + (n)) /* interval timer interrupts 0-3 */
#define IRQ_INTCMD_NUM	4
#define IRQ_INTDMA(n)	(0x20 + (n)) /* DMA interrupts 0-3 */
#define IRQ_INTDMA_NUM	4
#define IRQ_INTCSI(n)	(0x24 + (n)) /* CSI 0-2 transmit/receive completion */
#define IRQ_INTCSI_NUM	3
#define IRQ_INTSER(n)	(0x25 + (n)) /* UART 0-2 reception error */
#define IRQ_INTSER_NUM	3
#define IRQ_INTSR(n)	(0x26 + (n)) /* UART 0-2 reception completion */
#define IRQ_INTSR_NUM	3
#define IRQ_INTST(n)	(0x27 + (n)) /* UART 0-2 transmission completion */
#define IRQ_INTST_NUM	3

/* For <asm/irq.h> */
#define NUM_MACH_IRQS	0x30

/* TEG UART details.  */
#define NB85E_UART_BASE_ADDR(n)		(0xFFFFF600 + 0x10 * (n))
#define NB85E_UART_ASIM_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x0)
#define NB85E_UART_ASIS_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x2)
#define NB85E_UART_ASIF_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x4)
#define NB85E_UART_CKSR_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x6)
#define NB85E_UART_BRGC_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x8)
#define NB85E_UART_TXB_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0xA)
#define NB85E_UART_RXB_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0xC)
#define NB85E_UART_NUM_CHANNELS		1
#define NB85E_UART_BASE_FREQ		CPU_CLOCK_FREQ

/* The TEG RTPU.  */
#define NB85E_RTPU_BASE_ADDR		0xFFFFF210


#endif /* __V850_NB85E_TEG_H__ */
