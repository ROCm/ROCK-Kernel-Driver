#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mmzone.h>	/* for numnodes */
#include <linux/mm.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/sn/types.h>
#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ioc3.h>
#include <asm/mipsregs.h>
#include <asm/sn/gda.h>
#include <asm/sn/intr.h>
#include <asm/current.h>
#include <asm/smp.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/sn/launch.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/mapped_kernel.h>

#define CPU_NONE		(cpuid_t)-1

#define	CPUMASK_CLRALL(p)	(p) = 0
#define CPUMASK_SETB(p, bit)	(p) |= 1 << (bit)
#define CPUMASK_CLRB(p, bit)	(p) &= ~(1ULL << (bit))
#define CPUMASK_TSTB(p, bit)	((p) & (1ULL << (bit)))

#define CNODEMASK_CLRALL(p)	(p) = 0
#define CNODEMASK_TSTB(p, bit)	((p) & (1ULL << (bit)))
#define CNODEMASK_SETB(p, bit)	((p) |= 1ULL << (bit))

cpumask_t	boot_cpumask;
hubreg_t	region_mask = 0;
static int	fine_mode = 0;
int		maxcpus;
static spinlock_t hub_mask_lock = SPIN_LOCK_UNLOCKED;
static cnodemask_t hub_init_mask;
static atomic_t numstarted = ATOMIC_INIT(1);
nasid_t master_nasid = INVALID_NASID;

cnodeid_t	nasid_to_compact_node[MAX_NASIDS];
nasid_t		compact_to_nasid_node[MAX_COMPACT_NODES];
cnodeid_t	cpuid_to_compact_node[MAXCPUS];

hubreg_t get_region(cnodeid_t cnode)
{
	if (fine_mode)
		return COMPACT_TO_NASID_NODEID(cnode) >> NASID_TO_FINEREG_SHFT;
	else
		return COMPACT_TO_NASID_NODEID(cnode) >> NASID_TO_COARSEREG_SHFT;
}

static void gen_region_mask(hubreg_t *region_mask, int maxnodes)
{
	cnodeid_t cnode;

	(*region_mask) = 0;
	for (cnode = 0; cnode < maxnodes; cnode++) {
		(*region_mask) |= 1ULL << get_region(cnode);
	}
}

int is_fine_dirmode(void)
{
	return (((LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_REGIONSIZE_MASK)
		>> NSRI_REGIONSIZE_SHFT) & REGIONSIZE_FINE);
}

nasid_t get_actual_nasid(lboard_t *brd)
{
	klhub_t *hub;

	if (!brd)
		return INVALID_NASID;

	/* find out if we are a completely disabled brd. */
	hub  = (klhub_t *)find_first_component(brd, KLSTRUCT_HUB);
	if (!hub)
		return INVALID_NASID;
	if (!(hub->hub_info.flags & KLINFO_ENABLE))	/* disabled node brd */
		return hub->hub_info.physid;
	else
		return brd->brd_nasid;
}

int do_cpumask(cnodeid_t cnode, nasid_t nasid, cpumask_t *boot_cpumask, 
							int *highest)
{
	lboard_t *brd;
	klcpu_t *acpu;
	int cpus_found = 0;
	cpuid_t cpuid;

	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_IP27);

	do {
		acpu = (klcpu_t *)find_first_component(brd, KLSTRUCT_CPU);
		while (acpu) {
			cpuid = acpu->cpu_info.virtid;
			/* cnode is not valid for completely disabled brds */
			if (get_actual_nasid(brd) == brd->brd_nasid)
				cpuid_to_compact_node[cpuid] = cnode;
			if (cpuid > *highest)
				*highest = cpuid;
			/* Only let it join in if it's marked enabled */
			if (acpu->cpu_info.flags & KLINFO_ENABLE) {
				CPUMASK_SETB(*boot_cpumask, cpuid);
				cpus_found++;
			}
			acpu = (klcpu_t *)find_component(brd, (klinfo_t *)acpu, 
								KLSTRUCT_CPU);
		}
		brd = KLCF_NEXT(brd);
		if (brd)
			brd = find_lboard(brd,KLTYPE_IP27);
		else
			break;
	} while (brd);

	return cpus_found;
}

