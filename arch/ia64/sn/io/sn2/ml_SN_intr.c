/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997, 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * intr.c-
 *	This file contains all of the routines necessary to set up and
 *	handle interrupts on an IPXX board.
 */

#ident  "$Revision: 1.167 $"

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/hw_irq.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
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
#include <asm/sal.h>
#include <asm/sn/sn_sal.h>

extern irqpda_t	*irqpdaindr[];
extern cnodeid_t master_node_get(devfs_handle_t vhdl);
extern nasid_t master_nasid;

//  Initialize some shub registers for interrupts, both IO and error.

void
intr_init_vecblk( nodepda_t *npda,
		cnodeid_t node,
		int sn)
{
	int 			nasid = cnodeid_to_nasid(node);
	nasid_t			console_nasid;
	sh_ii_int0_config_u_t	ii_int_config;
	cpuid_t			cpu;
	cpuid_t			cpu0, cpu1;
	nodepda_t		*lnodepda;
	sh_ii_int0_enable_u_t	ii_int_enable;
	sh_local_int0_config_u_t	local_int_config;
	sh_local_int0_enable_u_t	local_int_enable;
	sh_fsb_system_agent_config_u_t	fsb_system_agent;
	sh_int_node_id_config_u_t	node_id_config;
	int is_console;

	console_nasid = get_console_nasid();
	if (console_nasid < 0) {
		console_nasid = master_nasid;
	}

	is_console = nasid == console_nasid;

	if (is_headless_node(node) ) {
		int cnode;
		struct ia64_sal_retval ret_stuff;

		// retarget all interrupts on this node to the master node.
		node_id_config.sh_int_node_id_config_regval = 0;
		node_id_config.sh_int_node_id_config_s.node_id = master_nasid;
		node_id_config.sh_int_node_id_config_s.id_sel = 1;
		HUB_S( (unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_INT_NODE_ID_CONFIG),
			node_id_config.sh_int_node_id_config_regval);
		cnode = nasid_to_cnodeid(master_nasid);
		lnodepda = NODEPDA(cnode);
		cpu = lnodepda->node_first_cpu;
		cpu = cpu_physical_id(cpu);
		SAL_CALL(ret_stuff, SN_SAL_REGISTER_CE, nasid, cpu, master_nasid,0,0,0,0);
		if (ret_stuff.status < 0) {
			printk("%s: SN_SAL_REGISTER_CE SAL_CALL failed\n",__FUNCTION__);
		}
	} else {
		lnodepda = NODEPDA(node);
		cpu = lnodepda->node_first_cpu;
		cpu = cpu_physical_id(cpu);
	}

	// Get the physical id's of the cpu's on this node.
	cpu0 = id_eid_to_cpu_physical_id(nasid, 0);
	cpu1 = id_eid_to_cpu_physical_id(nasid, 1);

	HUB_S( (unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_PI_ERROR_MASK), 0);
	HUB_S( (unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_PI_CRBP_ERROR_MASK), 0);

	// The II_INT_CONFIG register for cpu 0.
	ii_int_config.sh_ii_int0_config_s.type = 0;
	ii_int_config.sh_ii_int0_config_s.agt = 0;
	ii_int_config.sh_ii_int0_config_s.pid = cpu0;
	ii_int_config.sh_ii_int0_config_s.base = 0;

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_II_INT0_CONFIG),
		ii_int_config.sh_ii_int0_config_regval);

	// The II_INT_CONFIG register for cpu 1.
	ii_int_config.sh_ii_int0_config_s.type = 0;
	ii_int_config.sh_ii_int0_config_s.agt = 0;
	ii_int_config.sh_ii_int0_config_s.pid = cpu1;
	ii_int_config.sh_ii_int0_config_s.base = 0;

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_II_INT1_CONFIG),
		ii_int_config.sh_ii_int0_config_regval);

	// Enable interrupts for II_INT0 and 1.
	ii_int_enable.sh_ii_int0_enable_s.ii_enable = 1;

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_II_INT0_ENABLE),
		ii_int_enable.sh_ii_int0_enable_regval);
	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_II_INT1_ENABLE),
		ii_int_enable.sh_ii_int0_enable_regval);

	// init error regs
	// LOCAL_INT0 is for the UART only.

	local_int_config.sh_local_int0_config_s.type = 0;
	local_int_config.sh_local_int0_config_s.agt = 0;
	local_int_config.sh_local_int0_config_s.pid = cpu;
	local_int_config.sh_local_int0_config_s.base = 0;
	local_int_config.sh_local_int0_config_s.idx = SGI_UART_VECTOR;

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT0_CONFIG),
		local_int_config.sh_local_int0_config_regval);

	// LOCAL_INT1 is for all hardware errors.
	// It will send a BERR, which will result in an MCA.
	local_int_config.sh_local_int0_config_s.idx = 0;

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT1_CONFIG),
		local_int_config.sh_local_int0_config_regval);

	// Clear the LOCAL_INT_ENABLE register.
	local_int_enable.sh_local_int0_enable_regval = 0;

	if (is_console) {
		// Enable the UART interrupt.  Only applies to the console nasid.
		local_int_enable.sh_local_int0_enable_s.uart_int = 1;

		HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT0_ENABLE),
			local_int_enable.sh_local_int0_enable_regval);
	}

	// Enable all the error interrupts.
	local_int_enable.sh_local_int0_enable_s.uart_int = 0;
	local_int_enable.sh_local_int0_enable_s.pi_hw_int = 1;
	local_int_enable.sh_local_int0_enable_s.md_hw_int = 1;
	local_int_enable.sh_local_int0_enable_s.xn_hw_int = 1;
	local_int_enable.sh_local_int0_enable_s.lb_hw_int = 1;
	local_int_enable.sh_local_int0_enable_s.ii_hw_int = 1;
	local_int_enable.sh_local_int0_enable_s.pi_uce_int = 1;
	local_int_enable.sh_local_int0_enable_s.md_uce_int = 1;
	local_int_enable.sh_local_int0_enable_s.xn_uce_int = 1;
	local_int_enable.sh_local_int0_enable_s.system_shutdown_int = 1;
	local_int_enable.sh_local_int0_enable_s.l1_nmi_int = 1;
	local_int_enable.sh_local_int0_enable_s.stop_clock = 1;


	// Send BERR, rather than an interrupt, for shub errors.
	local_int_config.sh_local_int0_config_s.agt = 1;
	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT1_CONFIG),
		local_int_config.sh_local_int0_config_regval);

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT1_ENABLE),
		local_int_enable.sh_local_int0_enable_regval);

	// Make sure BERR is enabled.
	fsb_system_agent.sh_fsb_system_agent_config_regval = 
		HUB_L( (unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_FSB_SYSTEM_AGENT_CONFIG) );
	fsb_system_agent.sh_fsb_system_agent_config_s.berr_assert_en = 1;
	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_FSB_SYSTEM_AGENT_CONFIG),
		fsb_system_agent.sh_fsb_system_agent_config_regval);

	// Set LOCAL_INT2 to field CEs

	local_int_enable.sh_local_int0_enable_regval = 0;

	local_int_config.sh_local_int0_config_s.agt = 0;
	local_int_config.sh_local_int0_config_s.idx = SGI_SHUB_ERROR_VECTOR;
	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT2_CONFIG),
		local_int_config.sh_local_int0_config_regval);

	local_int_enable.sh_local_int0_enable_s.pi_ce_int = 1;
	local_int_enable.sh_local_int0_enable_s.md_ce_int = 1;
	local_int_enable.sh_local_int0_enable_s.xn_ce_int = 1;

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT2_ENABLE),
		local_int_enable.sh_local_int0_enable_regval);

	// Make sure all the rest of the LOCAL_INT regs are disabled.
	local_int_enable.sh_local_int0_enable_regval = 0;
	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT3_ENABLE),
		local_int_enable.sh_local_int0_enable_regval);

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT4_ENABLE),
		local_int_enable.sh_local_int0_enable_regval);

	HUB_S((unsigned long *)GLOBAL_MMR_ADDR(nasid, SH_LOCAL_INT5_ENABLE),
		local_int_enable.sh_local_int0_enable_regval);

}

