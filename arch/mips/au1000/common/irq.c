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
#include <linux/irq.h>
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

#if defined(CONFIG_MIPS_PB1000)
#include <asm/pb1000.h>
#elif defined(CONFIG_MIPS_PB1500)
#include <asm/pb1500.h>
#elif defined(CONFIG_MIPS_PB1100)
#include <asm/pb1100.h>
#elif defined(CONFIG_MIPS_DB1000)
#include <asm/db1x00.h>
#elif defined(CONFIG_MIPS_DB1100)
#include <asm/db1x00.h>
#elif defined(CONFIG_MIPS_DB1500)
#include <asm/db1x00.h>
#else
#error unsupported Alchemy board
#endif

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

#ifdef CONFIG_KGDB
extern void breakpoint(void);
#endif

extern asmlinkage void au1000_IRQ(void);
extern void set_debug_traps(void);
extern irq_cpustat_t irq_stat [NR_CPUS];
unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];

static void setup_local_irq(unsigned int irq, int type, int int_req);
static unsigned int startup_irq(unsigned int irq);
static void end_irq(unsigned int irq_nr);
static inline void mask_and_ack_level_irq(unsigned int irq_nr);
static inline void mask_and_ack_rise_edge_irq(unsigned int irq_nr);
static inline void mask_and_ack_fall_edge_irq(unsigned int irq_nr);
inline void local_enable_irq(unsigned int irq_nr);
inline void local_disable_irq(unsigned int irq_nr);

extern void __init init_generic_irq(void);

#ifdef CONFIG_PM
extern void counter0_irq(int irq, void *dev_id, struct pt_regs *regs);
#endif

static spinlock_t irq_lock = SPIN_LOCK_UNLOCKED;

static void setup_local_irq(unsigned int irq_nr, int type, int int_req)
{
	if (irq_nr > AU1000_MAX_INTR) return;
	/* Config2[n], Config1[n], Config0[n] */
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		switch (type) {
			case INTC_INT_RISE_EDGE: /* 0:0:1 */
				au_writel(1<<(irq_nr-32), IC1_CFG2CLR);
				au_writel(1<<(irq_nr-32), IC1_CFG1CLR);
				au_writel(1<<(irq_nr-32), IC1_CFG0SET);
				break;
			case INTC_INT_FALL_EDGE: /* 0:1:0 */
				au_writel(1<<(irq_nr-32), IC1_CFG2CLR);
				au_writel(1<<(irq_nr-32), IC1_CFG1SET);
				au_writel(1<<(irq_nr-32), IC1_CFG0CLR);
				break;
			case INTC_INT_HIGH_LEVEL: /* 1:0:1 */
				au_writel(1<<(irq_nr-32), IC1_CFG2SET);
				au_writel(1<<(irq_nr-32), IC1_CFG1CLR);
				au_writel(1<<(irq_nr-32), IC1_CFG0SET);
				break;
			case INTC_INT_LOW_LEVEL: /* 1:1:0 */
				au_writel(1<<(irq_nr-32), IC1_CFG2SET);
				au_writel(1<<(irq_nr-32), IC1_CFG1SET);
				au_writel(1<<(irq_nr-32), IC1_CFG0CLR);
				break;
			case INTC_INT_DISABLED: /* 0:0:0 */
				au_writel(1<<(irq_nr-32), IC1_CFG0CLR);
				au_writel(1<<(irq_nr-32), IC1_CFG1CLR);
				au_writel(1<<(irq_nr-32), IC1_CFG2CLR);
				break;
			default: /* disable the interrupt */
				printk("unexpected int type %d (irq %d)\n", type, irq_nr);
				au_writel(1<<(irq_nr-32), IC1_CFG0CLR);
				au_writel(1<<(irq_nr-32), IC1_CFG1CLR);
				au_writel(1<<(irq_nr-32), IC1_CFG2CLR);
				return;
		}
		if (int_req) /* assign to interrupt request 1 */
			au_writel(1<<(irq_nr-32), IC1_ASSIGNCLR);
		else	     /* assign to interrupt request 0 */
			au_writel(1<<(irq_nr-32), IC1_ASSIGNSET);
		au_writel(1<<(irq_nr-32), IC1_SRCSET);
		au_writel(1<<(irq_nr-32), IC1_MASKCLR);
		au_writel(1<<(irq_nr-32), IC1_WAKECLR);
	}
	else {
		switch (type) {
			case INTC_INT_RISE_EDGE: /* 0:0:1 */
				au_writel(1<<irq_nr, IC0_CFG2CLR);
				au_writel(1<<irq_nr, IC0_CFG1CLR);
				au_writel(1<<irq_nr, IC0_CFG0SET);
				break;
			case INTC_INT_FALL_EDGE: /* 0:1:0 */
				au_writel(1<<irq_nr, IC0_CFG2CLR);
				au_writel(1<<irq_nr, IC0_CFG1SET);
				au_writel(1<<irq_nr, IC0_CFG0CLR);
				break;
			case INTC_INT_HIGH_LEVEL: /* 1:0:1 */
				au_writel(1<<irq_nr, IC0_CFG2SET);
				au_writel(1<<irq_nr, IC0_CFG1CLR);
				au_writel(1<<irq_nr, IC0_CFG0SET);
				break;
			case INTC_INT_LOW_LEVEL: /* 1:1:0 */
				au_writel(1<<irq_nr, IC0_CFG2SET);
				au_writel(1<<irq_nr, IC0_CFG1SET);
				au_writel(1<<irq_nr, IC0_CFG0CLR);
				break;
			case INTC_INT_DISABLED: /* 0:0:0 */
				au_writel(1<<irq_nr, IC0_CFG0CLR);
				au_writel(1<<irq_nr, IC0_CFG1CLR);
				au_writel(1<<irq_nr, IC0_CFG2CLR);
				break;
			default: /* disable the interrupt */
				printk("unexpected int type %d (irq %d)\n", type, irq_nr);
				au_writel(1<<irq_nr, IC0_CFG0CLR);
				au_writel(1<<irq_nr, IC0_CFG1CLR);
				au_writel(1<<irq_nr, IC0_CFG2CLR);
				return;
		}
		if (int_req) /* assign to interrupt request 1 */
			au_writel(1<<irq_nr, IC0_ASSIGNCLR);
		else	     /* assign to interrupt request 0 */
			au_writel(1<<irq_nr, IC0_ASSIGNSET);
		au_writel(1<<irq_nr, IC0_SRCSET);
		au_writel(1<<irq_nr, IC0_MASKCLR);
		au_writel(1<<irq_nr, IC0_WAKECLR);
	}
	au_sync();
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


