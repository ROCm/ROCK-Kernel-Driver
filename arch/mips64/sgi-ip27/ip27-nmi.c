#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <asm/atomic.h>
#include <asm/sn/types.h>
#include <asm/sn/addrs.h>
#include <asm/sn/nmi.h>
#include <asm/sn/arch.h>
#include <asm/sn/sn0/hub.h>

#if 0
#define NODE_NUM_CPUS(n)	CNODE_NUM_CPUS(n)
#else
#define NODE_NUM_CPUS(n)	CPUS_PER_NODE
#endif

#define CNODEID_NONE (cnodeid_t)-1
#define enter_panic_mode()	spin_lock(&nmi_lock)

typedef unsigned long machreg_t;

spinlock_t nmi_lock = SPIN_LOCK_UNLOCKED;

/*
 * Lets see what else we need to do here. Set up sp, gp?
 */
void nmi_dump(void)
{
	void cont_nmi_dump(void);

	cont_nmi_dump();
}

void install_cpu_nmi_handler(int slice)
{
	nmi_t *nmi_addr;

	nmi_addr = (nmi_t *)NMI_ADDR(get_nasid(), slice);
	if (nmi_addr->call_addr)
		return;
	nmi_addr->magic = NMI_MAGIC;
	nmi_addr->call_addr = (void *)nmi_dump;
	nmi_addr->call_addr_c =
		(void *)(~((unsigned long)(nmi_addr->call_addr)));
	nmi_addr->call_parm = 0;
}

/*
 * Copy the cpu registers which have been saved in the IP27prom format
 * into the eframe format for the node under consideration.
 */

void
nmi_cpu_eframe_save(nasid_t nasid,
		    int	    slice)
{
	int 		i, numberof_nmi_cpu_regs;
	machreg_t	*prom_format;

	/* Get the total number of registers being saved by the prom */
	numberof_nmi_cpu_regs = sizeof(struct reg_struct) / sizeof(machreg_t);

	/* Get the pointer to the current cpu's register set. */
	prom_format = 
	    (machreg_t *)(TO_UNCAC(TO_NODE(nasid, IP27_NMI_KREGS_OFFSET)) +
			  slice * IP27_NMI_KREGS_CPU_SIZE);

	printk("NMI nasid %d: slice %d\n", nasid, slice);
	for (i = 0; i < numberof_nmi_cpu_regs; i++)
		printk("0x%lx  ", prom_format[i]);
	printk("\n\n");
}	

/*
 * Copy the cpu registers which have been saved in the IP27prom format
 * into the eframe format for the node under consideration.
 */
void
nmi_node_eframe_save(cnodeid_t  cnode)
{
	int		cpu;
	nasid_t		nasid;

	/* Make sure that we have a valid node */
	if (cnode == CNODEID_NONE)
		return;

	nasid = COMPACT_TO_NASID_NODEID(cnode);
	if (nasid == INVALID_NASID)
		return;

	/* Save the registers into eframe for each cpu */
	for(cpu = 0; cpu < NODE_NUM_CPUS(cnode); cpu++) 
		nmi_cpu_eframe_save(nasid, cpu);
}

/*
 * Save the nmi cpu registers for all cpus in the system.
 */
void
nmi_eframes_save(void)
{
	cnodeid_t	cnode;

	for(cnode = 0 ; cnode < numnodes; cnode++) 
		nmi_node_eframe_save(cnode);
}

void
cont_nmi_dump(void)
{
#ifndef REAL_NMI_SIGNAL
	static atomic_t nmied_cpus = ATOMIC_INIT(0);

	atomic_inc(&nmied_cpus);
#endif
	/* 
	 * Use enter_panic_mode to allow only 1 cpu to proceed
	 */
	enter_panic_mode();

#ifdef REAL_NMI_SIGNAL
	/*
	 * Wait up to 15 seconds for the other cpus to respond to the NMI.
	 * If a cpu has not responded after 10 sec, send it 1 additional NMI.
	 * This is for 2 reasons:
	 *	- sometimes a MMSC fail to NMI all cpus.
	 *	- on 512p SN0 system, the MMSC will only send NMIs to
	 *	  half the cpus. Unfortunately, we dont know which cpus may be
	 *	  NMIed - it depends on how the site chooses to configure.
	 * 
	 * Note: it has been measure that it takes the MMSC up to 2.3 secs to
	 * send NMIs to all cpus on a 256p system.
	 */
	for (i=0; i < 1500; i++) {
		for (node=0; node < numnodes; node++)
			if (NODEPDA(node)->dump_count == 0)
				break;
		if (node == numnodes)
			break;
		if (i == 1000) {
			for (node=0; node < numnodes; node++)
				if (NODEPDA(node)->dump_count == 0) {
					cpu = CNODE_TO_CPU_BASE(node);
					for (n=0; n < CNODE_NUM_CPUS(node); cpu++, n++) {
						CPUMASK_SETB(nmied_cpus, cpu);
						/* 
						 * cputonasid, cputoslice
						 * needs kernel cpuid
						 */
						SEND_NMI((cputonasid(cpu)), (cputoslice(cpu)));
					}
				}
					
		}
		udelay(10000);
	}
#else
	while (atomic_read(&nmied_cpus) != smp_num_cpus);
#endif

	/*
	 * Save the nmi cpu registers for all cpu in the eframe format.
	 */
	nmi_eframes_save();
	LOCAL_HUB_S(NI_PORT_RESET, NPR_PORTRESET | NPR_LOCALRESET);
}
