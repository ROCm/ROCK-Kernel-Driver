/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997, 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/hw_irq.h>
#include <asm/topology.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/xtalk/xtalk.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/intr.h>
#include <asm/sn/sn2/shub_mmr_t.h>
#include <asm/sn/sn2/shubio.h>
#include <asm/sal.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn2/shub_mmr.h>
#include <asm/sn/pda.h>

extern irqpda_t	*irqpdaindr;
extern cnodeid_t master_node_get(vertex_hdl_t vhdl);
extern nasid_t master_nasid;

/*  Initialize some shub registers for interrupts, both IO and error. */
void intr_init_vecblk(cnodeid_t node)
{
	int 				nasid = cnodeid_to_nasid(node);
	sh_ii_int0_config_u_t		ii_int_config;
	cpuid_t				cpu;
	cpuid_t				cpu0, cpu1;
	sh_ii_int0_enable_u_t		ii_int_enable;
	sh_int_node_id_config_u_t	node_id_config;
	sh_local_int5_config_u_t	local5_config;
	sh_local_int5_enable_u_t	local5_enable;

	if (is_headless_node(node) ) {
		struct ia64_sal_retval ret_stuff;
		int cnode;

		/* retarget all interrupts on this node to the master node. */
		node_id_config.sh_int_node_id_config_regval = 0;
		node_id_config.sh_int_node_id_config_s.node_id = master_nasid;
		node_id_config.sh_int_node_id_config_s.id_sel = 1;
		HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_INT_NODE_ID_CONFIG),
			node_id_config.sh_int_node_id_config_regval);
		cnode = nasid_to_cnodeid(master_nasid);
		cpu = first_cpu(node_to_cpumask(cnode));
		cpu = cpu_physical_id(cpu);
		SAL_CALL(ret_stuff, SN_SAL_REGISTER_CE, nasid, cpu, master_nasid,0,0,0,0);
		if (ret_stuff.status < 0)
			printk("%s: SN_SAL_REGISTER_CE SAL_CALL failed\n",__FUNCTION__);
	} else {
		cpu = first_cpu(node_to_cpumask(node));
		cpu = cpu_physical_id(cpu);
	}

	/* Get the physical id's of the cpu's on this node. */
	cpu0 = nasid_slice_to_cpu_physical_id(nasid, 0);
	cpu1 = nasid_slice_to_cpu_physical_id(nasid, 2);

	HUB_S( (unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_PI_ERROR_MASK), 0);
	HUB_S( (unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_PI_CRBP_ERROR_MASK), 0);

	/* Config and enable UART interrupt, all nodes. */
	local5_config.sh_local_int5_config_regval = 0;
	local5_config.sh_local_int5_config_s.idx = SGI_UART_VECTOR;
	local5_config.sh_local_int5_config_s.pid = cpu;
	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT5_CONFIG),
		local5_config.sh_local_int5_config_regval);

	local5_enable.sh_local_int5_enable_regval = 0;
	local5_enable.sh_local_int5_enable_s.uart_int = 1;
	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT5_ENABLE),
		local5_enable.sh_local_int5_enable_regval);


	/* The II_INT_CONFIG register for cpu 0. */
	ii_int_config.sh_ii_int0_config_regval = 0;
	ii_int_config.sh_ii_int0_config_s.type = 0;
	ii_int_config.sh_ii_int0_config_s.agt = 0;
	ii_int_config.sh_ii_int0_config_s.pid = cpu0;
	ii_int_config.sh_ii_int0_config_s.base = 0;

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_II_INT0_CONFIG),
		ii_int_config.sh_ii_int0_config_regval);


	/* The II_INT_CONFIG register for cpu 1. */
	ii_int_config.sh_ii_int0_config_regval = 0;
	ii_int_config.sh_ii_int0_config_s.type = 0;
	ii_int_config.sh_ii_int0_config_s.agt = 0;
	ii_int_config.sh_ii_int0_config_s.pid = cpu1;
	ii_int_config.sh_ii_int0_config_s.base = 0;

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_II_INT1_CONFIG),
		ii_int_config.sh_ii_int0_config_regval);


	/* Enable interrupts for II_INT0 and 1. */
	ii_int_enable.sh_ii_int0_enable_regval = 0;
	ii_int_enable.sh_ii_int0_enable_s.ii_enable = 1;

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_II_INT0_ENABLE),
		ii_int_enable.sh_ii_int0_enable_regval);
	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_II_INT1_ENABLE),
		ii_int_enable.sh_ii_int0_enable_regval);
}

static int intr_reserve_level(cpuid_t cpu, int bit)
{
	irqpda_t	*irqs = irqpdaindr;
	int		min_shared;
	int		i;

	if (bit < 0) {
		for (i = IA64_SN2_FIRST_DEVICE_VECTOR; i <= IA64_SN2_LAST_DEVICE_VECTOR; i++) {
			if (irqs->irq_flags[i] == 0) {
				bit = i;
				break;
			}
		}
	}

	if (bit < 0) {  /* ran out of irqs.  Have to share.  This will be rare. */
		min_shared = 256;
		for (i=IA64_SN2_FIRST_DEVICE_VECTOR; i < IA64_SN2_LAST_DEVICE_VECTOR; i++) {
			/* Share with the same device class */
			/* XXX: gross layering violation.. */
			if (irqpdaindr->curr->vendor == irqpdaindr->device_dev[i]->vendor &&
				irqpdaindr->curr->device == irqpdaindr->device_dev[i]->device &&
				irqpdaindr->share_count[i] < min_shared) {
					min_shared = irqpdaindr->share_count[i];
					bit = i;
			}
		}
	
		min_shared = 256;
		if (bit < 0) {  /* didn't find a matching device, just pick one. This will be */
				/* exceptionally rare. */
			for (i=IA64_SN2_FIRST_DEVICE_VECTOR; i < IA64_SN2_LAST_DEVICE_VECTOR; i++) {
				if (irqpdaindr->share_count[i] < min_shared) {
					min_shared = irqpdaindr->share_count[i];
					bit = i;
				}
			}
		}
		irqpdaindr->share_count[bit]++;
	}

	if (!(irqs->irq_flags[bit] & SN2_IRQ_SHARED)) {
		if (irqs->irq_flags[bit] & SN2_IRQ_RESERVED)
			return -1;
		irqs->num_irq_used++;
	}

	irqs->irq_flags[bit] |= SN2_IRQ_RESERVED;
	return bit;
}