inline void local_enable_irq(unsigned int irq_nr)
{
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		au_writel(1<<(irq_nr-32), IC1_MASKSET);
		au_writel(1<<(irq_nr-32), IC1_WAKESET);
	}
	else {
		au_writel(1<<irq_nr, IC0_MASKSET);
		au_writel(1<<irq_nr, IC0_WAKESET);
	}
	au_sync();
}


inline void local_disable_irq(unsigned int irq_nr)
{
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		au_writel(1<<(irq_nr-32), IC1_MASKCLR);
		au_writel(1<<(irq_nr-32), IC1_WAKECLR);
	}
	else {
		au_writel(1<<irq_nr, IC0_MASKCLR);
		au_writel(1<<irq_nr, IC0_WAKECLR);
	}
	au_sync();
}


static inline void mask_and_ack_rise_edge_irq(unsigned int irq_nr)
{
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		au_writel(1<<(irq_nr-32), IC1_RISINGCLR);
		au_writel(1<<(irq_nr-32), IC1_MASKCLR);
	}
	else {
		au_writel(1<<irq_nr, IC0_RISINGCLR);
		au_writel(1<<irq_nr, IC0_MASKCLR);
	}
	au_sync();
}


static inline void mask_and_ack_fall_edge_irq(unsigned int irq_nr)
{
	if (irq_nr > AU1000_LAST_INTC0_INT) {
		au_writel(1<<(irq_nr-32), IC1_FALLINGCLR);
		au_writel(1<<(irq_nr-32), IC1_MASKCLR);
	}
	else {
		au_writel(1<<irq_nr, IC0_FALLINGCLR);
		au_writel(1<<irq_nr, IC0_MASKCLR);
	}
	au_sync();
}


static inline void mask_and_ack_level_irq(unsigned int irq_nr)
{

	local_disable_irq(irq_nr);
	au_sync();
#if defined(CONFIG_MIPS_PB1000)
	if (irq_nr == AU1000_GPIO_15) {
		au_writel(0x8000, PB1000_MDR); /* ack int */
		au_sync();
	}
#endif
	return;
}


static void end_irq(unsigned int irq_nr)
{
	if (!(irq_desc[irq_nr].status & (IRQ_DISABLED|IRQ_INPROGRESS))) {
		local_enable_irq(irq_nr);
	}
#if defined(CONFIG_MIPS_PB1000)
	if (irq_nr == AU1000_GPIO_15) {
		au_writel(0x4000, PB1000_MDR); /* enable int */
		au_sync();
	}
#endif
}

unsigned long save_local_and_disable(int controller)
{
	int i;
	unsigned long flags, mask;

	spin_lock_irqsave(&irq_lock, flags);
	if (controller) {
		mask = au_readl(IC1_MASKSET);
		for (i=32; i<64; i++) {
			local_disable_irq(i);
		}
	}
	else {
		mask = au_readl(IC0_MASKSET);
		for (i=0; i<32; i++) {
			local_disable_irq(i);
		}
	}
	spin_unlock_irqrestore(&irq_lock, flags);

	return mask;
}

