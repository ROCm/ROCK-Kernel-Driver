/*
 * BRIEF MODULE DESCRIPTION
 *	Au1000 interrupt routines.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *		ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/delay.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/au1000.h>

#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5)

#undef DEBUG_IRQ
#ifdef DEBUG_IRQ
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk("%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define EXT_INTC0_REQ0 2 /* IP 2 */
#define EXT_INTC0_REQ1 3 /* IP 3 */
#define EXT_INTC1_REQ0 4 /* IP 4 */
#define EXT_INTC1_REQ1 5 /* IP 5 */
#define MIPS_TIMER_IP  7 /* IP 7 */

#ifdef CONFIG_REMOTE_DEBUG
extern void breakpoint(void);
#endif

extern asmlinkage void au1000_IRQ(void);

extern void set_debug_traps(void);
extern irq_cpustat_t irq_stat [];
extern irq_desc_t irq_desc[NR_IRQS];

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];

static void setup_local_irq(unsigned int irq, int type, int int_req);
static unsigned int startup_irq(unsigned int irq);
static void end_irq(unsigned int irq_nr);
static inline void mask_and_ack_level_irq(unsigned int irq_nr);
static inline void mask_and_ack_rise_edge_irq(unsigned int irq_nr);
static inline void mask_and_ack_fall_edge_irq(unsigned int irq_nr);
static inline void local_enable_irq(unsigned int irq_nr);
static inline void local_disable_irq(unsigned int irq_nr);

unsigned long spurious_interrupts;
extern unsigned int do_IRQ(int irq, struct pt_regs *regs);
extern void __init init_generic_irq(void);

static inline void sync(void)
{
	__asm volatile ("sync");
}


/* Function for careful CP0 interrupt mask access */
static inline void modify_cp0_intmask(unsigned clr_mask, unsigned set_mask)
{
	unsigned long status = read_32bit_cp0_register(CP0_STATUS);
	status &= ~((clr_mask & 0xFF) << 8);
	status |=   (set_mask & 0xFF) << 8;
	write_32bit_cp0_register(CP0_STATUS, status);
}


static inline void mask_cpu_irq_input(unsigned int irq_nr)
{
	modify_cp0_intmask(irq_nr, 0);
}


static inline void unmask_cpu_irq_input(unsigned int irq_nr)
{
	modify_cp0_intmask(0, irq_nr);
}


static void disable_cpu_irq_input(unsigned int irq_nr)
{
	unsigned long flags;

	save_and_cli(flags);
	mask_cpu_irq_input(irq_nr);
	restore_flags(flags);
}


static void enable_cpu_irq_input(unsigned int irq_nr)
{
	unsigned long flags;

	save_and_cli(flags);
	unmask_cpu_irq_input(irq_nr);
	restore_flags(flags);
}


