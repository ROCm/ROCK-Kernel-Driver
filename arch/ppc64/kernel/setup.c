/*
 * 
 * Common boot and setup code.
 *
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/version.h>
#include <linux/tty.h>
#include <linux/root_dev.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/smp.h>
#include <asm/elf.h>
#include <asm/machdep.h>
#include <asm/iSeries/LparData.h>
#include <asm/naca.h>
#include <asm/paca.h>
#include <asm/ppcdebug.h>
#include <asm/time.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/btext.h>
#include <asm/nvram.h>
#include <asm/system.h>

extern unsigned long klimit;
/* extern void *stab; */
extern HTAB htab_data;
extern unsigned long loops_per_jiffy;

int have_of = 1;

extern void  chrp_init(unsigned long r3,
		       unsigned long r4,
		       unsigned long r5,
		       unsigned long r6,
		       unsigned long r7);

extern void  pmac_init(unsigned long r3,
		       unsigned long r4,
		       unsigned long r5,
		       unsigned long r6,
		       unsigned long r7);

extern void iSeries_init( void );
extern void iSeries_init_early( void );
extern void pSeries_init_early( void );
extern void pSeriesLP_init_early(void);
extern void pmac_init_early(void);
extern void mm_init_ppc64( void ); 
extern void pseries_secondary_smp_init(unsigned long); 
extern int  idle_setup(void);
extern void vpa_init(int cpu);

unsigned long decr_overclock = 1;
unsigned long decr_overclock_proc0 = 1;
unsigned long decr_overclock_set = 0;
unsigned long decr_overclock_proc0_set = 0;

int powersave_nap;

char saved_command_line[COMMAND_LINE_SIZE];
unsigned char aux_device_present;

void parse_cmd_line(unsigned long r3, unsigned long r4, unsigned long r5,
		    unsigned long r6, unsigned long r7);
int parse_bootinfo(void);

#ifdef CONFIG_MAGIC_SYSRQ
unsigned long SYSRQ_KEY;
#endif /* CONFIG_MAGIC_SYSRQ */

struct machdep_calls ppc_md;

static int ppc64_panic_event(struct notifier_block *, unsigned long, void *);

static struct notifier_block ppc64_panic_block = {
	notifier_call: ppc64_panic_event,
	priority: INT_MIN /* may not return; must be done last */
};

/*
 * Perhaps we can put the pmac screen_info[] here
 * on pmac as well so we don't need the ifdef's.
 * Until we get multiple-console support in here
 * that is.  -- Cort
 * Maybe tie it to serial consoles, since this is really what
 * these processors use on existing boards.  -- Dan
 */ 
struct screen_info screen_info = {
	0, 25,			/* orig-x, orig-y */
	0,			/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25,			/* orig-video-lines */
	1,			/* orig-video-isVGA */
	16			/* orig-video-points */
};

/*
 * These are used in binfmt_elf.c to put aux entries on the stack
 * for each elf executable being started.
 */
int dcache_bsize;
int icache_bsize;
int ucache_bsize;

/*
 * Initialize the PPCDBG state.  Called before relocation has been enabled.
 */
void ppcdbg_initialize(void) {
	unsigned long offset = reloc_offset();
	struct naca_struct *_naca = RELOC(naca);

	_naca->debug_switch = PPC_DEBUG_DEFAULT; /* | PPCDBG_BUSWALK | PPCDBG_PHBINIT | PPCDBG_MM | PPCDBG_MMINIT | PPCDBG_TCEINIT | PPCDBG_TCE */;
}