cpuid_t cpu_node_probe(cpumask_t *boot_cpumask, int *numnodes)
{
	int i, cpus = 0, highest = 0;
	gda_t *gdap = GDA;
	nasid_t nasid;

	/*
	 * Initialize the arrays to invalid nodeid (-1)
	 */
	for (i = 0; i < MAX_COMPACT_NODES; i++)
		compact_to_nasid_node[i] = INVALID_NASID;
	for (i = 0; i < MAX_NASIDS; i++)
		nasid_to_compact_node[i] = INVALID_CNODEID;
	for (i = 0; i < MAXCPUS; i++)
		cpuid_to_compact_node[i] = INVALID_CNODEID;

	*numnodes = 0;
	for (i = 0; i < MAX_COMPACT_NODES; i++) {
		if ((nasid = gdap->g_nasidtable[i]) == INVALID_NASID) {
			break;
		} else {
			compact_to_nasid_node[i] = nasid;
			nasid_to_compact_node[nasid] = i;
			(*numnodes)++;
			cpus += do_cpumask(i, nasid, boot_cpumask, &highest);
		}
	}

	/*
	 * Cpus are numbered in order of cnodes. Currently, disabled
	 * cpus are not numbered.
	 */

	return(highest + 1);
}

int cpu_enabled(cpuid_t cpu)
{
	if (cpu == CPU_NONE)
		return 0;
	return (CPUMASK_TSTB(boot_cpumask, cpu) != 0);
}

void mlreset (void)
{
	int i;

	master_nasid = get_nasid();
	fine_mode = is_fine_dirmode();

	/*
	 * Probe for all CPUs - this creates the cpumask and
	 * sets up the mapping tables.
	 */
	CPUMASK_CLRALL(boot_cpumask);
	maxcpus = cpu_node_probe(&boot_cpumask, &numnodes);
	printk("Discovered %d cpus on %d nodes\n", maxcpus, numnodes);

	gen_region_mask(&region_mask, numnodes);
	CNODEMASK_CLRALL(hub_init_mask);

	setup_replication_mask(numnodes);

	/*
	 * Set all nodes' calias sizes to 8k
	 */
	for (i = 0; i < numnodes; i++) {
		nasid_t nasid;

		nasid = COMPACT_TO_NASID_NODEID(i);

		/*
		 * Always have node 0 in the region mask, otherwise
		 * CALIAS accesses get exceptions since the hub
		 * thinks it is a node 0 address.
		 */
		REMOTE_HUB_S(nasid, PI_REGION_PRESENT, (region_mask | 1));
#ifdef CONFIG_REPLICATE_EXHANDLERS
		REMOTE_HUB_S(nasid, PI_CALIAS_SIZE, PI_CALIAS_SIZE_8K);
#else
		REMOTE_HUB_S(nasid, PI_CALIAS_SIZE, PI_CALIAS_SIZE_0);
#endif

#ifdef LATER
		/*
		 * Set up all hubs to have a big window pointing at
		 * widget 0. Memory mode, widget 0, offset 0
		 */
		REMOTE_HUB_S(nasid, IIO_ITTE(SWIN0_BIGWIN),
			((HUB_PIO_MAP_TO_MEM << IIO_ITTE_IOSP_SHIFT) |
			(0 << IIO_ITTE_WIDGET_SHIFT)));
#endif
	}
}


void intr_clear_bits(nasid_t nasid, volatile hubreg_t *pend, int base_level,
							char *name)
{
	volatile hubreg_t bits;
	int i;

	/* Check pending interrupts */
	if ((bits = HUB_L(pend)) != 0)
		for (i = 0; i < N_INTPEND_BITS; i++)
			if (bits & (1 << i))
				LOCAL_HUB_CLR_INTR(base_level + i);
}
	
void intr_clear_all(nasid_t nasid)
{
	REMOTE_HUB_S(nasid, PI_INT_MASK0_A, 0);
	REMOTE_HUB_S(nasid, PI_INT_MASK0_B, 0);
	REMOTE_HUB_S(nasid, PI_INT_MASK1_A, 0);
	REMOTE_HUB_S(nasid, PI_INT_MASK1_B, 0);
	intr_clear_bits(nasid, REMOTE_HUB_ADDR(nasid, PI_INT_PEND0),
		INT_PEND0_BASELVL, "INT_PEND0");
	intr_clear_bits(nasid, REMOTE_HUB_ADDR(nasid, PI_INT_PEND1),
		INT_PEND1_BASELVL, "INT_PEND1");
}

