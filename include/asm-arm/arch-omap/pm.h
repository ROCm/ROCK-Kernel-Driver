/*
 * FILE NAME include/asm/arch-omap/pm.h
 *
 * BRIEF MODULE DESCRIPTION
 *
 * Author: MontaVista Software, Inc.
 *	   support@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * List of global OMAP registers to preserve.  All registers are 16 bits
 * and must be accessed with 16 read/writes.
 * More ones like CP and general purpose register values are preserved
 * with the stack pointer in sleep.S.
 */
#ifndef __ASM_ARCH_OMAP_PM_H
#define __ASM_ARCH_OMAP_PM_H

#define ARM_REG_BASE		(0xfffece00)
#define ARM_ASM_IDLECT1		(ARM_REG_BASE + 0x4)
#define ARM_ASM_IDLECT2		(ARM_REG_BASE + 0x8)
#define ARM_ASM_RSTCT1		(ARM_REG_BASE + 0x10)
#define ARM_ASM_RSTCT2		(ARM_REG_BASE + 0x14)
#define ARM_ASM_SYSST		(ARM_REG_BASE + 0x18)
/*
 * Traffic Controller Memory Interface Registers
 */
#define TCMIF_BASE		0xfffecc00
#define EMIFS_ASM_CONFIG_REG	(TCMIF_BASE + 0x0c)
#define EMIFF_ASM_SDRAM_CONFIG	(TCMIF_BASE + 0x20)
#define IRQ_MIR1		(OMAP_IH1_BASE + IRQ_MIR)

#ifdef CONFIG_ARCH_OMAP1510
#define IRQ_MIR2		(OMAP_IH2_BASE + IRQ_MIR)
#else /* CONFIG_ARCH_OMAP1610 */
#define IRQ_MIR2_0		(OMAP_IH2_0_BASE + IRQ_MIR)
#define IRQ_MIR2_1		(OMAP_IH2_1_BASE + IRQ_MIR)
#define IRQ_MIR2_2		(OMAP_IH2_2_BASE + IRQ_MIR)
#define IRQ_MIR2_3		(OMAP_IH2_3_BASE + IRQ_MIR)
#endif

#define IDLE_WAIT_CYCLES		0x00000fff
#define PERIPHERAL_ENABLE		0x2

#ifdef CONFIG_ARCH_OMAP1510
#define DEEP_SLEEP_REQUEST		0x0ec7
#define BIG_SLEEP_REQUEST		0x0cc5
#define IDLE_LOOP_REQUEST		0x0c00
#define IDLE_CLOCK_DOMAINS		0x2
#else /* CONFIG_ARCH_OMAP1610 */
#define DEEP_SLEEP_REQUEST		0x17c7
#define BIG_SLEEP_REQUEST		TBD
#define IDLE_LOOP_REQUEST		0x0400
#define IDLE_CLOCK_DOMAINS		0x09c7
#endif

#define SELF_REFRESH_MODE		0x0c000001
#define IDLE_EMIFS_REQUEST		0xc
#define MODEM_32K_EN			0x1

#ifndef __ASSEMBLER__
extern void omap_pm_idle(void);
extern void omap_pm_suspend(void);
extern int omap_cpu_suspend(unsigned short, unsigned short);
extern int omap_idle_loop_suspend(void);
extern struct async_struct *omap_pm_sercons;
extern unsigned int serial_in(struct async_struct *, int);
extern unsigned int serial_out(struct async_struct *, int, int);

#ifdef CONFIG_ARCH_OMAP1510
#define OMAP_SRAM_IDLE_SUSPEND	0xd002F000
#define OMAP_SRAM_API_SUSPEND	0xd002F200
#else /* CONFIG_ARCH_OMAP1610 */
#define OMAP_SRAM_IDLE_SUSPEND	0xd0000400
#define OMAP_SRAM_API_SUSPEND	0xd0000600
#endif

#define CPU_SUSPEND_SIZE	200
#define ARM_REG_BASE		(0xfffece00)
#define ARM_ASM_IDLECT1		(ARM_REG_BASE + 0x4)
#define ARM_ASM_IDLECT2		(ARM_REG_BASE + 0x8)
#define ARM_ASM_RSTCT1		(ARM_REG_BASE + 0x10)
#define ARM_ASM_RSTCT2		(ARM_REG_BASE + 0x14)
#define ARM_ASM_SYSST		(ARM_REG_BASE + 0x18)

#define TCMIF_BASE		0xfffecc00
#define PM_EMIFS_CONFIG_REG	(TCMIF_BASE + 0x0c)
#define PM_EMIFF_SDRAM_CONFIG	(TCMIF_BASE + 0x20)
#define FUNC_MUX_CTRL_LOW_PWR	(0xfffe1020)

