/*
 *
 * BRIEF MODULE DESCRIPTION
 *	ITE 8172G interrupt/setup routines.
 *
 * Copyright 2000,2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * Part of this file was derived from Carsten Langgaard's 
 * arch/mips/mips-boards/atlas/atlas_int.c.
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
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
#include <linux/serial_reg.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_int.h>
#include <asm/it8172/it8172_dbg.h>

#undef DEBUG_IRQ
#ifdef DEBUG_IRQ
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk("%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#ifdef CONFIG_REMOTE_DEBUG
extern void breakpoint(void);
#endif

/* revisit */
#define EXT_IRQ0_TO_IP 2 /* IP 2 */
#define EXT_IRQ5_TO_IP 7 /* IP 7 */

extern void set_debug_traps(void);
extern void mips_timer_interrupt(int irq, struct pt_regs *regs);
extern asmlinkage void it8172_IRQ(void);
unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];
unsigned long spurious_count = 0;
irq_desc_t irq_desc[NR_IRQS];
irq_desc_t *irq_desc_base=&irq_desc[0];
void disable_it8172_irq(unsigned int irq_nr);
void enable_it8172_irq(unsigned int irq_nr);

struct it8172_intc_regs volatile *it8172_hw0_icregs
	= (struct it8172_intc_regs volatile *)(KSEG1ADDR(IT8172_PCI_IO_BASE + IT_INTC_BASE));

/* Function for careful CP0 interrupt mask access */
static inline void modify_cp0_intmask(unsigned clr_mask, unsigned set_mask)
{
        unsigned long status = read_32bit_cp0_register(CP0_STATUS);
        status &= ~((clr_mask & 0xFF) << 8);
        status |=   (set_mask & 0xFF) << 8;
        write_32bit_cp0_register(CP0_STATUS, status);
}

static inline void mask_irq(unsigned int irq_nr)
{
        modify_cp0_intmask(irq_nr, 0);
}

static inline void unmask_irq(unsigned int irq_nr)
{
        modify_cp0_intmask(0, irq_nr);
}

void disable_irq(unsigned int irq_nr)
{
        unsigned long flags;

        save_and_cli(flags);
	disable_it8172_irq(irq_nr);
        restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;

        save_and_cli(flags);
	enable_it8172_irq(irq_nr);
        restore_flags(flags);
}


void disable_it8172_irq(unsigned int irq_nr)
{
	DPRINTK("disable_it8172_irq %d\n", irq_nr);

	if ( (irq_nr >= IT8172_LPC_IRQ_BASE) && (irq_nr <= IT8172_SERIRQ_15)) {
		/* LPC interrupt */
		DPRINTK("disable, before lpc_mask  %x\n", it8172_hw0_icregs->lpc_mask);
		it8172_hw0_icregs->lpc_mask |= (1 << (irq_nr - IT8172_LPC_IRQ_BASE));
		DPRINTK("disable, after lpc_mask  %x\n", it8172_hw0_icregs->lpc_mask);
	}
	else if ( (irq_nr >= IT8172_LB_IRQ_BASE) && (irq_nr <= IT8172_IOCHK_IRQ)) {
		/* Local Bus interrupt */
		DPRINTK("before lb_mask  %x\n", it8172_hw0_icregs->lb_mask);
		it8172_hw0_icregs->lb_mask |= (1 << (irq_nr - IT8172_LB_IRQ_BASE));
		DPRINTK("after lb_mask  %x\n", it8172_hw0_icregs->lb_mask);
	}
	else if ( (irq_nr >= IT8172_PCI_DEV_IRQ_BASE) && (irq_nr <= IT8172_DMA_IRQ)) {
		/* PCI and other interrupts */
		DPRINTK("before pci_mask  %x\n", it8172_hw0_icregs->pci_mask);
		it8172_hw0_icregs->pci_mask |= (1 << (irq_nr - IT8172_PCI_DEV_IRQ_BASE));
		DPRINTK("after pci_mask  %x\n", it8172_hw0_icregs->pci_mask);
	}
	else if ( (irq_nr >= IT8172_NMI_IRQ_BASE) && (irq_nr <= IT8172_POWER_NMI_IRQ)) {
		/* NMI interrupts */
		DPRINTK("before nmi_mask  %x\n", it8172_hw0_icregs->nmi_mask);
		it8172_hw0_icregs->nmi_mask |= (1 << (irq_nr - IT8172_NMI_IRQ_BASE));
		DPRINTK("after nmi_mask  %x\n", it8172_hw0_icregs->nmi_mask);
	}
	else {
		panic("disable_it8172_irq: bad irq %d\n", irq_nr);
	}
}


