/*
 * FILE NAME
 *	arch/mips/vr41xx/vr4122/common/icu.c
 *
 * BRIEF MODULE DESCRIPTION
 *	Interrupt Control Unit routines for the NEC VR4122 and VR4131.
 *
 * Author: Yoichi Yuasa
 *         yyuasa@mvista.com or source@mvista.com
 *
 * Copyright 2001,2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*
 * Changes:
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - Added support for NEC VR4111 and VR4121.
 *
 *  Paul Mundt <lethal@chaoticdreams.org>
 *  - kgdb support.
 *
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/cpu.h>
#include <asm/gdb-stub.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/vr41xx/vr41xx.h>

extern asmlinkage void vr41xx_handle_interrupt(void);

extern void __init init_generic_irq(void);
extern void mips_cpu_irq_init(u32 irq_base);

extern void vr41xx_giuint_init(void);
extern unsigned int giuint_do_IRQ(int pin, struct pt_regs *regs);

static u32 vr41xx_icu1_base = 0;
static u32 vr41xx_icu2_base = 0;

#define VR4111_SYSINT1REG	KSEG1ADDR(0x0b000080)
#define VR4111_SYSINT2REG	KSEG1ADDR(0x0b000200)

#define VR4122_SYSINT1REG	KSEG1ADDR(0x0f000080)
#define VR4122_SYSINT2REG	KSEG1ADDR(0x0f0000a0)

#define SYSINT1REG	0x00
#define GIUINTLREG	0x08
#define MSYSINT1REG	0x0c
#define MGIUINTLREG	0x14
#define NMIREG		0x18
#define SOFTREG		0x1a

#define SYSINT2REG	0x00
#define GIUINTHREG	0x02
#define MSYSINT2REG	0x06
#define MGIUINTHREG	0x08

#define read_icu1(offset)	readw(vr41xx_icu1_base + (offset))
#define write_icu1(val, offset)	writew((val), vr41xx_icu1_base + (offset))

#define read_icu2(offset)	readw(vr41xx_icu2_base + (offset))
#define write_icu2(val, offset)	writew((val), vr41xx_icu2_base + (offset))

static inline u16 set_icu1(u16 offset, u16 set)
{
	u16 res;

	res = read_icu1(offset);
	res |= set;
	write_icu1(res, offset);

	return res;
}

static inline u16 clear_icu1(u16 offset, u16 clear)
{
	u16 res;

	res = read_icu1(offset);
	res &= ~clear;
	write_icu1(res, offset);

	return res;
}

static inline u16 set_icu2(u16 offset, u16 set)
{
	u16 res;

	res = read_icu2(offset);
	res |= set;
	write_icu2(res, offset);

	return res;
}

static inline u16 clear_icu2(u16 offset, u16 clear)
{
	u16 res;

	res = read_icu2(offset);
	res &= ~clear;
	write_icu2(res, offset);

	return res;
}

/*=======================================================================*/

static void enable_sysint1_irq(unsigned int irq)
{
	set_icu1(MSYSINT1REG, (u16)1 << (irq - SYSINT1_IRQ_BASE));
}

static void disable_sysint1_irq(unsigned int irq)
{
	clear_icu1(MSYSINT1REG, (u16)1 << (irq - SYSINT1_IRQ_BASE));
}

static unsigned int startup_sysint1_irq(unsigned int irq)
{
	set_icu1(MSYSINT1REG, (u16)1 << (irq - SYSINT1_IRQ_BASE));

	return 0; /* never anything pending */
}

#define shutdown_sysint1_irq	disable_sysint1_irq
#define ack_sysint1_irq		disable_sysint1_irq

static void end_sysint1_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		set_icu1(MSYSINT1REG, (u16)1 << (irq - SYSINT1_IRQ_BASE));
}

static struct hw_interrupt_type sysint1_irq_type = {
	"SYSINT1",
	startup_sysint1_irq,
	shutdown_sysint1_irq,
	enable_sysint1_irq,
	disable_sysint1_irq,
	ack_sysint1_irq,
	end_sysint1_irq,
	NULL
};

/*=======================================================================*/

static void enable_sysint2_irq(unsigned int irq)
{
	set_icu2(MSYSINT2REG, (u16)1 << (irq - SYSINT2_IRQ_BASE));
}

static void disable_sysint2_irq(unsigned int irq)
{
	clear_icu2(MSYSINT2REG, (u16)1 << (irq - SYSINT2_IRQ_BASE));
}

static unsigned int startup_sysint2_irq(unsigned int irq)
{
	set_icu2(MSYSINT2REG, (u16)1 << (irq - SYSINT2_IRQ_BASE));

	return 0; /* never anything pending */
}

#define shutdown_sysint2_irq	disable_sysint2_irq
#define ack_sysint2_irq		disable_sysint2_irq

static void end_sysint2_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		set_icu2(MSYSINT2REG, (u16)1 << (irq - SYSINT2_IRQ_BASE));
}

static struct hw_interrupt_type sysint2_irq_type = {
	"SYSINT2",
	startup_sysint2_irq,
	shutdown_sysint2_irq,
	enable_sysint2_irq,
	disable_sysint2_irq,
	ack_sysint2_irq,
	end_sysint2_irq,
	NULL
};

/*=======================================================================*/