void sn_mp_setup(void)
{
	cnodeid_t	cnode;
#if 0
	cpuid_t		cpu;
#endif

	for (cnode = 0; cnode < numnodes; cnode++) {
#if 0
		init_platform_nodepda();
#endif
		intr_clear_all(COMPACT_TO_NASID_NODEID(cnode));
	}
#if 0
	for (cpu = 0; cpu < maxcpus; cpu++) {
		init_platform_pda();
	}
#endif
}

void per_hub_init(cnodeid_t cnode)
{
	extern void pcibr_setup(cnodeid_t);
	cnodemask_t	done;
	nasid_t		nasid;

	nasid = COMPACT_TO_NASID_NODEID(cnode);

	spin_lock(&hub_mask_lock);
	/* Test our bit. */
	if (!(done = CNODEMASK_TSTB(hub_init_mask, cnode))) {
		/* Turn our bit on in the mask. */
		CNODEMASK_SETB(hub_init_mask, cnode);
		/*
	 	 * Do the actual initialization if it hasn't been done yet.
	 	 * We don't need to hold a lock for this work.
	 	 */
		/*
		 * Set CRB timeout at 5ms, (< PI timeout of 10ms)
		 */
		REMOTE_HUB_S(nasid, IIO_ICTP, 0x800);
		REMOTE_HUB_S(nasid, IIO_ICTO, 0xff);
		hub_rtc_init(cnode);
		pcibr_setup(cnode); 
#ifdef CONFIG_REPLICATE_EXHANDLERS
		/*
		 * If this is not a headless node initialization, 
		 * copy over the caliased exception handlers.
		 */
		if (get_compact_nodeid() == cnode) {
			extern char except_vec0, except_vec1_r10k;
			extern char except_vec2_generic, except_vec3_generic;

			memcpy((void *)(KSEG0 + 0x100), &except_vec2_generic,
								0x80);
			memcpy((void *)(KSEG0 + 0x180), &except_vec3_generic,
								0x80);
			memcpy((void *)KSEG0, &except_vec0, 0x80);
			memcpy((void *)KSEG0 + 0x080, &except_vec1_r10k, 0x80);
			memcpy((void *)(KSEG0 + 0x100), (void *) KSEG0, 0x80);
			memcpy((void *)(KSEG0 + 0x180), &except_vec3_generic,
								0x100);
			flush_cache_l1();
			flush_cache_l2();
		}
#endif
	}
	spin_unlock(&hub_mask_lock);
}

/*
 * This is similar to hard_smp_processor_id().
 */
cpuid_t getcpuid(void)
{
	klcpu_t *klcpu;

	klcpu = nasid_slice_to_cpuinfo(get_nasid(),LOCAL_HUB_L(PI_CPU_NUM));
	return klcpu->cpu_info.virtid;
}

void per_cpu_init(void)
{
	extern void install_cpu_nmi_handler(int slice);
	extern void load_mmu(void);
	static int is_slave = 0;
	int cpu = smp_processor_id();
	cnodeid_t cnode = get_compact_nodeid();

	current_cpu_data.asid_cache = ASID_FIRST_VERSION;
	TLBMISS_HANDLER_SETUP();
#if 0
	intr_init();
#endif
	set_cp0_status(ST0_IM, 0);
	per_hub_init(cnode);
	cpu_time_init();
	if (smp_processor_id())	/* master can't do this early, no kmalloc */
		install_cpuintr(cpu);
	/* Install our NMI handler if symmon hasn't installed one. */
	install_cpu_nmi_handler(cputoslice(cpu));
#if 0
	install_tlbintr(cpu);
#endif
	set_cp0_status(SRB_DEV0 | SRB_DEV1, SRB_DEV0 | SRB_DEV1);
	if (is_slave) {
		set_cp0_status(ST0_BEV, 0);
		if (mips4_available)
			set_cp0_status(ST0_XX, ST0_XX);
		set_cp0_status(ST0_KX|ST0_SX|ST0_UX, ST0_KX|ST0_SX|ST0_UX);
		sti();
		load_mmu();
		atomic_inc(&numstarted);
	} else {
		is_slave = 1;
	}
}

cnodeid_t get_compact_nodeid(void)
{
	nasid_t nasid;

	nasid = get_nasid();
	/*
	 * Map the physical node id to a virtual node id (virtual node ids
	 * are contiguous).
	 */
	return NASID_TO_COMPACT_NODEID(nasid);
}

#ifdef CONFIG_SMP

/*
 * Takes as first input the PROM assigned cpu id, and the kernel
 * assigned cpu id as the second.
 */
