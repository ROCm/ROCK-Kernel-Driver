/*
 * Platform dependent support for SGI SN
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/cpumask.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sn/sgi.h>
#include <asm/sn/hcl.h>
#include <asm/sn/types.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pciio_private.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/io.h>
#include <asm/sn/intr.h>
#include <asm/sn/addrs.h>
#include <asm/sn/driver.h>
#include <asm/sn/arch.h>
#include <asm/sn/pda.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/sn/sn2/shub_mmr.h>

static void force_interrupt(int irq);
extern void pcibr_force_interrupt(pcibr_intr_t intr);
extern int sn_force_interrupt_flag;
struct irq_desc * sn_irq_desc(unsigned int irq);
extern cpumask_t    __cacheline_aligned pending_irq_cpumask[NR_IRQS];

struct sn_intr_list_t {
	struct sn_intr_list_t *next;
	pcibr_intr_t intr;
};

static struct sn_intr_list_t *sn_intr_list[NR_IRQS];


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

static inline void sn_move_irq(int irq)
{
	/* note - we hold desc->lock */
	cpumask_t tmp;
	irq_desc_t *desc = irq_descp(irq);

	if (!cpus_empty(pending_irq_cpumask[irq])) {
		cpus_and(tmp, pending_irq_cpumask[irq], cpu_online_map);
		if (unlikely(!cpus_empty(tmp))) {
			desc->handler->set_affinity(irq, pending_irq_cpumask[irq]);
		}
		cpus_clear(pending_irq_cpumask[irq]);
	}
}

static void
sn_ack_irq(unsigned int irq)
{
	unsigned long event_occurred, mask = 0;
	int nasid;

	irq = irq & 0xff;
	nasid = smp_physical_node_id();
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
	__set_bit(irq, (volatile void *)pda->sn_in_service_ivecs);
	sn_move_irq(irq);
}

static void
sn_end_irq(unsigned int irq)
{
	int nasid;
	int ivec;
	unsigned long event_occurred;
	irq_desc_t *desc = sn_irq_desc(irq);
	unsigned int status = desc->status;

	ivec = irq & 0xff;
	if (ivec == SGI_UART_VECTOR) {
		nasid = smp_physical_node_id();
		event_occurred = HUB_L( (unsigned long *)GLOBAL_MMR_ADDR(nasid,SH_EVENT_OCCURRED) );
		// If the UART bit is set here, we may have received an interrupt from the
		// UART that the driver missed.  To make sure, we IPI ourselves to force us
		// to look again.
		if (event_occurred & SH_EVENT_OCCURRED_UART_INT_MASK) {
				platform_send_ipi(smp_processor_id(), SGI_UART_VECTOR, IA64_IPI_DM_INT, 0);
		}
	}
	__clear_bit(ivec, (volatile void *)pda->sn_in_service_ivecs);
	if (sn_force_interrupt_flag)
		if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS)))
			force_interrupt(irq);
}

static void
sn_set_affinity_irq(unsigned int irq, cpumask_t mask)
{
#ifdef CONFIG_SMP
	int redir = 0;
	int cpu;
	struct sn_intr_list_t *p = sn_intr_list[irq];
	pcibr_intr_t intr;
	extern void sn_shub_redirect_intr(pcibr_intr_t intr, unsigned long cpu);
	extern void sn_tio_redirect_intr(pcibr_intr_t intr, unsigned long cpu);

	if (p == NULL)
		return; 
        
	intr = p->intr;

	if (intr == NULL)
		return; 

	cpu = first_cpu(mask);
	sn_shub_redirect_intr(intr, cpu);
	irq = irq & 0xff;  /* strip off redirect bit, if someone stuck it on. */
	(void) set_irq_affinity_info(irq, cpu_physical_id(intr->bi_cpu), redir);
#endif /* CONFIG_SMP */
}


struct hw_interrupt_type irq_type_sn = {
	"SN hub",
	sn_startup_irq,
	sn_shutdown_irq,
	sn_enable_irq,
	sn_disable_irq,
	sn_ack_irq, 
	sn_end_irq,
	sn_set_affinity_irq
};


struct irq_desc *
sn_irq_desc(unsigned int irq)
{

	irq = SN_IVEC_FROM_IRQ(irq);

	return(_irq_desc + irq);
}

u8
sn_irq_to_vector(unsigned int irq)
{
	return(irq);
}

unsigned int
sn_local_vector_to_irq(u8 vector)
{
	return (CPU_VECTOR_TO_IRQ(smp_processor_id(), vector));
}

void
sn_irq_init (void)
{
	int i;
	irq_desc_t *base_desc = _irq_desc;

	for (i=0; i<NR_IRQS; i++) {
		if (base_desc[i].handler == &no_irq_type) {
			base_desc[i].handler = &irq_type_sn;
		}
	}
}