// (Un)Reserve an irq on this cpu.

static int
do_intr_reserve_level(cpuid_t cpu,
			int bit,
			int reserve)
{
	int i;
	irqpda_t	*irqs = irqpdaindr[cpu];

	if (reserve) {
		if (bit < 0) {
			for (i = IA64_SN2_FIRST_DEVICE_VECTOR; i <= IA64_SN2_LAST_DEVICE_VECTOR; i++) {
				if (irqs->irq_flags[i] == 0) {
					bit = i;
					break;
				}
			}
		}
		if (bit < 0) {
			return -1;
		}
		if (irqs->irq_flags[bit] & SN2_IRQ_RESERVED) {
			return -1;
		} else {
			irqs->num_irq_used++;
			irqs->irq_flags[bit] |= SN2_IRQ_RESERVED;
			return bit;
		}
	} else {
		if (irqs->irq_flags[bit] & SN2_IRQ_RESERVED) {
			irqs->num_irq_used--;
			irqs->irq_flags[bit] &= ~SN2_IRQ_RESERVED;
			return bit;
		} else {
			return -1;
		}
	}
}

int
intr_reserve_level(cpuid_t cpu,
		int bit,
		int resflags,
		devfs_handle_t owner_dev,
		char *name)
{
	return(do_intr_reserve_level(cpu, bit, 1));
}

void
intr_unreserve_level(cpuid_t cpu,
		int bit)
{
	(void)do_intr_reserve_level(cpu, bit, 0);
}

// Mark an irq on this cpu as (dis)connected.