static void enable_giuint_irq(unsigned int irq)
{
	int pin;

	pin = irq - GIU_IRQ_BASE;
	if (pin < 16)
		set_icu1(MGIUINTLREG, (u16)1 << pin);
	else
		set_icu2(MGIUINTHREG, (u16)1 << (pin - 16));

	vr41xx_enable_giuint(pin);
}

static void disable_giuint_irq(unsigned int irq)
{
	int pin;

	pin = irq - GIU_IRQ_BASE;
	vr41xx_disable_giuint(pin);

	if (pin < 16)
		clear_icu1(MGIUINTLREG, (u16)1 << pin);
	else
		clear_icu2(MGIUINTHREG, (u16)1 << (pin - 16));
}

static unsigned int startup_giuint_irq(unsigned int irq)
{
	vr41xx_clear_giuint(irq - GIU_IRQ_BASE);

	enable_giuint_irq(irq);

	return 0; /* never anything pending */
}

#define shutdown_giuint_irq	disable_giuint_irq

static void ack_giuint_irq(unsigned int irq)
{
	disable_giuint_irq(irq);

	vr41xx_clear_giuint(irq - GIU_IRQ_BASE);
}

static void end_giuint_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		enable_giuint_irq(irq);
}

static struct hw_interrupt_type giuint_irq_type = {
	"GIUINT",
	startup_giuint_irq,
	shutdown_giuint_irq,
	enable_giuint_irq,
	disable_giuint_irq,
	ack_giuint_irq,
	end_giuint_irq,
	NULL
};

/*=======================================================================*/

static struct irqaction icu_cascade = {no_action, 0, 0, "cascade", NULL, NULL};

static void __init vr41xx_icu_init(void)
{
	int i;

	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
	case CPU_VR4121:
		vr41xx_icu1_base = VR4111_SYSINT1REG;
		vr41xx_icu2_base = VR4111_SYSINT2REG;
		break;
	case CPU_VR4122:
	case CPU_VR4131:
		vr41xx_icu1_base = VR4122_SYSINT1REG;
		vr41xx_icu2_base = VR4122_SYSINT2REG;
		break;
	default:
		panic("Unexpected CPU of NEC VR4100 series");
		break;
	}

	write_icu1(0, MSYSINT1REG);
	write_icu1(0, MGIUINTLREG);

	write_icu2(0, MSYSINT2REG);
	write_icu2(0, MGIUINTHREG);

	for (i = SYSINT1_IRQ_BASE; i <= GIU_IRQ_LAST; i++) {
		if (i >= SYSINT1_IRQ_BASE && i <= SYSINT1_IRQ_LAST)
			irq_desc[i].handler = &sysint1_irq_type;
		else if (i >= SYSINT2_IRQ_BASE && i <= SYSINT2_IRQ_LAST)
			irq_desc[i].handler = &sysint2_irq_type;
		else if (i >= GIU_IRQ_BASE && i <= GIU_IRQ_LAST)
			irq_desc[i].handler = &giuint_irq_type;
	}

	setup_irq(ICU_CASCADE_IRQ, &icu_cascade);
}

void __init init_IRQ(void)
{
	memset(irq_desc, 0, sizeof(irq_desc));

	init_generic_irq();
	mips_cpu_irq_init(MIPS_CPU_IRQ_BASE);
	vr41xx_icu_init();

	vr41xx_giuint_init();

	set_except_vector(0, vr41xx_handle_interrupt);

#ifdef CONFIG_KGDB
	printk("Setting debug traps - please connect the remote debugger.\n");
	set_debug_traps();
	breakpoint();
#endif
}

/*=======================================================================*/

static inline void giuint_irqdispatch(u16 pendl, u16 pendh, struct pt_regs *regs)
{
	int i;

	if (pendl) {
		for (i = 0; i < 16; i++) {
			if (pendl & (0x0001 << i)) {
				giuint_do_IRQ(i, regs);
				return;
			}
		}
	}
	else if (pendh) {
		for (i = 0; i < 16; i++) {
			if (pendh & (0x0001 << i)) {
				giuint_do_IRQ(i + 16, regs);
				return;
			}
		}
	}
}

asmlinkage void icu_irqdispatch(struct pt_regs *regs)
{
	u16 pend1, pend2, pendl, pendh;
	u16 mask1, mask2, maskl, maskh;
	int i;

	pend1 = read_icu1(SYSINT1REG);
	mask1 = read_icu1(MSYSINT1REG);

	pend2 = read_icu2(SYSINT2REG);
	mask2 = read_icu2(MSYSINT2REG);

	pendl = read_icu1(GIUINTLREG);
	maskl = read_icu1(MGIUINTLREG);

	pendh = read_icu2(GIUINTHREG);
	maskh = read_icu2(MGIUINTHREG);

	pend1 &= mask1;
	pend2 &= mask2;
	pendl &= maskl;
	pendh &= maskh;

	if (pend1) {
		if ((pend1 & 0x01ff) == 0x0100) {
			giuint_irqdispatch(pendl, pendh, regs);
		}
		else {
			for (i = 0; i < 16; i++) {
				if (pend1 & (0x0001 << i)) {
					do_IRQ(SYSINT1_IRQ_BASE + i, regs);
					break;
				}
			}
		}
		return;
	}
	else if (pend2) {
		for (i = 0; i < 16; i++) {
			if (pend2 & (0x0001 << i)) {
				do_IRQ(SYSINT2_IRQ_BASE + i, regs);
				break;
			}
		}
	}
}