static struct console udbg_console = {
	.name	= "udbg",
	.write	= udbg_console_write,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

static int early_console_initialized;

void __init disable_early_printk(void)
{
	if (!early_console_initialized)
		return;
	unregister_console(&udbg_console);
	early_console_initialized = 0;
}

/*
 * Do some initial setup of the system.  The parameters are those which 
 * were passed in from the bootloader.
 */
void setup_system(unsigned long r3, unsigned long r4, unsigned long r5,
		  unsigned long r6, unsigned long r7)
{
#if defined(CONFIG_SMP) && defined(CONFIG_PPC_PSERIES)
	unsigned int ret, i;
#endif

#ifdef CONFIG_XMON_DEFAULT
	xmon_init();
#endif

#ifdef CONFIG_PPC_ISERIES
	/* pSeries systems are identified in prom.c via OF. */
	if ( itLpNaca.xLparInstalled == 1 )
		systemcfg->platform = PLATFORM_ISERIES_LPAR;
#endif
	
	switch (systemcfg->platform) {
#ifdef CONFIG_PPC_ISERIES
	case PLATFORM_ISERIES_LPAR:
		iSeries_init_early();
		break;
#endif

#ifdef CONFIG_PPC_PSERIES
	case PLATFORM_PSERIES:
		pSeries_init_early();
		parse_bootinfo();
		break;

	case PLATFORM_PSERIES_LPAR:
		pSeriesLP_init_early();
		parse_bootinfo();
		break;
#endif /* CONFIG_PPC_PSERIES */
#ifdef CONFIG_PPC_PMAC
	case PLATFORM_POWERMAC:
		pmac_init_early();
		parse_bootinfo();
#endif /* CONFIG_PPC_PMAC */
	}

	/* If we were passed an initrd, set the ROOT_DEV properly if the values
	 * look sensible. If not, clear initrd reference.
	 */
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start >= KERNELBASE && initrd_end >= KERNELBASE &&
	    initrd_end > initrd_start)
		ROOT_DEV = Root_RAM0;
	else
		initrd_start = initrd_end = 0;
#endif /* CONFIG_BLK_DEV_INITRD */

#ifdef CONFIG_BOOTX_TEXT
	map_boot_text();
	if (systemcfg->platform == PLATFORM_POWERMAC) {
		early_console_initialized = 1;
		register_console(&udbg_console);
	}
#endif /* CONFIG_BOOTX_TEXT */

#ifdef CONFIG_PPC_PSERIES
	if (systemcfg->platform & PLATFORM_PSERIES) {
		early_console_initialized = 1;
		register_console(&udbg_console);
		__irq_offset_value = NUM_ISA_INTERRUPTS;
		finish_device_tree();
		chrp_init(r3, r4, r5, r6, r7);

#ifdef CONFIG_SMP
		/* Start secondary threads on SMT systems */
		for (i = 0; i < NR_CPUS; i++) {
			if(cpu_available(i)  && !cpu_possible(i)) {
				printk("%16.16x : starting thread\n", i);
				rtas_call(rtas_token("start-cpu"), 3, 1, 
					  (void *)&ret,
					  get_hard_smp_processor_id(i), 
					  *((unsigned long *)pseries_secondary_smp_init),
					  i);
				cpu_set(i, cpu_possible_map);
				systemcfg->processorCount++;
			}
		}
#endif /* CONFIG_SMP */
	}
#endif /* CONFIG_PPC_PSERIES */

#ifdef CONFIG_PPC_PMAC
	if (systemcfg->platform == PLATFORM_POWERMAC) {
		finish_device_tree();
		pmac_init(r3, r4, r5, r6, r7);
	}
#endif /* CONFIG_PPC_PMAC */

	/* Finish initializing the hash table (do the dynamic
	 * patching for the fast-path hashtable.S code)
	 */
	htab_finish_init();

	printk("Starting Linux PPC64 %s\n", UTS_RELEASE);

	printk("-----------------------------------------------------\n");
	printk("naca                          = 0x%p\n", naca);
	printk("naca->pftSize                 = 0x%lx\n", naca->pftSize);
	printk("naca->debug_switch            = 0x%lx\n", naca->debug_switch);
	printk("naca->interrupt_controller    = 0x%ld\n", naca->interrupt_controller);
	printk("systemcfg                     = 0x%p\n", systemcfg);
	printk("systemcfg->processorCount     = 0x%lx\n", systemcfg->processorCount);
	printk("systemcfg->physicalMemorySize = 0x%lx\n", systemcfg->physicalMemorySize);
	printk("systemcfg->dCacheL1LineSize   = 0x%x\n", systemcfg->dCacheL1LineSize);
	printk("systemcfg->iCacheL1LineSize   = 0x%x\n", systemcfg->iCacheL1LineSize);
	printk("htab_data.htab                = 0x%p\n", htab_data.htab);
	printk("htab_data.num_ptegs           = 0x%lx\n", htab_data.htab_num_ptegs);
	printk("-----------------------------------------------------\n");

	mm_init_ppc64();

#if defined(CONFIG_SMP) && defined(CONFIG_PPC_PSERIES)
	if (cur_cpu_spec->firmware_features & FW_FEATURE_SPLPAR) {
		vpa_init(boot_cpuid);
	}
#endif

	/* Select the correct idle loop for the platform. */
	idle_setup();