void enable_it8172_irq(unsigned int irq_nr)
{
	DPRINTK("enable_it8172_irq %d\n", irq_nr);
	if ( (irq_nr >= IT8172_LPC_IRQ_BASE) && (irq_nr <= IT8172_SERIRQ_15)) {
		/* LPC interrupt */
		DPRINTK("enable, before lpc_mask  %x\n", it8172_hw0_icregs->lpc_mask);
		it8172_hw0_icregs->lpc_mask &= ~(1 << (irq_nr - IT8172_LPC_IRQ_BASE));
		DPRINTK("enable, after lpc_mask  %x\n", it8172_hw0_icregs->lpc_mask);
	}
	else if ( (irq_nr >= IT8172_LB_IRQ_BASE) && (irq_nr <= IT8172_IOCHK_IRQ)) {
		/* Local Bus interrupt */
		DPRINTK("before lb_mask  %x\n", it8172_hw0_icregs->lb_mask);
		it8172_hw0_icregs->lb_mask &= ~(1 << (irq_nr - IT8172_LB_IRQ_BASE));
		DPRINTK("after lb_mask  %x\n", it8172_hw0_icregs->lb_mask);
	}
	else if ( (irq_nr >= IT8172_PCI_DEV_IRQ_BASE) && (irq_nr <= IT8172_DMA_IRQ)) {
		/* PCI and other interrupts */
		DPRINTK("before pci_mask  %x\n", it8172_hw0_icregs->pci_mask);
		it8172_hw0_icregs->pci_mask &= ~(1 << (irq_nr - IT8172_PCI_DEV_IRQ_BASE));
		DPRINTK("after pci_mask  %x\n", it8172_hw0_icregs->pci_mask);
	}
	else if ( (irq_nr >= IT8172_NMI_IRQ_BASE) && (irq_nr <= IT8172_POWER_NMI_IRQ)) {
		/* NMI interrupts */
		DPRINTK("before nmi_mask  %x\n", it8172_hw0_icregs->nmi_mask);
		it8172_hw0_icregs->nmi_mask &= ~(1 << (irq_nr - IT8172_NMI_IRQ_BASE));
		DPRINTK("after nmi_mask  %x\n", it8172_hw0_icregs->nmi_mask);
	}
	else {
		panic("enable_it8172_irq: bad irq %d\n", irq_nr);
	}
}

static unsigned int startup_ite_irq(unsigned int irq)
{
	enable_it8172_irq(irq);
	return 0; 
}

#define shutdown_ite_irq	disable_it8172_irq
#define mask_and_ack_ite_irq    disable_it8172_irq

static void end_ite_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_it8172_irq(irq);
}

static struct hw_interrupt_type it8172_irq_type = {
	"ITE8172",
	startup_ite_irq,
	shutdown_ite_irq,
	enable_it8172_irq,
	disable_it8172_irq,
	mask_and_ack_ite_irq,
	end_ite_irq,
	NULL
};


int get_irq_list(char *buf)
{
        int i, len = 0, j;
        struct irqaction * action;

        len += sprintf(buf+len, "           ");
        for (j=0; j<smp_num_cpus; j++)
                len += sprintf(buf+len, "CPU%d       ",j);
        *(char *)(buf+len++) = '\n';

        for (i = 0 ; i < NR_IRQS ; i++) {
                action = irq_desc[i].action;
                if ( !action || !action->handler )
                        continue;
                len += sprintf(buf+len, "%3d: ", i);		
                len += sprintf(buf+len, "%10u ", kstat_irqs(i));
                if ( irq_desc[i].handler )		
                        len += sprintf(buf+len, " %s ", irq_desc[i].handler->typename );
                else
                        len += sprintf(buf+len, "  None      ");
                len += sprintf(buf+len, "    %s",action->name);
                for (action=action->next; action; action = action->next) {
                        len += sprintf(buf+len, ", %s", action->name);
                }
                len += sprintf(buf+len, "\n");
        }
        len += sprintf(buf+len, "BAD: %10lu\n", spurious_count);
        return len;
}