static void setup_local_irq(unsigned int irq_nr, int type, int int_req)
{
	/* Config2[n], Config1[n], Config0[n] */
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		switch (type) {
			case INTC_INT_RISE_EDGE: /* 0:0:1 */
				outl(1<<irq_nr,INTC1_CONFIG2_CLEAR);
				outl(1<<irq_nr, INTC1_CONFIG1_CLEAR);
				outl(1<<irq_nr, INTC1_CONFIG0_SET);
				break;
			case INTC_INT_FALL_EDGE: /* 0:1:0 */
				outl(1<<irq_nr, INTC1_CONFIG2_CLEAR);
				outl(1<<irq_nr, INTC1_CONFIG1_SET);
				outl(1<<irq_nr, INTC1_CONFIG0_CLEAR);
				break;
			case INTC_INT_HIGH_LEVEL: /* 1:0:1 */
				outl(1<<irq_nr, INTC1_CONFIG2_SET);
				outl(1<<irq_nr, INTC1_CONFIG1_CLEAR);
				outl(1<<irq_nr, INTC1_CONFIG0_SET);
				break;
			case INTC_INT_LOW_LEVEL: /* 1:1:0 */
				outl(1<<irq_nr, INTC1_CONFIG2_SET);
				outl(1<<irq_nr, INTC1_CONFIG1_SET);
				outl(1<<irq_nr, INTC1_CONFIG0_CLEAR);
				break;
			case INTC_INT_DISABLED: /* 0:0:0 */
				outl(1<<irq_nr, INTC1_CONFIG0_CLEAR);
				outl(1<<irq_nr, INTC1_CONFIG1_CLEAR);
				outl(1<<irq_nr, INTC1_CONFIG2_CLEAR);
				break;
			default: /* disable the interrupt */
				printk("unexpected int type %d (irq %d)\n", type, irq_nr);
				outl(1<<irq_nr, INTC1_CONFIG0_CLEAR);
				outl(1<<irq_nr, INTC1_CONFIG1_CLEAR);
				outl(1<<irq_nr, INTC1_CONFIG2_CLEAR);
				return;
		}
		if (int_req) /* assign to interrupt request 1 */
			outl(1<<irq_nr, INTC1_ASSIGN_REQ_CLEAR);
		else	     /* assign to interrupt request 0 */
			outl(1<<irq_nr, INTC1_ASSIGN_REQ_SET);
		outl(1<<irq_nr, INTC1_SOURCE_SET);
		outl(1<<irq_nr, INTC1_MASK_CLEAR);
	}
	else {
		switch (type) {
			case INTC_INT_RISE_EDGE: /* 0:0:1 */
				outl(1<<irq_nr,INTC0_CONFIG2_CLEAR);
				outl(1<<irq_nr, INTC0_CONFIG1_CLEAR);
				outl(1<<irq_nr, INTC0_CONFIG0_SET);
				break;
			case INTC_INT_FALL_EDGE: /* 0:1:0 */
				outl(1<<irq_nr, INTC0_CONFIG2_CLEAR);
				outl(1<<irq_nr, INTC0_CONFIG1_SET);
				outl(1<<irq_nr, INTC0_CONFIG0_CLEAR);
				break;
			case INTC_INT_HIGH_LEVEL: /* 1:0:1 */
				outl(1<<irq_nr, INTC0_CONFIG2_SET);
				outl(1<<irq_nr, INTC0_CONFIG1_CLEAR);
				outl(1<<irq_nr, INTC0_CONFIG0_SET);
				break;
			case INTC_INT_LOW_LEVEL: /* 1:1:0 */
				outl(1<<irq_nr, INTC0_CONFIG2_SET);
				outl(1<<irq_nr, INTC0_CONFIG1_SET);
				outl(1<<irq_nr, INTC0_CONFIG0_CLEAR);
				break;
			case INTC_INT_DISABLED: /* 0:0:0 */
				outl(1<<irq_nr, INTC0_CONFIG0_CLEAR);
				outl(1<<irq_nr, INTC0_CONFIG1_CLEAR);
				outl(1<<irq_nr, INTC0_CONFIG2_CLEAR);
				break;
			default: /* disable the interrupt */
				printk("unexpected int type %d (irq %d)\n", type, irq_nr);
				outl(1<<irq_nr, INTC0_CONFIG0_CLEAR);
				outl(1<<irq_nr, INTC0_CONFIG1_CLEAR);
				outl(1<<irq_nr, INTC0_CONFIG2_CLEAR);
				return;
		}
		if (int_req) /* assign to interrupt request 1 */
			outl(1<<irq_nr, INTC0_ASSIGN_REQ_CLEAR);
		else	     /* assign to interrupt request 0 */
			outl(1<<irq_nr, INTC0_ASSIGN_REQ_SET);
		outl(1<<irq_nr, INTC0_SOURCE_SET);
		outl(1<<irq_nr, INTC0_MASK_CLEAR);
	}
	sync();
}


static unsigned int startup_irq(unsigned int irq_nr)
{
	local_enable_irq(irq_nr);
	return 0; 
}


static void shutdown_irq(unsigned int irq_nr)
{
	local_disable_irq(irq_nr);
	return;
}


static inline void local_enable_irq(unsigned int irq_nr)
{
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		outl(1<<irq_nr, INTC1_MASK_SET);
	}
	else {
		outl(1<<irq_nr, INTC0_MASK_SET);
	}
	sync();
}


static inline void local_disable_irq(unsigned int irq_nr)
{
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		outl(1<<irq_nr, INTC1_MASK_CLEAR);
	}
	else {
		outl(1<<irq_nr, INTC0_MASK_CLEAR);
	}
	sync();
}


static inline void mask_and_ack_rise_edge_irq(unsigned int irq_nr)
{
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		outl(1<<irq_nr, INTC1_R_EDGE_DETECT_CLEAR);
		outl(1<<irq_nr, INTC1_MASK_CLEAR);
	}
	else {
		outl(1<<irq_nr, INTC0_R_EDGE_DETECT_CLEAR);
		outl(1<<irq_nr, INTC0_MASK_CLEAR);
	}
	sync();
}