	switch (systemcfg->platform) {
#ifdef CONFIG_PPC_ISERIES
	case PLATFORM_ISERIES_LPAR:
		iSeries_init();
		break;
#endif
	default:
		/* The following relies on the device tree being */
		/* fully configured.                             */
		parse_cmd_line(r3, r4, r5, r6, r7);
	}
}

void machine_restart(char *cmd)
{
	if (ppc_md.nvram_sync)
		ppc_md.nvram_sync();
	ppc_md.restart(cmd);
}

EXPORT_SYMBOL(machine_restart);
  
void machine_power_off(void)
{
	if (ppc_md.nvram_sync)
		ppc_md.nvram_sync();
	ppc_md.power_off();
}

EXPORT_SYMBOL(machine_power_off);
  
void machine_halt(void)
{
	if (ppc_md.nvram_sync)
		ppc_md.nvram_sync();
	ppc_md.halt();
}

EXPORT_SYMBOL(machine_halt);

unsigned long ppc_proc_freq;
unsigned long ppc_tb_freq;

static int ppc64_panic_event(struct notifier_block *this,
                             unsigned long event, void *ptr)
{
	ppc_md.panic((char *)ptr);  /* May not return */
	return NOTIFY_DONE;
}


#ifdef CONFIG_SMP
DEFINE_PER_CPU(unsigned int, pvr);
#endif

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long cpu_id = (unsigned long)v - 1;
	unsigned int pvr;
	unsigned short maj;
	unsigned short min;

	if (cpu_id == NR_CPUS) {
		seq_printf(m, "timebase\t: %lu\n", ppc_tb_freq);

		if (ppc_md.get_cpuinfo != NULL)
			ppc_md.get_cpuinfo(m);

		return 0;
	}

	/* We only show online cpus: disable preempt (overzealous, I
	 * knew) to prevent cpu going down. */
	preempt_disable();
	if (!cpu_online(cpu_id)) {
		preempt_enable();
		return 0;
	}

#ifdef CONFIG_SMP
	pvr = per_cpu(pvr, cpu_id);
#else
	pvr = _get_PVR();
#endif
	maj = (pvr >> 8) & 0xFF;
	min = pvr & 0xFF;

	seq_printf(m, "processor\t: %lu\n", cpu_id);
	seq_printf(m, "cpu\t\t: ");

	if (cur_cpu_spec->pvr_mask)
		seq_printf(m, "%s", cur_cpu_spec->cpu_name);
	else
		seq_printf(m, "unknown (%08x)", pvr);

#ifdef CONFIG_ALTIVEC
	if (cur_cpu_spec->cpu_features & CPU_FTR_ALTIVEC)
		seq_printf(m, ", altivec supported");
#endif /* CONFIG_ALTIVEC */

	seq_printf(m, "\n");

	/*
	 * Assume here that all clock rates are the same in a
	 * smp system.  -- Cort
	 */
	seq_printf(m, "clock\t\t: %lu.%06luMHz\n", ppc_proc_freq / 1000000,
		   ppc_proc_freq % 1000000);

	seq_printf(m, "revision\t: %hd.%hd\n\n", maj, min);

	preempt_enable();
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos <= NR_CPUS ? (void *)((*pos)+1) : NULL;
}
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}
static void c_stop(struct seq_file *m, void *v)
{
}
struct seq_operations cpuinfo_op = {
	.start =c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};

/*
 * Fetch the cmd_line from open firmware. 
 */
void parse_cmd_line(unsigned long r3, unsigned long r4, unsigned long r5,
		  unsigned long r6, unsigned long r7)
{
	cmd_line[0] = 0;

#ifdef CONFIG_CMDLINE
	strlcpy(cmd_line, CONFIG_CMDLINE, sizeof(cmd_line));
#endif /* CONFIG_CMDLINE */

#ifdef CONFIG_PPC_PSERIES
	{
	struct device_node *chosen;

	chosen = of_find_node_by_name(NULL, "chosen");
	if (chosen != NULL) {
		char *p;
		p = get_property(chosen, "bootargs", NULL);
		if (p != NULL && p[0] != 0)
			strlcpy(cmd_line, p, sizeof(cmd_line));
		of_node_put(chosen);
	}
	}
#endif

	/* Look for mem= option on command line */
	if (strstr(cmd_line, "mem=")) {
		char *p, *q;
		unsigned long maxmem = 0;
		extern unsigned long __max_memory;

		for (q = cmd_line; (p = strstr(q, "mem=")) != 0; ) {
			q = p + 4;
			if (p > cmd_line && p[-1] != ' ')
				continue;
			maxmem = simple_strtoul(q, &q, 0);
			if (*q == 'k' || *q == 'K') {
				maxmem <<= 10;
				++q;
			} else if (*q == 'm' || *q == 'M') {
				maxmem <<= 20;
				++q;
			}
		}
		__max_memory = maxmem;
	}
}

