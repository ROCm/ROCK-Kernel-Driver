/* linux/include/asm-arm/arch-s3c2410/regs-serial.h
 *
 *  From linux/include/asm-arm/hardware/serial_s3c2410.h
 *
 *  Internal header file for Samsung S3C2410 serial ports (UART0-2)
 *
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *
 *  Additional defines, (c) 2003 Simtec Electronics (linux@simtec.co.uk)
 *
 *  Adapted from:
 *
 *  Internal header file for MX1ADS serial ports (UART1 & 2)
 *
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARM_REGS_SERIAL_H
#define __ASM_ARM_REGS_SERIAL_H

#define S3C2410_VA_UART0      (S3C2410_VA_UART)
#define S3C2410_VA_UART1      (S3C2410_VA_UART + 0x4000 )
#define S3C2410_VA_UART2      (S3C2410_VA_UART + 0x8000 )

#define S3C2410_PA_UART0      (S3C2410_PA_UART)
#define S3C2410_PA_UART1      (S3C2410_PA_UART + 0x4000 )
#define S3C2410_PA_UART2      (S3C2410_PA_UART + 0x8000 )

#define S3C2410_URXH	  (0x24)
#define S3C2410_UTXH	  (0x20)
#define S3C2410_ULCON	  (0x00)
#define S3C2410_UCON	  (0x04)
#define S3C2410_UFCON	  (0x08)
#define S3C2410_UMCON	  (0x0C)
#define S3C2410_UBRDIV	  (0x28)
#define S3C2410_UTRSTAT	  (0x10)
#define S3C2410_UERSTAT	  (0x14)
#define S3C2410_UFSTAT	  (0x18)
#define S3C2410_UMSTAT	  (0x1C)

#define S3C2410_LCON_CFGMASK	  ((0xF<<3)|(0x3))

#define S3C2410_LCON_CS5	  (0x0)
#define S3C2410_LCON_CS6	  (0x1)
#define S3C2410_LCON_CS7	  (0x2)
#define S3C2410_LCON_CS8	  (0x3)
#define S3C2410_LCON_CSMASK	  (0x3)

#define S3C2410_LCON_PNONE	  (0x0)
#define S3C2410_LCON_PEVEN	  (0x5 << 3)
#define S3C2410_LCON_PODD	  (0x4 << 3)
#define S3C2410_LCON_PMASK	  (0x7 << 3)

#define S3C2410_LCON_STOPB	  (1<<2)

#define S3C2410_UCON_UCLK	  (1<<10)
#define S3C2410_UCON_SBREAK	  (1<<4)

#define S3C2410_UCON_TXILEVEL	  (1<<9)
#define S3C2410_UCON_RXILEVEL	  (1<<8)
#define S3C2410_UCON_TXIRQMODE	  (1<<2)
#define S3C2410_UCON_RXIRQMODE	  (1<<0)
#define S3C2410_UCON_RXFIFO_TOI	  (1<<7)

#define S3C2410_UCON_DEFAULT	  (S3C2410_UCON_TXILEVEL | S3C2410_UCON_RXILEVEL \
				   | S3C2410_UCON_TXIRQMODE | S3C2410_UCON_RXIRQMODE \
				   | S3C2410_UCON_RXFIFO_TOI)

#define S3C2410_UFCON_FIFOMODE	  (1<<0)
#define S3C2410_UFCON_TXTRIG0	  (0<<6)
#define S3C2410_UFCON_RXTRIG8	  (1<<4)
#define S3C2410_UFCON_RXTRIG12	  (2<<4)

#define S3C2410_UFCON_RESETBOTH	  (3<<1)

#define S3C2410_UFCON_DEFAULT	  (S3C2410_UFCON_FIFOMODE | S3C2410_UFCON_TXTRIG0 \
				  | S3C2410_UFCON_RXTRIG8 )

#define S3C2410_UFSTAT_TXFULL	  (1<<9)
#define S3C2410_UFSTAT_RXFULL	  (1<<8)
#define S3C2410_UFSTAT_TXMASK	  (15<<4)
#define S3C2410_UFSTAT_TXSHIFT	  (4)
#define S3C2410_UFSTAT_RXMASK	  (15<<0)
#define S3C2410_UFSTAT_RXSHIFT	  (0)

#define S3C2410_UTRSTAT_TXFE	  (1<<1)
#define S3C2410_UTRSTAT_RXDR	  (1<<0)

#define S3C2410_UERSTAT_OVERRUN	  (1<<0)
#define S3C2410_UERSTAT_FRAME	  (1<<2)
#define S3C2410_UERSTAT_ANY	  (S3C2410_UERSTAT_OVERRUN | S3C2410_UERSTAT_FRAME)

/* fifo size information */

#ifndef __ASSEMBLY__
static inline int S3C2410_UFCON_RXC(int fcon)
{
	if (fcon & S3C2410_UFSTAT_RXFULL)
		return 16;

	return ((fcon) & S3C2410_UFSTAT_RXMASK) >> S3C2410_UFSTAT_RXSHIFT;
}

static inline int S3C2410_UFCON_TXC(int fcon)
{
	if (fcon & S3C2410_UFSTAT_TXFULL)
		return 16;

	return ((fcon) & S3C2410_UFSTAT_TXMASK) >> S3C2410_UFSTAT_TXSHIFT;
}
#endif /* __ASSEMBLY__ */

#define S3C2410_UMSTAT_CTS	  (1<<0)
#define S3C2410_UMSTAT_DeltaCTS	  (1<<2)

#ifndef __ASSEMBLY__
/* configuration structure for per-machine configurations for the
 * serial port
 *
 * the pointer is setup by the machine specific initialisation from the
 * arch/arm/mach-s3c2410/ directory.
*/

struct s3c2410_uartcfg {
	unsigned char	   hwport;	 /* hardware port number */
	unsigned char	   unused;
	unsigned short	   flags;

	unsigned long	  *clock;	 /* pointer to clock rate */

	unsigned long	   ucon;	 /* value of ucon for port */
	unsigned long	   ulcon;	 /* value of ulcon for port */
	unsigned long	   ufcon;	 /* value of ufcon for port */
};

extern struct s3c2410_uartcfg *s3c2410_uartcfgs;

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ARM_REGS_SERIAL_H */