#ifdef CONFIG_ARCH_OMAP1510
#define ULPD_LOW_POWER_REQ             0x0001
#else /* CONFIG_ARCH_OMAP1610 */
#define ULPD_LOW_POWER_REQ             0x3
#endif
#define ULPD_LOW_PWR                   0x1000
#define ULPD_LOW_POWER_EN              0x0001

#define DSP_IDLE_DELAY			10
#define DSP_IDLE			0x0040
#define DSP_ENABLE			0x0002
#define SUFFICIENT_DSP_RESET_TIME	1000
#define DEFAULT_MPUI_CONFIG		0x05cf
#define ENABLE_XORCLK			0x2
#define DSP_RESET			0x2000
#define TC_IDLE_REQUEST			(0x0000000c)
#define EMIFF_CONFIG_REG		EMIFF_SDRAM_CONFIG


#define ARM_SAVE(x) arm_sleep_save[ARM_SLEEP_SAVE_##x] = omap_readw(x)
#define ARM_RESTORE(x) omap_writew((unsigned short)arm_sleep_save[ARM_SLEEP_SAVE_##x], x)
#define ARM_SHOW(x) arm_sleep_save[ARM_SLEEP_SAVE_##x]

#define ULPD_SAVE(x) ulpd_sleep_save[ULPD_SLEEP_SAVE_##x] = omap_readw(x)
#define ULPD_RESTORE(x) omap_writew((unsigned short)ulpd_sleep_save[ULPD_SLEEP_SAVE_##x], x)
#define ULPD_SHOW(x) ulpd_sleep_save[ULPD_SLEEP_SAVE_##x]

#define MPUI_SAVE(x) mpui_sleep_save[MPUI_SLEEP_SAVE_##x] = omap_readl(x)
#define MPUI_RESTORE(x) omap_writel((unsigned int)mpui_sleep_save[MPUI_SLEEP_SAVE_##x], x)
#define MPUI_SHOW(x) (unsigned int)mpui_sleep_save[MPUI_SLEEP_SAVE_##x]

enum arm_save_state {
	ARM_SLEEP_SAVE_START = 0,
	/*
	 * 9 MPU control registers, all 16 bits
	 */
	ARM_SLEEP_SAVE_ARM_CKCTL, ARM_SLEEP_SAVE_ARM_IDLECT1,
	ARM_SLEEP_SAVE_ARM_IDLECT2, ARM_SLEEP_SAVE_ARM_EWUPCT,
	ARM_SLEEP_SAVE_ARM_RSTCT1, ARM_SLEEP_SAVE_ARM_RSTCT2,
	ARM_SLEEP_SAVE_ARM_SYSST,

	ARM_SLEEP_SAVE_SIZE
};

enum ulpd_save_state {
	ULPD_SLEEP_SAVE_START = 0,
	ULPD_SLEEP_SAVE_ULPD_IT_STATUS_REG, ULPD_SLEEP_SAVE_ULPD_CLOCK_CTRL_REG,
	ULPD_SLEEP_SAVE_ULPD_SOFT_REQ_REG, ULPD_SLEEP_SAVE_ULPD_STATUS_REQ_REG,
	ULPD_SLEEP_SAVE_ULPD_DPLL_CTRL_REG, ULPD_SLEEP_SAVE_ULPD_POWER_CTRL_REG,
	ULPD_SLEEP_SAVE_SIZE
};

enum mpui_save_state {
	/*
	 * MPUI registers 32 bits
	 */
	MPUI_SLEEP_SAVE_MPUI_CTRL_REG, MPUI_SLEEP_SAVE_MPUI_DSP_BOOT_CONFIG,
	MPUI_SLEEP_SAVE_MPUI_DSP_API_CONFIG,
	MPUI_SLEEP_SAVE_MPUI_DSP_STATUS_REG,
	MPUI_SLEEP_SAVE_PM_EMIFF_SDRAM_CONFIG,
	MPUI_SLEEP_SAVE_PM_EMIFS_CONFIG_REG,
	MPUI_SLEEP_SAVE_IRQ_MIR1,
#ifdef CONFIG_ARCH_OMAP1510
	MPUI_SLEEP_SAVE_IRQ_MIR2,
#else /* CONFIG_ARCH_OMAP1610 */
	MPUI_SLEEP_SAVE_IRQ_MIR2_0,
	MPUI_SLEEP_SAVE_IRQ_MIR2_1,
	MPUI_SLEEP_SAVE_IRQ_MIR2_2,
	MPUI_SLEEP_SAVE_IRQ_MIR2_3,
#endif

	MPUI_SLEEP_SAVE_SIZE
};


#endif	/* ASSEMBLER */
#endif