#ifdef CONFIG_PPC_PSERIES
static int __init set_preferred_console(void)
{
	struct device_node *prom_stdout;
	char *name;
	int offset;

	/* The user has requested a console so this is already set up. */
	if (strstr(saved_command_line, "console="))
		return -EBUSY;

	prom_stdout = find_path_device(of_stdout_device);
	if (!prom_stdout)
		return -ENODEV;

	name = (char *)get_property(prom_stdout, "name", NULL);
	if (!name)
		return -ENODEV;

	if (strcmp(name, "serial") == 0) {
		int i;
		u32 *reg = (u32 *)get_property(prom_stdout, "reg", &i);
		if (i > 8) {
			switch (reg[1]) {
				case 0x3f8:
					offset = 0;
					break;
				case 0x2f8:
					offset = 1;
					break;
				case 0x898:
					offset = 2;
					break;
				case 0x890:
					offset = 3;
					break;
				default:
					/* We dont recognise the serial port */
					return -ENODEV;
			}
		}
	} else if (strcmp(name, "vty") == 0)
		/* pSeries LPAR virtual console */
		return add_preferred_console("hvc", 0, NULL);
	else if (strcmp(name, "ch-a") == 0)
		offset = 0;
	else if (strcmp(name, "ch-b") == 0)
		offset = 1;
	else
		return -ENODEV;

	return add_preferred_console("ttyS", offset, NULL);

}
console_initcall(set_preferred_console);

int parse_bootinfo(void)
{
	struct bi_record *rec;

	rec = prom.bi_recs;

	if ( rec == NULL || rec->tag != BI_FIRST )
		return -1;

	for ( ; rec->tag != BI_LAST ; rec = bi_rec_next(rec) ) {
		switch (rec->tag) {
		case BI_CMD_LINE:
			strlcpy(cmd_line, (void *)rec->data, sizeof(cmd_line));
			break;
		}
	}

	return 0;
}
#endif

int __init ppc_init(void)
{
	/* clear the progress line */
	ppc_md.progress(" ", 0xffff);

	if (ppc_md.init != NULL) {
		ppc_md.init();
	}
	return 0;
}

arch_initcall(ppc_init);

void __init ppc64_calibrate_delay(void)
{
	loops_per_jiffy = tb_ticks_per_jiffy;

	printk("Calibrating delay loop... %lu.%02lu BogoMips\n",
			       loops_per_jiffy/(500000/HZ),
			       loops_per_jiffy/(5000/HZ) % 100);
}	

extern void (*calibrate_delay)(void);

#ifdef CONFIG_IRQSTACKS
static void __init irqstack_early_init(void)
{
	int i;

	/* interrupt stacks must be under 256MB, we cannot afford to take SLB misses on them */
	for (i = 0; i < NR_CPUS; i++) {
		softirq_ctx[i] = (struct thread_info *)__va(lmb_alloc_base(THREAD_SIZE,
					THREAD_SIZE, 0x10000000));
		hardirq_ctx[i] = (struct thread_info *)__va(lmb_alloc_base(THREAD_SIZE,
					THREAD_SIZE, 0x10000000));
	}
}
#else
#define irqstack_early_init()
#endif

/*
 * Called into from start_kernel, after lock_kernel has been called.
 * Initializes bootmem, which is unsed to manage page allocation until
 * mem_init is called.
 */
void __init setup_arch(char **cmdline_p)
{
	extern int panic_timeout;
	extern void do_init_bootmem(void);

	calibrate_delay = ppc64_calibrate_delay;

	ppc64_boot_msg(0x12, "Setup Arch");

#ifdef CONFIG_XMON
	if (strstr(cmd_line, "xmon")) {
		/* ensure xmon is enabled */
		xmon_init();
		debugger(0);
	}
#endif /* CONFIG_XMON */

	/*
	 * Set cache line size based on type of cpu as a default.
	 * Systems with OF can look in the properties on the cpu node(s)
	 * for a possibly more accurate value.
	 */
	dcache_bsize = systemcfg->dCacheL1LineSize; 
	icache_bsize = systemcfg->iCacheL1LineSize; 

	/* reboot on panic */
	panic_timeout = 180;

	if (ppc_md.panic)
		notifier_chain_register(&panic_notifier_list, &ppc64_panic_block);

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = klimit;
	
	/* Save unparsed command line copy for /proc/cmdline */
	strlcpy(saved_command_line, cmd_line, sizeof(saved_command_line));
	*cmdline_p = cmd_line;

	irqstack_early_init();

	/* set up the bootmem stuff with available memory */
	do_init_bootmem();

	ppc_md.setup_arch();

	paging_init();
	ppc64_boot_msg(0x15, "Setup Done");
}

