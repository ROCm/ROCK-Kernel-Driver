/*
 * linux/include/asm-arm/arch-omap/board-h3.h
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Copyright (C) 2004 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __ASM_ARCH_OMAP_H3_H
#define __ASM_ARCH_OMAP_H3_H

/* In OMAP1710 H3 the Ethernet is directly connected to CS1 */
#define OMAP1710_ETHR_BASE		0xE8000000
#define OMAP1710_ETHR_SIZE		SZ_4K
#define OMAP1710_ETHR_START		0x04000000

/* Intel STRATA NOR flash at CS3 */
#define OMAP_NOR_FLASH_BASE		0xD8000000
#define OMAP_NOR_FLASH_SIZE		SZ_32M
#define OMAP_NOR_FLASH_START		0x00000000

#define MAXIRQNUM			(IH_BOARD_BASE)
#define MAXFIQNUM			MAXIRQNUM
#define MAXSWINUM			MAXIRQNUM

#define NR_IRQS				(MAXIRQNUM + 1)

#define OMAP_MCBSP1_BASE		OMAP1610_MCBSP1_BASE
#define AUDIO_DRR2  (OMAP_MCBSP1_BASE + 0x00)
#define AUDIO_DRR1  (OMAP_MCBSP1_BASE + 0x02)
#define AUDIO_DXR2  (OMAP_MCBSP1_BASE + 0x04)
#define AUDIO_DXR1  (OMAP_MCBSP1_BASE + 0x06)
#define AUDIO_SPCR2 (OMAP_MCBSP1_BASE + 0x08)
#define AUDIO_SPCR1 (OMAP_MCBSP1_BASE + 0x0a)
#define AUDIO_RCR2  (OMAP_MCBSP1_BASE + 0x0c)
#define AUDIO_RCR1  (OMAP_MCBSP1_BASE + 0x0e)
#define AUDIO_XCR2  (OMAP_MCBSP1_BASE + 0x10)
#define AUDIO_XCR1  (OMAP_MCBSP1_BASE + 0x12)
#define AUDIO_SRGR2 (OMAP_MCBSP1_BASE + 0x14)
#define AUDIO_SRGR1 (OMAP_MCBSP1_BASE + 0x16)
#define AUDIO_MCR2  (OMAP_MCBSP1_BASE + 0x18)
#define AUDIO_MCR1  (OMAP_MCBSP1_BASE + 0x1a)
#define AUDIO_RCERA (OMAP_MCBSP1_BASE + 0x1c)
#define AUDIO_RCERB (OMAP_MCBSP1_BASE + 0x1e)
#define AUDIO_XCERA (OMAP_MCBSP1_BASE + 0x20)
#define AUDIO_XCERB (OMAP_MCBSP1_BASE + 0x22)
#define AUDIO_PCR0  (OMAP_MCBSP1_BASE + 0x24)

/* UART3 Registers Maping through MPU bus */
#define OMAP_MPU_UART3_BASE     0xFFFB9800      /* UART3 through MPU bus */
#define UART3_RHR               (OMAP_MPU_UART3_BASE + 0)
#define UART3_THR               (OMAP_MPU_UART3_BASE + 0)
#define UART3_DLL               (OMAP_MPU_UART3_BASE + 0)
#define UART3_IER               (OMAP_MPU_UART3_BASE + 4)
#define UART3_DLH               (OMAP_MPU_UART3_BASE + 4)
#define UART3_IIR               (OMAP_MPU_UART3_BASE + 8)
#define UART3_FCR               (OMAP_MPU_UART3_BASE + 8)
#define UART3_EFR               (OMAP_MPU_UART3_BASE + 8)
#define UART3_LCR               (OMAP_MPU_UART3_BASE + 0x0C)
#define UART3_MCR               (OMAP_MPU_UART3_BASE + 0x10)
#define UART3_XON1_ADDR1        (OMAP_MPU_UART3_BASE + 0x10)
#define UART3_XON2_ADDR2        (OMAP_MPU_UART3_BASE + 0x14)
#define UART3_LSR               (OMAP_MPU_UART3_BASE + 0x14)
#define UART3_TCR               (OMAP_MPU_UART3_BASE + 0x18)
#define UART3_MSR               (OMAP_MPU_UART3_BASE + 0x18)
#define UART3_XOFF1             (OMAP_MPU_UART3_BASE + 0x18)
#define UART3_XOFF2             (OMAP_MPU_UART3_BASE + 0x1C)
#define UART3_SPR               (OMAP_MPU_UART3_BASE + 0x1C)
#define UART3_TLR               (OMAP_MPU_UART3_BASE + 0x1C)
#define UART3_MDR1              (OMAP_MPU_UART3_BASE + 0x20)
#define UART3_MDR2              (OMAP_MPU_UART3_BASE + 0x24)
#define UART3_SFLSR             (OMAP_MPU_UART3_BASE + 0x28)
#define UART3_TXFLL             (OMAP_MPU_UART3_BASE + 0x28)
#define UART3_RESUME            (OMAP_MPU_UART3_BASE + 0x2C)
#define UART3_TXFLH             (OMAP_MPU_UART3_BASE + 0x2C)
#define UART3_SFREGL            (OMAP_MPU_UART3_BASE + 0x30)
#define UART3_RXFLL             (OMAP_MPU_UART3_BASE + 0x30)
#define UART3_SFREGH            (OMAP_MPU_UART3_BASE + 0x34)
#define UART3_RXFLH             (OMAP_MPU_UART3_BASE + 0x34)
#define UART3_BLR               (OMAP_MPU_UART3_BASE + 0x38)
#define UART3_ACREG             (OMAP_MPU_UART3_BASE + 0x3C)
#define UART3_DIV16             (OMAP_MPU_UART3_BASE + 0x3C)
#define UART3_SCR               (OMAP_MPU_UART3_BASE + 0x40)
#define UART3_SSR               (OMAP_MPU_UART3_BASE + 0x44)
#define UART3_EBLR              (OMAP_MPU_UART3_BASE + 0x48)
#define UART3_OSC_12M_SEL       (OMAP_MPU_UART3_BASE + 0x4C)
#define UART3_MVR               (OMAP_MPU_UART3_BASE + 0x50)

#endif /*  __ASM_ARCH_OMAP_H3_H */