void restore_local_and_enable(int controller, unsigned long mask)
{
	int i;
	unsigned long flags, new_mask;

	spin_lock_irqsave(&irq_lock, flags);
	for (i=0; i<32; i++) {
		if (mask & (1<<i)) {
			if (controller)
				local_enable_irq(i+32);
			else
				local_enable_irq(i);
		}
	}
	if (controller)
		new_mask = au_readl(IC1_MASKSET);
	else
		new_mask = au_readl(IC0_MASKSET);

	spin_unlock_irqrestore(&irq_lock, flags);
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

/*
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
*/

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

#ifdef CONFIG_PM
void startup_match20_interrupt(void)
{
	local_enable_irq(AU1000_TOY_MATCH2_INT);
}
#endif


void __init init_IRQ(void)
{
	int i;
	unsigned long cp0_status;

	cp0_status = read_c0_status();
	memset(irq_desc, 0, sizeof(irq_desc));
	set_except_vector(0, au1000_IRQ);

	init_generic_irq();

	for (i = 0; i <= AU1000_MAX_INTR; i++) {
		switch (i) {
			case AU1000_UART0_INT:
			case AU1000_UART3_INT:
#ifdef CONFIG_MIPS_PB1000
			case AU1000_UART1_INT:
			case AU1000_UART2_INT:

			case AU1000_SSI0_INT:
			case AU1000_SSI1_INT:
#endif

#ifdef CONFIG_MIPS_PB1100
			case AU1000_UART1_INT:

			case AU1000_SSI0_INT:
			case AU1000_SSI1_INT:
#endif
		        case AU1000_DMA_INT_BASE:
		        case AU1000_DMA_INT_BASE+1:
		        case AU1000_DMA_INT_BASE+2:
		        case AU1000_DMA_INT_BASE+3:
		        case AU1000_DMA_INT_BASE+4:
		        case AU1000_DMA_INT_BASE+5:
		        case AU1000_DMA_INT_BASE+6:
		        case AU1000_DMA_INT_BASE+7:

			case AU1000_IRDA_TX_INT:
			case AU1000_IRDA_RX_INT:

			case AU1000_MAC0_DMA_INT:
#if defined(CONFIG_MIPS_PB1000) || defined(CONFIG_MIPS_DB1000) || defined(CONFIG_MIPS_PB1500) || defined(CONFIG_MIPS_DB1500)
			case AU1000_MAC1_DMA_INT:
#endif
			case AU1500_GPIO_204:
				setup_local_irq(i, INTC_INT_HIGH_LEVEL, 0);
				irq_desc[i].handler = &level_irq_type;
				break;

#ifdef CONFIG_MIPS_PB1000
			case AU1000_GPIO_15:
#endif
		        case AU1000_USB_HOST_INT:
#if defined(CONFIG_MIPS_PB1500) || defined(CONFIG_MIPS_DB1500)
			case AU1000_PCI_INTA:
			case AU1000_PCI_INTB:
			case AU1000_PCI_INTC:
			case AU1000_PCI_INTD:
			case AU1500_GPIO_201:
			case AU1500_GPIO_202:
			case AU1500_GPIO_203:
			case AU1500_GPIO_205:
			case AU1500_GPIO_207:
#endif

#ifdef CONFIG_MIPS_PB1100
			case AU1000_GPIO_9: // PCMCIA Card Fully_Interted#
			case AU1000_GPIO_10: // PCMCIA_STSCHG#
			case AU1000_GPIO_11: // PCMCIA_IRQ#
			case AU1000_GPIO_13: // DC_IRQ#
			case AU1000_GPIO_23: // 2-wire SCL
#endif
#if defined(CONFIG_MIPS_DB1000) || defined(CONFIG_MIPS_DB1100) || defined(CONFIG_MIPS_DB1500)
			case AU1000_GPIO_0: // PCMCIA Card 0 Fully_Interted#
			case AU1000_GPIO_1: // PCMCIA Card 0 STSCHG#
			case AU1000_GPIO_2: // PCMCIA Card 0 IRQ#

			case AU1000_GPIO_3: // PCMCIA Card 1 Fully_Interted#
			case AU1000_GPIO_4: // PCMCIA Card 1 STSCHG#
			case AU1000_GPIO_5: // PCMCIA Card 1 IRQ#
#endif
				setup_local_irq(i, INTC_INT_LOW_LEVEL, 0);
				irq_desc[i].handler = &level_irq_type;
                                break;
			case AU1000_ACSYNC_INT:
			case AU1000_AC97C_INT:
			case AU1000_TOY_INT:
			case AU1000_TOY_MATCH0_INT:
			case AU1000_TOY_MATCH1_INT:
		        case AU1000_USB_DEV_SUS_INT:
		        case AU1000_USB_DEV_REQ_INT:
			case AU1000_RTC_INT:
			case AU1000_RTC_MATCH0_INT:
			case AU1000_RTC_MATCH1_INT:
			case AU1000_RTC_MATCH2_INT:
				setup_local_irq(i, INTC_INT_RISE_EDGE, 0);
				irq_desc[i].handler = &rise_edge_irq_type;
				break;

				 // Careful if you change match 2 request!
				 // The interrupt handler is called directly
				 // from the low level dispatch code.
			case AU1000_TOY_MATCH2_INT:
				 setup_local_irq(i, INTC_INT_RISE_EDGE, 1);
				 irq_desc[i].handler = &rise_edge_irq_type;
				  break;
			default: /* active high, level interrupt */
				setup_local_irq(i, INTC_INT_HIGH_LEVEL, 0);
				irq_desc[i].handler = &level_irq_type;
				break;
		}
	}

	set_c0_status(ALLINTS);
#ifdef CONFIG_KGDB
	/* If local serial I/O used for debug port, enter kgdb at once */
	puts("Waiting for kgdb to connect...");
	set_debug_traps();
	breakpoint();
#endif
}


/*
 * Interrupts are nested. Even if an interrupt handler is registered
 * as "fast", we might get another interrupt before we return from
 * intcX_reqX_irqdispatch().
 */

void intc0_req0_irqdispatch(struct pt_regs *regs)
{
	int irq = 0, i;
	static unsigned long intc0_req0 = 0;

	intc0_req0 |= au_readl(IC0_REQ0INT);

	if (!intc0_req0) return;

	/*
	 * Because of the tight timing of SETUP token to reply
	 * transactions, the USB devices-side packet complete
	 * interrupt needs the highest priority.
	 */
	if ((intc0_req0 & (1<<AU1000_USB_DEV_REQ_INT))) {
		intc0_req0 &= ~(1<<AU1000_USB_DEV_REQ_INT);
		do_IRQ(AU1000_USB_DEV_REQ_INT, regs);
		return;
	}

	for (i=0; i<32; i++) {
		if ((intc0_req0 & (1<<i))) {
			intc0_req0 &= ~(1<<i);
			do_IRQ(irq, regs);
			break;
		}
		irq++;
	}
}


void intc0_req1_irqdispatch(struct pt_regs *regs)
{
	int irq = 0, i;
	static unsigned long intc0_req1 = 0;

	intc0_req1 = au_readl(IC0_REQ1INT);

	if (!intc0_req1) return;

	for (i=0; i<32; i++) {
		if ((intc0_req1 & (1<<i))) {
			intc0_req1 &= ~(1<<i);
#ifdef CONFIG_PM
			if (i == AU1000_TOY_MATCH2_INT) {
				mask_and_ack_rise_edge_irq(irq);
				counter0_irq(irq, NULL, regs);
				local_enable_irq(irq);
			}
			else
#endif
			{
				do_IRQ(irq, regs);
			}
			break;
		}
		irq++;
	}
}


/*
 * Interrupt Controller 1:
 * interrupts 32 - 63
 */
void intc1_req0_irqdispatch(struct pt_regs *regs)
{
	int irq = 0, i;
	static unsigned long intc1_req0 = 0;

	intc1_req0 |= au_readl(IC1_REQ0INT);

	if (!intc1_req0) return;

#if defined(CONFIG_MIPS_PB1000) && defined(DEBUG_IRQ)
	au_writel(1, CPLD_AUX0); /* debug led 0 */
#endif
	for (i=0; i<32; i++) {
		if ((intc1_req0 & (1<<i))) {
			intc1_req0 &= ~(1<<i);
#if defined(CONFIG_MIPS_PB1000) && defined(DEBUG_IRQ)
			au_writel(2, CPLD_AUX0); /* turn on debug led 1  */
			do_IRQ(irq+32, regs);
			au_writel(0, CPLD_AUX0); /* turn off debug led 1 */
#else
			do_IRQ(irq+32, regs);
#endif
			break;
		}
		irq++;
	}
#if defined(CONFIG_MIPS_PB1000) && defined(DEBUG_IRQ)
	au_writel(0, CPLD_AUX0);
#endif
}


void intc1_req1_irqdispatch(struct pt_regs *regs)
{
	int irq = 0, i;
	static unsigned long intc1_req1 = 0;

	intc1_req1 |= au_readl(IC1_REQ1INT);

	if (!intc1_req1) return;

	for (i=0; i<32; i++) {
		if ((intc1_req1 & (1<<i))) {
			intc1_req1 &= ~(1<<i);
			do_IRQ(irq+32, regs);
			break;
		}
		irq++;
	}
}