/* ToDo: do something useful if ppc_md is not yet setup. */
#define PPC64_LINUX_FUNCTION 0x0f000000
#define PPC64_IPL_MESSAGE 0xc0000000
#define PPC64_TERM_MESSAGE 0xb0000000
#define PPC64_ATTN_MESSAGE 0xa0000000
#define PPC64_DUMP_MESSAGE 0xd0000000

static void ppc64_do_msg(unsigned int src, const char *msg)
{
	if (ppc_md.progress) {
		char buf[32];

		sprintf(buf, "%08x        \n", src);
		ppc_md.progress(buf, 0);
		sprintf(buf, "%-16s", msg);
		ppc_md.progress(buf, 0);
	}
}

/* Print a boot progress message. */
void ppc64_boot_msg(unsigned int src, const char *msg)
{
	ppc64_do_msg(PPC64_LINUX_FUNCTION|PPC64_IPL_MESSAGE|src, msg);
	printk("[boot]%04x %s\n", src, msg);
}

/* Print a termination message (print only -- does not stop the kernel) */
void ppc64_terminate_msg(unsigned int src, const char *msg)
{
	ppc64_do_msg(PPC64_LINUX_FUNCTION|PPC64_TERM_MESSAGE|src, msg);
	printk("[terminate]%04x %s\n", src, msg);
}

/* Print something that needs attention (device error, etc) */
void ppc64_attention_msg(unsigned int src, const char *msg)
{
	ppc64_do_msg(PPC64_LINUX_FUNCTION|PPC64_ATTN_MESSAGE|src, msg);
	printk("[attention]%04x %s\n", src, msg);
}

/* Print a dump progress message. */
void ppc64_dump_msg(unsigned int src, const char *msg)
{
	ppc64_do_msg(PPC64_LINUX_FUNCTION|PPC64_DUMP_MESSAGE|src, msg);
	printk("[dump]%04x %s\n", src, msg);
}

int set_spread_lpevents( char * str )
{
	/* The parameter is the number of processors to share in processing lp events */
	unsigned long i;
	unsigned long val = simple_strtoul( str, NULL, 0 );
	if ( ( val > 0 ) && ( val <= NR_CPUS ) ) {
		for ( i=1; i<val; ++i )
			paca[i].lpQueuePtr = paca[0].lpQueuePtr;
		printk("lpevent processing spread over %ld processors\n", val);
	}
	else
		printk("invalid spreaqd_lpevents %ld\n", val);
	return 1;
}	

/* This should only be called on processor 0 during calibrate decr */
void setup_default_decr(void)
{
	struct paca_struct *lpaca = get_paca();

	if ( decr_overclock_set && !decr_overclock_proc0_set )
		decr_overclock_proc0 = decr_overclock;

	lpaca->default_decr = tb_ticks_per_jiffy / decr_overclock_proc0;	
	lpaca->next_jiffy_update_tb = get_tb() + tb_ticks_per_jiffy;
}

int set_decr_overclock_proc0( char * str )
{
	unsigned long val = simple_strtoul( str, NULL, 0 );
	if ( ( val >= 1 ) && ( val <= 48 ) ) {
		decr_overclock_proc0_set = 1;
		decr_overclock_proc0 = val;
		printk("proc 0 decrementer overclock factor of %ld\n", val);
	}
	else
		printk("invalid proc 0 decrementer overclock factor of %ld\n", val);
	return 1;
}

int set_decr_overclock( char * str )
{
	unsigned long val = simple_strtoul( str, NULL, 0 );
	if ( ( val >= 1 ) && ( val <= 48 ) ) {
		decr_overclock_set = 1;
		decr_overclock = val;
		printk("decrementer overclock factor of %ld\n", val);
	}
	else
		printk("invalid decrementer overclock factor of %ld\n", val);
	return 1;

}

__setup("spread_lpevents=", set_spread_lpevents );
__setup("decr_overclock_proc0=", set_decr_overclock_proc0 );
__setup("decr_overclock=", set_decr_overclock );
