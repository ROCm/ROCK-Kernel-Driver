/*
 * Platform dependent support for SGI SN1
 *
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/sn/hcl.h>
#include <asm/sn/types.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pciio_private.h>
#ifdef ajmtestintr
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#endif /* ajmtestintr */
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/io.h>
#include <asm/sn/intr.h>
#include <asm/sn/addrs.h>
#include <asm/sn/driver.h>
#include <asm/sn/arch.h>
#include <asm/sn/nodepda.h>

int irq_to_bit_pos(int irq);

static unsigned int
sn_startup_irq(unsigned int irq)
{
        return(0);
}

static void
sn_shutdown_irq(unsigned int irq)
{
}

static void
sn_disable_irq(unsigned int irq)
{
}

static void
sn_enable_irq(unsigned int irq)
{
}

static void
sn_ack_irq(unsigned int irq)
{
#ifdef CONFIG_IA64_SGI_SN1
	int bit = -1;
	unsigned long long intpend_val;
	int subnode;
#endif
#ifdef CONFIG_IA64_SGI_SN2
	unsigned long event_occurred, mask = 0;
#endif
	int nasid;

	irq = irq & 0xff;
	nasid = smp_physical_node_id();
#ifdef CONFIG_IA64_SGI_SN1
	subnode = cpuid_to_subnode(smp_processor_id());
	if (irq == SGI_UART_IRQ) {
		intpend_val = REMOTE_HUB_PI_L(nasid, subnode, PI_INT_PEND0);
		if (intpend_val & (1L<<GFX_INTR_A) ) {
			bit = GFX_INTR_A;
			REMOTE_HUB_PI_CLR_INTR(nasid, subnode, bit);
		}
		if ( intpend_val & (1L<<GFX_INTR_B) ) {
			bit = GFX_INTR_B;
			REMOTE_HUB_PI_CLR_INTR(nasid, subnode, bit);
		}
		if (intpend_val & (1L<<PG_MIG_INTR) ) {
			bit = PG_MIG_INTR;
			REMOTE_HUB_PI_CLR_INTR(nasid, subnode, bit);
		}
		if (intpend_val & (1L<<CC_PEND_A)) {
			bit = CC_PEND_A;
			REMOTE_HUB_PI_CLR_INTR(nasid, subnode, bit);
		}
		if (intpend_val & (1L<<CC_PEND_B)) {
			bit = CC_PEND_B;
			REMOTE_HUB_PI_CLR_INTR(nasid, subnode, bit);
		}
		return;
	}
	bit = irq_to_bit_pos(irq);
	REMOTE_HUB_PI_CLR_INTR(nasid, subnode, bit);
#endif

#ifdef CONFIG_IA64_SGI_SN2
	event_occurred = HUB_L( (unsigned long *)GLOBAL_MMR_ADDR(nasid,SH_EVENT_OCCURRED) );
	if (event_occurred & SH_EVENT_OCCURRED_UART_INT_MASK) {
		mask |= (1 << SH_EVENT_OCCURRED_UART_INT_SHFT);
	}
	if (event_occurred & SH_EVENT_OCCURRED_IPI_INT_MASK) {
		mask |= (1 << SH_EVENT_OCCURRED_IPI_INT_SHFT);
	}
	if (event_occurred & SH_EVENT_OCCURRED_II_INT0_MASK) {
		mask |= (1 << SH_EVENT_OCCURRED_II_INT0_SHFT);
	}
	if (event_occurred & SH_EVENT_OCCURRED_II_INT1_MASK) {
		mask |= (1 << SH_EVENT_OCCURRED_II_INT1_SHFT);
	}
	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_EVENT_OCCURRED_ALIAS), mask );
#endif
}

static void
sn_end_irq(unsigned int irq)
{
#ifdef CONFIG_IA64_SGI_SN1
	unsigned long long intpend_val, mask = 0x70L;
	int subnode;
#endif
	int nasid;
#ifdef CONFIG_IA64_SGI_SN2
	unsigned long event_occurred;
#endif

	irq = irq & 0xff;
#ifdef CONFIG_IA64_SGI_SN1
	if (irq == SGI_UART_IRQ) {
		nasid = smp_physical_node_id();
		subnode = cpuid_to_subnode(smp_processor_id());
		intpend_val = REMOTE_HUB_PI_L(nasid, subnode, PI_INT_PEND0);
		if (intpend_val & mask) {
			platform_send_ipi(smp_processor_id(), SGI_UART_IRQ, IA64_IPI_DM_INT, 0);
		}
	}
#endif
#ifdef CONFIG_IA64_SGI_SN2
	if (irq == SGI_UART_VECTOR) {
		nasid = smp_physical_node_id();
		event_occurred = HUB_L( (unsigned long *)GLOBAL_MMR_ADDR(nasid,SH_EVENT_OCCURRED) );
		// If the UART bit is set here, we may have received an interrupt from the
		// UART that the driver missed.  To make sure, we IPI ourselves to force us
		// to look again.
		if (event_occurred & SH_EVENT_OCCURRED_UART_INT_MASK) {
				platform_send_ipi(smp_processor_id(), SGI_UART_VECTOR, IA64_IPI_DM_INT, 0);
		}
	}
#endif

}