void
register_pcibr_intr(int irq, pcibr_intr_t intr)
{
	struct sn_intr_list_t *p = kmalloc(sizeof(struct sn_intr_list_t), GFP_KERNEL);
	struct sn_intr_list_t *list;
	int cpu = intr->bi_cpu;

	if (pdacpu(cpu)->sn_last_irq < irq) {
		pdacpu(cpu)->sn_last_irq = irq;
	}
	if (pdacpu(cpu)->sn_first_irq == 0 || pdacpu(cpu)->sn_first_irq > irq) pdacpu(cpu)->sn_first_irq = irq;
	if (!p) panic("Could not allocate memory for sn_intr_list_t\n");
	if ((list = sn_intr_list[irq])) {
		while (list->next) list = list->next;
		list->next = p;
		p->next = NULL;
		p->intr = intr;
	} else {
		sn_intr_list[irq] = p;
		p->next = NULL;
		p->intr = intr;
	}
}

void
unregister_pcibr_intr(int irq, pcibr_intr_t intr)
{

	struct sn_intr_list_t **prev, *curr;
	int cpu = intr->bi_cpu;
	int i;

	if (sn_intr_list[irq] == NULL)
		return;

	prev = &sn_intr_list[irq];
	curr = sn_intr_list[irq];
	while (curr) {
		if (curr->intr == intr)	 {
			*prev = curr->next;
			break;
		}
		prev = &curr->next;
		curr = curr->next;
	}

	if (curr)
		kfree(curr);

	if (!sn_intr_list[irq]) {
		if (pdacpu(cpu)->sn_last_irq == irq) {
			for (i = pdacpu(cpu)->sn_last_irq - 1; i; i--)
				if (sn_intr_list[i])
					break;
			pdacpu(cpu)->sn_last_irq = i;
		}

		if (pdacpu(cpu)->sn_first_irq == irq) {
			pdacpu(cpu)->sn_first_irq = 0;
			for (i = pdacpu(cpu)->sn_first_irq + 1; i < NR_IRQS; i++)
				if (sn_intr_list[i])
					pdacpu(cpu)->sn_first_irq = i;
		}
	}

}

void
force_polled_int(void)
{
	int i;
	struct sn_intr_list_t *p;

	for (i=0; i<NR_IRQS;i++) {
		p = sn_intr_list[i];
		while (p) {
			if (p->intr){
				pcibr_force_interrupt(p->intr);
			}
			p = p->next;
		}
	}
}

static void
force_interrupt(int irq)
{
	struct sn_intr_list_t *p = sn_intr_list[irq];

	while (p) {
		if (p->intr) {
			pcibr_force_interrupt(p->intr);
		}
		p = p->next;
	}
}

/*
Check for lost interrupts.  If the PIC int_status reg. says that
an interrupt has been sent, but not handled, and the interrupt
is not pending in either the cpu irr regs or in the soft irr regs,
and the interrupt is not in service, then the interrupt may have
been lost.  Force an interrupt on that pin.  It is possible that
the interrupt is in flight, so we may generate a spurious interrupt,
but we should never miss a real lost interrupt.
*/

static void
sn_check_intr(int irq, pcibr_intr_t intr)
{
	unsigned long regval;
	int irr_reg_num;
	int irr_bit;
	unsigned long irr_reg;


	regval = pcireg_intr_status_get(intr->bi_soft);
	irr_reg_num = irq_to_vector(irq) / 64;
	irr_bit = irq_to_vector(irq) % 64;
	switch (irr_reg_num) {
		case 0:
			irr_reg = ia64_getreg(_IA64_REG_CR_IRR0);
			break;
		case 1:
			irr_reg = ia64_getreg(_IA64_REG_CR_IRR1);
			break;
		case 2:
			irr_reg = ia64_getreg(_IA64_REG_CR_IRR2);
			break;
		case 3:
			irr_reg = ia64_getreg(_IA64_REG_CR_IRR3);
			break;
	}
	if (!test_bit(irr_bit, &irr_reg) ) {
		if (!test_bit(irq, pda->sn_soft_irr) ) {
			if (!test_bit(irq, pda->sn_in_service_ivecs) ) {
				regval &= 0xff;
				if (intr->bi_ibits & regval & intr->bi_last_intr) {
					regval &= ~(intr->bi_ibits & regval);
					pcibr_force_interrupt(intr);
				}
			}
		}
	}
	intr->bi_last_intr = regval;
}

void
sn_lb_int_war_check(void)
{
	int i;

	if (pda->sn_first_irq == 0) return;
	for (i=pda->sn_first_irq;
		i <= pda->sn_last_irq; i++) {
			struct sn_intr_list_t *p = sn_intr_list[i];
			if (p == NULL) {
				continue;
			}
			while (p) {
				sn_check_intr(i, p->intr);
				p = p->next;
			}
	}
}