void intr_unreserve_level(cpuid_t cpu,
		int bit)
{
	irqpda_t	*irqs = irqpdaindr;

	if (irqs->irq_flags[bit] & SN2_IRQ_RESERVED) {
		irqs->num_irq_used--;
		irqs->irq_flags[bit] &= ~SN2_IRQ_RESERVED;
	}
}

int intr_connect_level(cpuid_t cpu, int bit)
{
	irqpda_t	*irqs = irqpdaindr;

	if (!(irqs->irq_flags[bit] & SN2_IRQ_SHARED) &&
	     (irqs->irq_flags[bit] & SN2_IRQ_CONNECTED))
		return -1;
 
	irqs->irq_flags[bit] |= SN2_IRQ_CONNECTED;
	return bit;
}

int intr_disconnect_level(cpuid_t cpu, int bit)
{
	irqpda_t	*irqs = irqpdaindr;

	if (!(irqs->irq_flags[bit] & SN2_IRQ_CONNECTED))
		return -1;
	irqs->irq_flags[bit] &= ~SN2_IRQ_CONNECTED;
	return bit;
}

/*
 * Choose a cpu on this node.
 *
 * We choose the one with the least number of int's assigned to it.
 */
static cpuid_t intr_cpu_choose_from_node(cnodeid_t cnode)
{
	cpuid_t		cpu, best_cpu = CPU_NONE;
	int		slice, min_count = 1000;

	for (slice = CPUS_PER_NODE - 1; slice >= 0; slice--) {
		int intrs;

		cpu = cnode_slice_to_cpuid(cnode, slice);
		if (cpu == NR_CPUS)
			continue;
		if (!cpu_online(cpu))
			continue;

		intrs = pdacpu(cpu)->sn_num_irqs;

		if (min_count > intrs) {
			min_count = intrs;
			best_cpu = cpu;
			if (enable_shub_wars_1_1()) {
				/*
				 * Rather than finding the best cpu, always
				 * return the first cpu.  This forces all
				 * interrupts to the same cpu
				 */
				break;
			}
		}
	}
	pdacpu(best_cpu)->sn_num_irqs++;
	return best_cpu;
}

/*
 * We couldn't put it on the closest node.  Try to find another one.
 * Do a stupid round-robin assignment of the node.
 */
static cpuid_t intr_cpu_choose_node(void)
{
	static cnodeid_t last_node = -1;	/* XXX: racy */
	cnodeid_t candidate_node;
	cpuid_t cpuid;

	if (last_node >= numnodes)
		last_node = 0;

	for (candidate_node = last_node + 1; candidate_node != last_node;
			candidate_node++) {
		if (candidate_node == numnodes)
			candidate_node = 0;
		cpuid = intr_cpu_choose_from_node(candidate_node);
		if (cpuid != CPU_NONE)
			return cpuid;
	}

	return CPU_NONE;
}

/*
 * Find the node to assign for this interrupt.
 *
 * SN2 + pcibr addressing limitation:
 *   Due to this limitation, all interrupts from a given bridge must
 *   go to the name node.  The interrupt must also be targetted for
 *   the same processor.  This limitation does not exist on PIC.
 *   But, the processor limitation will stay.  The limitation will be
 *   similar to the bedrock/xbridge limit regarding PI's
 */
cpuid_t intr_heuristic(vertex_hdl_t dev, int req_bit, int *resp_bit)
{
	cpuid_t		cpuid;
	vertex_hdl_t	pconn_vhdl;
	pcibr_soft_t	pcibr_soft;
	int 		bit;

	/* XXX: gross layering violation.. */
	if (hwgraph_edge_get(dev, EDGE_LBL_PCI, &pconn_vhdl) == GRAPH_SUCCESS) {
		pcibr_soft = pcibr_soft_get(pconn_vhdl);
		if (pcibr_soft && pcibr_soft->bsi_err_intr) {
			/*
			 * The cpu was chosen already when we assigned
			 * the error interrupt.
			 */
			cpuid = ((hub_intr_t)pcibr_soft->bsi_err_intr)->i_cpuid;
			goto done;
		}
	}

	/*
	 * Need to choose one.  Try the controlling c-brick first.
	 */
	cpuid = intr_cpu_choose_from_node(master_node_get(dev));
	if (cpuid == CPU_NONE)
		cpuid = intr_cpu_choose_node();

 done:
	if (cpuid != CPU_NONE) {
		bit = intr_reserve_level(cpuid, req_bit);
		if (bit >= 0) {
			*resp_bit = bit;
			return cpuid;
		}
	}

	printk("Cannot target interrupt to target cpu (%ld).\n", cpuid);
	return CPU_NONE;
}