asmlinkage void do_IRQ(int irq, struct pt_regs *regs)
{
	struct irqaction *action;
	int cpu;

	cpu = smp_processor_id();
	irq_enter(cpu, irq);

	kstat.irqs[cpu][irq]++;
#if 0
	if (irq_desc[irq].handler && irq_desc[irq].handler->ack) {
	//	printk("invoking ack handler\n");
		irq_desc[irq].handler->ack(irq);
	}
#endif

	action = irq_desc[irq].action;

	if (action && action->handler)
	{
		//mask_irq(1<<irq);
		//printk("action->handler %x\n", action->handler);
		disable_it8172_irq(irq);
		//if (!(action->flags & SA_INTERRUPT)) __sti(); /* reenable ints */
		do { 
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while ( action );
		//__cli(); /* disable ints */
		if (irq_desc[irq].handler)
		{
		}
		//unmask_irq(1<<irq);
		enable_it8172_irq(irq);
	}
	else
	{
		spurious_count++;
		printk("Unhandled interrupt %d, cause %x, disabled\n", 
				(unsigned)irq, (unsigned)regs->cp0_cause);
		disable_it8172_irq(irq);
	}
	irq_exit(cpu, irq);
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{
        struct irqaction *old, **p, *action;
        unsigned long flags;

        /*
         * IP0 and IP1 are software interrupts. IP7 is typically the timer interrupt.
	 *
	 * The ITE QED-4N-S01B board has one single interrupt line going from
	 * the system controller to the CPU. It's connected to the CPU external
	 * irq pin 1, which is IP2.  The interrupt numbers are listed in it8172_int.h;
	 * the ISA interrupts are numbered from 0 to 15, and the rest go from
	 * there.  
         */

	//printk("request_irq: %d handler %x\n", irq, handler);
        if (irq >= NR_IRQS) 
                return -EINVAL;

        if (!handler)
        {
                /* Free */
                for (p = &irq_desc[irq].action; (action = *p) != NULL; p = &action->next)
                {
                        /* Found it - now free it */
                        save_flags(flags);
                        cli();
                        *p = action->next;
			disable_it8172_irq(irq);
                        restore_flags(flags);
                        kfree(action);
                        return 0;
                }
                return -ENOENT;
        }
        
        action = (struct irqaction *)
                kmalloc(sizeof(struct irqaction), GFP_KERNEL);
        if (!action)
                return -ENOMEM;
        memset(action, 0, sizeof(struct irqaction));
        
        save_flags(flags);
        cli();
        
        action->handler = handler;
        action->flags = irqflags;					
        action->mask = 0;
        action->name = devname;
        action->dev_id = dev_id;
        action->next = NULL;

        p = &irq_desc[irq].action;
        
        if ((old = *p) != NULL) {
                /* Can't share interrupts unless both agree to */
                if (!(old->flags & action->flags & SA_SHIRQ))
                        return -EBUSY;
                /* add new interrupt at end of irq queue */
                do {
                        p = &old->next;
                        old = *p;
                } while (old);
        }
        *p = action;
	enable_it8172_irq(irq);
        restore_flags(flags);	
#if 0
	printk("request_irq: status %x cause %x\n", 
			read_32bit_cp0_register(CP0_STATUS), read_32bit_cp0_register(CP0_CAUSE));
#endif
        return 0;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
        request_irq(irq, NULL, 0, NULL, dev_id);
}

void enable_cpu_timer(void)
{
        unsigned long flags;

        save_and_cli(flags);
	unmask_irq(1<<EXT_IRQ5_TO_IP); /* timer interrupt */
        restore_flags(flags);
}

unsigned long probe_irq_on (void)
{
        return 0;
}

int probe_irq_off (unsigned long irqs)
{
        return 0;
}


void __init init_IRQ(void)
{
	int i;
        unsigned long flags;


        memset(irq_desc, 0, sizeof(irq_desc));
        set_except_vector(0, it8172_IRQ);

	/* mask all interrupts */
	it8172_hw0_icregs->lb_mask  = 0xffff;
	it8172_hw0_icregs->lpc_mask = 0xffff;
	it8172_hw0_icregs->pci_mask = 0xffff;
	it8172_hw0_icregs->nmi_mask = 0xffff;

	/* make all interrupts level triggered */
	it8172_hw0_icregs->lb_trigger  = 0;
	it8172_hw0_icregs->lpc_trigger = 0;
	it8172_hw0_icregs->pci_trigger = 0;
	it8172_hw0_icregs->nmi_trigger = 0;

	/* active level setting */
	/* uart, keyboard, and mouse are active high */
	it8172_hw0_icregs->lpc_level = (0x10 | 0x2 | 0x1000);
	it8172_hw0_icregs->lb_level |= 0x20;

	/* keyboard and mouse are edge triggered */
	it8172_hw0_icregs->lpc_trigger |= (0x2 | 0x1000); 


#if 0
	// Enable this piece of code to make internal USB interrupt
	// edge triggered.
	it8172_hw0_icregs->pci_trigger |= 
		(1 << (IT8172_USB_IRQ - IT8172_PCI_DEV_IRQ_BASE));
	it8172_hw0_icregs->pci_level &= 
		~(1 << (IT8172_USB_IRQ - IT8172_PCI_DEV_IRQ_BASE));
#endif

	for (i = 0; i <= IT8172_INT_END; i++) {
		irq_desc[i].status	= IRQ_DISABLED;
		irq_desc[i].action	= 0;
		irq_desc[i].depth	= 1;
		irq_desc[i].handler	= &it8172_irq_type;
	}

	/*
	 * Enable external int line 2
	 * All ITE interrupts are masked for now.
	 */
        save_and_cli(flags);
	unmask_irq(1<<EXT_IRQ0_TO_IP);
        restore_flags(flags);

#ifdef CONFIG_REMOTE_DEBUG
	/* If local serial I/O used for debug port, enter kgdb at once */
	puts("Waiting for kgdb to connect...");
	set_debug_traps();
	breakpoint(); 
#endif
}

void mips_spurious_interrupt(struct pt_regs *regs)
{
#if 1
	return;
#else
	unsigned long status, cause;

	printk("got spurious interrupt\n");
	status = read_32bit_cp0_register(CP0_STATUS);
	cause = read_32bit_cp0_register(CP0_CAUSE);
	printk("status %x cause %x\n", status, cause);
	printk("epc %x badvaddr %x \n", regs->cp0_epc, regs->cp0_badvaddr);
//	while(1);
#endif
}

void it8172_hw0_irqdispatch(struct pt_regs *regs)
{
	int irq;
	unsigned short intstatus, status;

	intstatus = it8172_hw0_icregs->intstatus;
	if (intstatus & 0x8) {
		panic("Got NMI interrupt\n");
	}
	else if (intstatus & 0x4) {
		/* PCI interrupt */
		irq = 0;
		status = it8172_hw0_icregs->pci_req;
		while (!(status & 0x1)) {
			irq++;
			status >>= 1;
		}
		irq += IT8172_PCI_DEV_IRQ_BASE;
		//printk("pci int %d\n", irq);
	}
	else if (intstatus & 0x1) {
		/* Local Bus interrupt */
		irq = 0;
		status = it8172_hw0_icregs->lb_req;
		while (!(status & 0x1)) {
			irq++;
			status >>= 1;
		}
		irq += IT8172_LB_IRQ_BASE;
		//printk("lb int %d\n", irq);
	}
	else if (intstatus & 0x2) {
		/* LPC interrupt */
		/* Since some lpc interrupts are edge triggered,
		 * we could lose an interrupt this way because
		 * we acknowledge all ints at onces. Revisit.
		 */
		status = it8172_hw0_icregs->lpc_req;
		it8172_hw0_icregs->lpc_req = 0; /* acknowledge ints */
		irq = 0;
		while (!(status & 0x1)) {
			irq++;
			status >>= 1;
		}
		irq += IT8172_LPC_IRQ_BASE;
		//printk("LPC int %d\n", irq);
	}
	else {
		return;
	}
	do_IRQ(irq, regs);
}

void show_pending_irqs(void)
{
	fputs("intstatus:  ");
	put32(it8172_hw0_icregs->intstatus);
	puts("");

	fputs("pci_req:  ");
	put32(it8172_hw0_icregs->pci_req);
	puts("");

	fputs("lb_req:  ");
	put32(it8172_hw0_icregs->lb_req);
	puts("");

	fputs("lpc_req:  ");
	put32(it8172_hw0_icregs->lpc_req);
	puts("");
}