static inline void mask_and_ack_fall_edge_irq(unsigned int irq_nr)
{
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		outl(1<<irq_nr, INTC1_F_EDGE_DETECT_CLEAR);
		outl(1<<irq_nr, INTC1_MASK_CLEAR);
	}
	else {
		outl(1<<irq_nr, INTC0_F_EDGE_DETECT_CLEAR);
		outl(1<<irq_nr, INTC0_MASK_CLEAR);
	}
}


static inline void mask_and_ack_level_irq(unsigned int irq_nr)
{
	local_disable_irq(irq_nr);
	sync();
	return;
}


static void end_irq(unsigned int irq_nr)
{
	if (!(irq_desc[irq_nr].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		local_enable_irq(irq_nr);
	else
		printk("warning: end_irq %d did not enable\n", irq_nr);
}


static struct hw_interrupt_type rise_edge_irq_type = {
	"Au1000 Rise Edge",
	startup_irq,
	shutdown_irq,
	local_enable_irq,
	local_disable_irq,
	mask_and_ack_rise_edge_irq,
	end_irq,
	NULL
};


static struct hw_interrupt_type fall_edge_irq_type = {
	"Au1000 Fall Edge",
	startup_irq,
	shutdown_irq,
	local_enable_irq,
	local_disable_irq,
	mask_and_ack_fall_edge_irq,
	end_irq,
	NULL
};


static struct hw_interrupt_type level_irq_type = {
	"Au1000 Level",
	startup_irq,
	shutdown_irq,
	local_enable_irq,
	local_disable_irq,
	mask_and_ack_level_irq,
	end_irq,
	NULL
};


void enable_cpu_timer(void)
{
	enable_cpu_irq_input(1<<MIPS_TIMER_IP); /* timer interrupt */
}


void __init init_IRQ(void)
{
	int i;
	unsigned long cp0_status;

	cp0_status = read_32bit_cp0_register(CP0_STATUS);
	memset(irq_desc, 0, sizeof(irq_desc));
	set_except_vector(0, au1000_IRQ);

	init_generic_irq();
	
	/* 
	 * Setup high priority interrupts on int_request0; low priority on
	 * int_request1
	 */
	for (i = 0; i <= NR_IRQS; i++) {
		switch (i) {
			case AU1000_MAC0_DMA_INT:
			case AU1000_MAC1_DMA_INT:
				setup_local_irq(i, INTC_INT_HIGH_LEVEL, 0);
				irq_desc[i].handler = &level_irq_type;
				break;
			default: /* active high, level interrupt */
				setup_local_irq(i, INTC_INT_HIGH_LEVEL, 1);
				irq_desc[i].handler = &level_irq_type;
				break;
		}
	}

	set_cp0_status(ALLINTS);
#ifdef CONFIG_REMOTE_DEBUG
	/* If local serial I/O used for debug port, enter kgdb at once */
	puts("Waiting for kgdb to connect...");
	set_debug_traps();
	breakpoint(); 
#endif
}


void mips_spurious_interrupt(struct pt_regs *regs)
{
	spurious_interrupts++;
}


void intc0_req0_irqdispatch(struct pt_regs *regs)
{
	int irq = 0, i;
	unsigned long int_request;

	int_request = inl(INTC0_REQ0_INT);

	if (!int_request) return;

	for (i=0; i<32; i++) {
		if ((int_request & 0x1)) {
			do_IRQ(irq, regs);
		}
		irq++;
		int_request >>= 1;
	}
}


void intc0_req1_irqdispatch(struct pt_regs *regs)
{
	int irq = 0, i;
	unsigned long int_request;

	int_request = inl(INTC0_REQ1_INT);

	if (!int_request) return;

	for (i=0; i<32; i++) {
		if ((int_request & 0x1)) {
			do_IRQ(irq, regs);
		}
		irq++;
		int_request >>= 1;
	}
}


void intc1_req0_irqdispatch(struct pt_regs *regs)
{
	int irq = 0, i;
	unsigned long int_request;

	int_request = inl(INTC1_REQ0_INT);

	if (!int_request) return;

	for (i=0; i<32; i++) {
		if ((int_request & 0x1)) {
			do_IRQ(irq, regs);
		}
		irq++;
		int_request >>= 1;
	}
}


void intc1_req1_irqdispatch(struct pt_regs *regs)
{
	int irq = 0, i;
	unsigned long int_request;

	int_request = inl(INTC1_REQ1_INT);

	if (!int_request) return;

	for (i=0; i<32; i++) {
		if ((int_request & 0x1)) {
			do_IRQ(irq, regs);
		}
		irq++;
		int_request >>= 1;
	}
}
