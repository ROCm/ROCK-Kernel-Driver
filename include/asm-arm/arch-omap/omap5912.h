/* linux/include/asm-arm/arch-omap/omap5912.h
 *
 * Hardware definitions for TI OMAP5912 processor.
 *
 * Written by Dirk Behme <dirk.behme@de.bosch.com>
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

#ifndef __ASM_ARCH_OMAP5912_H
#define __ASM_ARCH_OMAP5912_H

/*
 * ----------------------------------------------------------------------------
 * Base addresses
 * ----------------------------------------------------------------------------
 */

/* Syntax: XX_BASE = Virtual base address, XX_START = Physical base address */

/* OMAP5912 internal SRAM size is 250kByte */
#define OMAP5912_SRAM_BASE	0xD0000000
#define OMAP5912_SRAM_SIZE	0x3E800
#define OMAP5912_SRAM_START	0x20000000

#define OMAP5912_DSP_BASE	0xE0000000
#define OMAP5912_DSP_SIZE	0x50000
#define OMAP5912_DSP_START	0xE0000000

#define OMAP5912_DSPREG_BASE	0xE1000000
#define OMAP5912_DSPREG_SIZE	SZ_128K
#define OMAP5912_DSPREG_START	0xE1000000

/*
 * ---------------------------------------------------------------------------
 * Interrupts
 * ---------------------------------------------------------------------------
 */
#define OMAP_IH2_0_BASE		(0xfffe0000)
#define OMAP_IH2_1_BASE		(0xfffe0100)
#define OMAP_IH2_2_BASE		(0xfffe0200)
#define OMAP_IH2_3_BASE		(0xfffe0300)

#define OMAP_IH2_0_ITR		(OMAP_IH2_0_BASE + 0x00)
#define OMAP_IH2_0_MIR		(OMAP_IH2_0_BASE + 0x04)
#define OMAP_IH2_0_SIR_IRQ	(OMAP_IH2_0_BASE + 0x10)
#define OMAP_IH2_0_SIR_FIQ	(OMAP_IH2_0_BASE + 0x14)
#define OMAP_IH2_0_CONTROL	(OMAP_IH2_0_BASE + 0x18)
#define OMAP_IH2_0_ILR0		(OMAP_IH2_0_BASE + 0x1c)
#define OMAP_IH2_0_ISR		(OMAP_IH2_0_BASE + 0x9c)

#define OMAP_IH2_1_ITR		(OMAP_IH2_1_BASE + 0x00)
#define OMAP_IH2_1_MIR		(OMAP_IH2_1_BASE + 0x04)
#define OMAP_IH2_1_SIR_IRQ	(OMAP_IH2_1_BASE + 0x10)
#define OMAP_IH2_1_SIR_FIQ	(OMAP_IH2_1_BASE + 0x14)
#define OMAP_IH2_1_CONTROL	(OMAP_IH2_1_BASE + 0x18)
#define OMAP_IH2_1_ILR1		(OMAP_IH2_1_BASE + 0x1c)
#define OMAP_IH2_1_ISR		(OMAP_IH2_1_BASE + 0x9c)

#define OMAP_IH2_2_ITR		(OMAP_IH2_2_BASE + 0x00)
#define OMAP_IH2_2_MIR		(OMAP_IH2_2_BASE + 0x04)
#define OMAP_IH2_2_SIR_IRQ	(OMAP_IH2_2_BASE + 0x10)
#define OMAP_IH2_2_SIR_FIQ	(OMAP_IH2_2_BASE + 0x14)
#define OMAP_IH2_2_CONTROL	(OMAP_IH2_2_BASE + 0x18)
#define OMAP_IH2_2_ILR2		(OMAP_IH2_2_BASE + 0x1c)
#define OMAP_IH2_2_ISR		(OMAP_IH2_2_BASE + 0x9c)

#define OMAP_IH2_3_ITR		(OMAP_IH2_3_BASE + 0x00)
#define OMAP_IH2_3_MIR		(OMAP_IH2_3_BASE + 0x04)
#define OMAP_IH2_3_SIR_IRQ	(OMAP_IH2_3_BASE + 0x10)
#define OMAP_IH2_3_SIR_FIQ	(OMAP_IH2_3_BASE + 0x14)
#define OMAP_IH2_3_CONTROL	(OMAP_IH2_3_BASE + 0x18)
#define OMAP_IH2_3_ILR3		(OMAP_IH2_3_BASE + 0x1c)
#define OMAP_IH2_3_ISR		(OMAP_IH2_3_BASE + 0x9c)

/*
 * ----------------------------------------------------------------------------
 * System control registers
 * ----------------------------------------------------------------------------
 */

#define OMAP5912_ARM_IDLECT3	(CLKGEN_REG_BASE + 0x24)

#endif /*  __ASM_ARCH_OMAP5912_H */