static int
do_intr_connect_level(cpuid_t cpu,
			int bit,
			int connect)
{
	irqpda_t	*irqs = irqpdaindr[cpu];

	if (connect) {
		if (irqs->irq_flags[bit] & SN2_IRQ_CONNECTED) {
			return -1;
		} else {
			irqs->irq_flags[bit] |= SN2_IRQ_CONNECTED;
			return bit;
		}
	} else {
		if (irqs->irq_flags[bit] & SN2_IRQ_CONNECTED) {
			irqs->irq_flags[bit] &= ~SN2_IRQ_CONNECTED;
			return bit;
		} else {
			return -1;
		}
	}
	return(bit);
}

int
intr_connect_level(cpuid_t cpu,
		int bit,
		ilvl_t is,
		intr_func_t intr_prefunc)
{
	return(do_intr_connect_level(cpu, bit, 1));
}

int
intr_disconnect_level(cpuid_t cpu,
		int bit)
{
	return(do_intr_connect_level(cpu, bit, 0));
}

// Choose a cpu on this node.
// We choose the one with the least number of int's assigned to it.

static cpuid_t
do_intr_cpu_choose(cnodeid_t cnode) {
	cpuid_t		cpu, best_cpu = CPU_NONE;
	int		slice, min_count = 1000;
	irqpda_t	*irqs;

	for (slice = 0; slice < CPUS_PER_NODE; slice++) {
		int intrs;

		cpu = cnode_slice_to_cpuid(cnode, slice);
		if (cpu == CPU_NONE) {
			continue;
		}

		if (!cpu_enabled(cpu)) {
			continue;
		}

		irqs = irqpdaindr[cpu];
		intrs = irqs->num_irq_used;

		if (min_count > intrs) {
			min_count = intrs;
			best_cpu = cpu;
		}
	}
	return best_cpu;
}

static cpuid_t
intr_cpu_choose_from_node(cnodeid_t cnode)
{
	return(do_intr_cpu_choose(cnode));
}

// See if we can use this cpu/vect.

static cpuid_t
intr_bit_reserve_test(cpuid_t cpu,
			int favor_subnode,
			cnodeid_t cnode,
			int req_bit,
			int resflags,
			devfs_handle_t owner_dev,
			char *name,
			int *resp_bit)
{
	ASSERT( (cpu == CPU_NONE) || (cnode == CNODEID_NONE) );

	if (cnode != CNODEID_NONE) {
		cpu = intr_cpu_choose_from_node(cnode);
	}

	if (cpu != CPU_NONE) {
		*resp_bit = do_intr_reserve_level(cpu, req_bit, 1);
		if (*resp_bit >= 0) {
			return(cpu);
		}
	}
	return CPU_NONE;
}

// Find the node to assign for this interrupt.

cpuid_t
intr_heuristic(devfs_handle_t dev,
		device_desc_t dev_desc,
		int	req_bit,
		int resflags,
		devfs_handle_t owner_dev,
		char *name,
		int *resp_bit)
{
	cpuid_t		cpuid;
	cnodeid_t	candidate = -1;
	devfs_handle_t	pconn_vhdl;
	pcibr_soft_t	pcibr_soft;

/* SN2 + pcibr addressing limitation */
/* Due to this limitation, all interrupts from a given bridge must go to the name node.*/
/* This limitation does not exist on PIC. */

	if ( (hwgraph_edge_get(dev, EDGE_LBL_PCI, &pconn_vhdl) == GRAPH_SUCCESS) &&
		( (pcibr_soft = pcibr_soft_get(pconn_vhdl) ) != NULL) ) {
			if (pcibr_soft->bsi_err_intr) {
				candidate = cpuid_to_cnodeid( ((hub_intr_t)pcibr_soft->bsi_err_intr)->i_cpuid);
			}
	}

	if (candidate >= 0) {
		// The node was chosen already when we assigned the error interrupt.
		cpuid = intr_bit_reserve_test(CPU_NONE,
						0,
						candidate,
						req_bit,
						0,
						owner_dev,
						name,
						resp_bit);
	} else {
		// Need to choose one.  Try the controlling c-brick first.
		cpuid = intr_bit_reserve_test(CPU_NONE,
						0,
						master_node_get(dev),
						req_bit,
						0,
						owner_dev,
						name,
						resp_bit);
	}

	if (cpuid != CPU_NONE) {
		return cpuid;
	}

	if (candidate >= 0) {
		printk("Cannot target interrupt to target node (%d).\n",candidate);
		return CPU_NONE;
	} else {
		printk("Cannot target interrupt to closest node (%d) 0x%p\n",
			master_node_get(dev), (void *)owner_dev);
	}

	// We couldn't put it on the closest node.  Try to find another one.
	// Do a stupid round-robin assignment of the node.

	{
		static cnodeid_t last_node = -1;
		if (last_node >= numnodes) last_node = 0;
		for (candidate = last_node + 1; candidate != last_node; candidate++) {
			if (candidate == numnodes) candidate = 0;
			cpuid = intr_bit_reserve_test(CPU_NONE,
							0,
							candidate,
							req_bit,
							0,
							owner_dev,
							name,
							resp_bit);
			if (cpuid != CPU_NONE) {
				return cpuid;
			}
		}
	}

	printk("cannot target interrupt: 0x%p\n",(void *)owner_dev);
	return CPU_NONE;
}