static void
sn_set_affinity_irq(unsigned int irq, unsigned long mask)
{
}


struct hw_interrupt_type irq_type_iosapic_level = {
	"SN hub",
	sn_startup_irq,
	sn_shutdown_irq,
	sn_enable_irq,
	sn_disable_irq,
	sn_ack_irq, 
	sn_end_irq,
	sn_set_affinity_irq
};


#define irq_type_sn irq_type_iosapic_level
struct irq_desc *_sn_irq_desc[NR_CPUS];

struct irq_desc *
sn_irq_desc(unsigned int irq) {
	int cpu = irq >> 8;

	irq = irq & 0xff;

	return(_sn_irq_desc[cpu] + irq);
}

u8
sn_irq_to_vector(u8 irq) {
	return(irq & 0xff);
}

int gsi_to_vector(u32 irq) {
	return irq & 0xff;
}

int gsi_to_irq(u32 irq) {
	return irq & 0xff;
}

unsigned int
sn_local_vector_to_irq(u8 vector) {
	return (CPU_VECTOR_TO_IRQ(smp_processor_id(), vector));
}

void *kmalloc(size_t, int);

void
sn_irq_init (void)
{
	int i;
	irq_desc_t *base_desc = _irq_desc;

	for (i=IA64_FIRST_DEVICE_VECTOR; i<NR_IVECS; i++) {
		if (base_desc[i].handler == &no_irq_type) {
			base_desc[i].handler = &irq_type_sn;
		}
	}
}

void
sn_init_irq_desc(void) {
	int i;
	irq_desc_t *base_desc = _irq_desc, *p;

	for (i=0; i < NR_CPUS; i++) {
		p =  page_address(alloc_pages_node(local_nodeid, GFP_KERNEL,
			get_order(sizeof(struct irq_desc) * NR_IVECS) ) );
		ASSERT(p);
		memcpy(p, base_desc, sizeof(struct irq_desc) * NR_IVECS);
		_sn_irq_desc[i] = p;
	}
}


int
bit_pos_to_irq(int bit) {
#define BIT_TO_IRQ 64
	if (bit > 118) bit = 118;

#ifdef CONFIG_IA64_SGI_SN1
	if (bit >= GFX_INTR_A && bit <= CC_PEND_B) {
		return SGI_UART_IRQ;
	}
#endif

        return bit + BIT_TO_IRQ;
}

int
irq_to_bit_pos(int irq) {
#define IRQ_TO_BIT 64
	int bit = irq - IRQ_TO_BIT;

        return bit;
}

#ifdef ajmtestintr

#include <linux/timer.h>
struct timer_list intr_test_timer = TIMER_INITIALIZER(NULL, 0, 0);
int intr_test_icount[NR_IRQS];
struct intr_test_reg_struct {
	pcibr_soft_t pcibr_soft;
	int slot;
};
struct intr_test_reg_struct intr_test_registered[NR_IRQS];

void
intr_test_handle_timer(unsigned long data) {
	int i;
	bridge_t	*bridge;

	for (i=0;i<NR_IRQS;i++) {
		if (intr_test_registered[i].pcibr_soft) {
			pcibr_soft_t pcibr_soft = intr_test_registered[i].pcibr_soft;
			xtalk_intr_t intr = pcibr_soft->bs_intr[intr_test_registered[i].slot].bsi_xtalk_intr;
			/* send interrupt */
			bridge = pcibr_soft->bs_base;
			bridge->b_force_always[intr_test_registered[i].slot].intr = 1;
		}
	}
	del_timer(&intr_test_timer);
	intr_test_timer.expires = jiffies + HZ/100;
	add_timer(&intr_test_timer);
}

void
intr_test_set_timer(void) {
	intr_test_timer.expires = jiffies + HZ/100;
	intr_test_timer.function = intr_test_handle_timer;
	add_timer(&intr_test_timer);
}

void
intr_test_register_irq(int irq, pcibr_soft_t pcibr_soft, int slot) {
	irq = irq & 0xff;
	intr_test_registered[irq].pcibr_soft = pcibr_soft;
	intr_test_registered[irq].slot = slot;
}

void
intr_test_handle_intr(int irq, void *junk, struct pt_regs *morejunk) {
	intr_test_icount[irq]++;
	printk("RECEIVED %d INTERRUPTS ON IRQ %d\n",intr_test_icount[irq], irq);
}
#endif /* ajmtestintr */