static void alloc_cpupda(cpuid_t cpu, int cpunum)
{
	cnodeid_t	node;
	nasid_t		nasid;

	node = get_cpu_cnode(cpu);
	nasid = COMPACT_TO_NASID_NODEID(node);

	cputonasid(cpunum) = nasid;
	cputocnode(cpunum) = node;
	cputoslice(cpunum) = get_cpu_slice(cpu);
	cpu_data[cpunum].p_cpuid = cpu;
}

void __init smp_callin(void)
{
#if 0
	calibrate_delay();
	smp_store_cpu_info(cpuid);
#endif
}

int __init start_secondary(void)
{
	extern int cpu_idle(void);
	extern atomic_t smp_commenced;

	smp_callin();
	while (!atomic_read(&smp_commenced));
	return cpu_idle();
}

static volatile cpumask_t boot_barrier;

void cboot(void)
{
	CPUMASK_CLRB(boot_barrier, getcpuid());	/* needs atomicity */
	per_cpu_init();
#if 0
	ecc_init();
	bte_lateinit();
	init_mfhi_war();
#endif
	_flush_tlb_all();
	flush_cache_l1();
	flush_cache_l2();
	start_secondary();
}

void allowboot(void)
{
	int		num_cpus = 0;
	cpuid_t		cpu, mycpuid = getcpuid();
	cnodeid_t	cnode;
	extern void	bootstrap(void);

	sn_mp_setup();
	/* Master has already done per_cpu_init() */
	install_cpuintr(smp_processor_id());
#if 0
	bte_lateinit();
	ecc_init();
#endif

	replicate_kernel_text(numnodes);
	boot_barrier = boot_cpumask;
	/* Launch slaves. */
	for (cpu = 0; cpu < maxcpus; cpu++) {
		if (cpu == mycpuid) {
			alloc_cpupda(cpu, num_cpus);
			num_cpus++;
			/* We're already started, clear our bit */
			CPUMASK_CLRB(boot_barrier, cpu);
			continue;
		}

		/* Skip holes in CPU space */
		if (CPUMASK_TSTB(boot_cpumask, cpu)) {
			struct task_struct *p;

			/*
			 * The following code is purely to make sure
			 * Linux can schedule processes on this slave.
			 */
			kernel_thread(0, NULL, CLONE_PID);
			p = init_task.prev_task;
			sprintf(p->comm, "%s%d", "Idle", num_cpus);
			init_tasks[num_cpus] = p;
			alloc_cpupda(cpu, num_cpus);
			p->processor = num_cpus;
			p->has_cpu = 1; /* we schedule the first task manually */
			del_from_runqueue(p);
			unhash_process(p);
			/* Attach to the address space of init_task. */
			atomic_inc(&init_mm.mm_count);
			p->active_mm = &init_mm;
			
			/*
		 	 * Launch a slave into bootstrap().
		 	 * It doesn't take an argument, and we
			 * set sp to the kernel stack of the newly 
			 * created idle process, gp to the proc struct
			 * (so that current-> works).
		 	 */
			LAUNCH_SLAVE(cputonasid(num_cpus),cputoslice(num_cpus), 
				(launch_proc_t)MAPPED_KERN_RW_TO_K0(bootstrap),
				0, (void *)((unsigned long)p + 
				KERNEL_STACK_SIZE - 32), (void *)p);

			/*
			 * Now optimistically set the mapping arrays. We
			 * need to wait here, verify the cpu booted up, then
			 * fire up the next cpu.
			 */
			__cpu_number_map[cpu] = num_cpus;
			__cpu_logical_map[num_cpus] = cpu;
			num_cpus++;
			/*
			 * Wait this cpu to start up and initialize its hub,
			 * and discover the io devices it will control.
			 * 
			 * XXX: We really want to fire up launch all the CPUs
			 * at once.  We have to preserve the order of the
			 * devices on the bridges first though.
			 */
			while(atomic_read(&numstarted) != num_cpus);
		}
	}


#ifdef LATER
	Wait logic goes here.
#endif
	for (cnode = 0; cnode < numnodes; cnode++) {
#if 0
		if (cnodetocpu(cnode) == -1) {
			printk("Initializing headless hub,cnode %d", cnode);
			per_hub_init(cnode);
		}
#endif
	}
#if 0
	cpu_io_setup();
	init_mfhi_war();
#endif
	smp_num_cpus = num_cpus;
}

#else /* CONFIG_SMP */
void cboot(void) {}
#endif /* CONFIG_SMP */
