/*
 * include/asm/arch/serial_reg.h
 *
 * Redistribution of this file is permitted under the terms of the GNU 
 * Public License (GPL)
 * 
 * These are the SA1100 UART port assignments, expressed as long index 
 * of the base address.
 */

#ifndef ASM_ARCH_SERIAL_REG_H
#define ASM_ARCH_SERIAL_REG_H


/*
 * Register index.
 */
#define UTCR0 		0	/* 0x00 UART_LCR  Line control register */
#define UTCR1 		1	/* 0x04 UART_DLLSB */
#define UTCR2 		2	/* 0x08 UART_DLMSB */
#define UTCR3 		3	/* 0x0c UART_IER */
#define UTDR 		5	/* 0x14 UART_RX, UART_TX */
#define UTSR0 		7	/* 0x1c */
#define UTSR1 		8	/* 0x20 UART_LSR  Line Status register */

#define UART_RX 	UTDR		/* Receive port, read only */
#define UART_TX 	UTDR		/* transmit port, write only */

#if 0
/*
 * Line control register flags
 */
#define UTCR0_PE 	1		/* Parity enable */
#define UTCR0_EP 	2		/* Even parity */
#define UTCR0_SB 	4		/* Stop bit */
#define UTCR0_DB 	8		/* Data bits in transmission (0 = 7, 1 = 8) */

#define UTCR0_OES	UTCR0_EP
#define UTCR0_SBS	UTCR0_SB
#define UTCR0_DSS	UTCR0_DB
#define UTCR0_SCE	16		/* Sample clock enable */
#define UTCR0_RCE	32		/* Receive clock edge select */
#define UTCR0_TCE	64		/* Transmit clock edge select */


/*
 * Line status bits.
 */
#define UTSR1_TBY 	1		/* transmitter busy flag */
#define UTSR1_RNE 	2		/* receiver not empty (LSR_DR) */
#define UTSR1_TNF 	4		/* transmit fifo non full */
#define UTSR1_PRE 	8		/* parity read error (LSR_PE) */
#define UTSR1_FRE 	16		/* framing error (LSR_FE) */
#define UTSR1_ROR 	32		/* receive fifo overrun (LSR_OE) */

#define UTSR1_ERROR 	(UTSR1_PRE | UTSR1_FRE | UTSR1_ROR)  /* LSR_ERROR */


#define UTSR0_TFS 	1		/* transmit fifo service request */
#define UTSR0_RFS 	2		/* receive fifo service request */
#define UTSR0_RID	4		/* receiver idle */
#define UTSR0_RBB 	8		/* receiver begin of break */
#define UTSR0_REB 	16		/* receiver end of break */
#define UTSR0_EIF 	32		/* error in fifo */


/*
 * Interrupt enable register (IER)
 */
#define UTCR3_RXE 	1		/* Receiver enable */
#define UTCR3_TXE 	2		/* Transmit enable */
#define UTCR3_BRK 	4		/* Break */
#define UTCR3_RIM 	8		/* Receive FIFO interrupt mask (IER_RDA) */
#define UTCR3_TIM 	16		/* Transmit FIFO interrupt mask (IER_THRE) */
#define UTCR3_LBM 	32		/* Loop Back Mode */

#endif 

#endif /* ASM_ARCH_SERIAL_REG_H */
