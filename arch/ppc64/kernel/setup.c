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
#include <asm/sections.h>

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

extern void iSeries_init( void );
extern void iSeries_init_early( void );
extern void pSeries_init_early( void );
extern void pSeriesLP_init_early(void);
extern void mm_init_ppc64( void ); 

unsigned long decr_overclock = 1;
unsigned long decr_overclock_proc0 = 1;
unsigned long decr_overclock_set = 0;
unsigned long decr_overclock_proc0_set = 0;

#ifdef CONFIG_XMON
extern void xmon_map_scc(void);
#endif

char saved_command_line[256];
unsigned char aux_device_present;

void parse_cmd_line(unsigned long r3, unsigned long r4, unsigned long r5,
		    unsigned long r6, unsigned long r7);
int parse_bootinfo(void);

#ifdef CONFIG_MAGIC_SYSRQ
unsigned long SYSRQ_KEY;
#endif /* CONFIG_MAGIC_SYSRQ */

struct machdep_calls ppc_md;

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
 * Do some initial setup of the system.  The paramters are those which 
 * were passed in from the bootloader.
 */
void setup_system(unsigned long r3, unsigned long r4, unsigned long r5,
		  unsigned long r6, unsigned long r7)
{
#ifdef CONFIG_XMON_DEFAULT
	debugger = xmon;
	debugger_bpt = xmon_bpt;
	debugger_sstep = xmon_sstep;
	debugger_iabr_match = xmon_iabr_match;
	debugger_dabr_match = xmon_dabr_match;
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
#ifdef CONFIG_BLK_DEV_INITRD
		initrd_start = initrd_end = 0;
#endif
		parse_bootinfo();
		break;

	case PLATFORM_PSERIES_LPAR:
		pSeriesLP_init_early();
#ifdef CONFIG_BLK_DEV_INITRD
		initrd_start = initrd_end = 0;
#endif
		parse_bootinfo();
		break;
#endif
	}

	if (systemcfg->platform & PLATFORM_PSERIES) {
		early_console_initialized = 1;
		register_console(&udbg_console);
	}

	printk("Starting Linux PPC64 %s\n", UTS_RELEASE);

	printk("-----------------------------------------------------\n");
	printk("naca                          = 0x%p\n", naca);
	printk("naca->pftSize                 = 0x%lx\n", naca->pftSize);
	printk("naca->debug_switch            = 0x%lx\n", naca->debug_switch);
	printk("naca->interrupt_controller    = 0x%ld\n", naca->interrupt_controller);
	printk("systemcfg                      = 0x%p\n", systemcfg);
	printk("systemcfg->processorCount     = 0x%lx\n", systemcfg->processorCount);
	printk("systemcfg->physicalMemorySize = 0x%lx\n", systemcfg->physicalMemorySize);
	printk("systemcfg->dCacheL1LineSize   = 0x%x\n", systemcfg->dCacheL1LineSize);
	printk("systemcfg->iCacheL1LineSize   = 0x%x\n", systemcfg->iCacheL1LineSize);
	printk("htab_data.htab                = 0x%p\n", htab_data.htab);
	printk("htab_data.num_ptegs           = 0x%lx\n", htab_data.htab_num_ptegs);
	printk("-----------------------------------------------------\n");

	if (systemcfg->platform & PLATFORM_PSERIES) {
		finish_device_tree();
		chrp_init(r3, r4, r5, r6, r7);
	}

	mm_init_ppc64();

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
	ppc_md.restart(cmd);
}

EXPORT_SYMBOL(machine_restart);
  
void machine_power_off(void)
{
	ppc_md.power_off();
}

EXPORT_SYMBOL(machine_power_off);
  
void machine_halt(void)
{
	ppc_md.halt();
}

EXPORT_SYMBOL(machine_halt);

unsigned long ppc_proc_freq;
unsigned long ppc_tb_freq;

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long cpu_id = (unsigned long)v - 1;
	unsigned int pvr;
	unsigned short maj;
	unsigned short min;

	if (cpu_id == NR_CPUS) {

		if (ppc_md.get_cpuinfo != NULL)
			ppc_md.get_cpuinfo(m);

		return 0;
	}

	if (!cpu_online(cpu_id))
		return 0;

#ifdef CONFIG_SMP
	pvr = paca[cpu_id].pvr;
#else
	pvr = _get_PVR();
#endif
	maj = (pvr >> 8) & 0xFF;
	min = pvr & 0xFF;

	seq_printf(m, "processor\t: %lu\n", cpu_id);
	seq_printf(m, "cpu\t\t: ");

	switch (PVR_VER(pvr)) {
	case PV_NORTHSTAR:
		seq_printf(m, "RS64-II (northstar)\n");
		break;
	case PV_PULSAR:
		seq_printf(m, "RS64-III (pulsar)\n");
		break;
	case PV_POWER4:
		seq_printf(m, "POWER4 (gp)\n");
		break;
	case PV_ICESTAR:
		seq_printf(m, "RS64-III (icestar)\n");
		break;
	case PV_SSTAR:
		seq_printf(m, "RS64-IV (sstar)\n");
		break;
	case PV_POWER4p:
		seq_printf(m, "POWER4+ (gq)\n");
		break;
	case PV_630:
		seq_printf(m, "POWER3 (630)\n");
		break;
	case PV_630p:
		seq_printf(m, "POWER3 (630+)\n");
		break;
	default:
		seq_printf(m, "Unknown (%08x)\n", pvr);
		break;
	}

	/*
	 * Assume here that all clock rates are the same in a
	 * smp system.  -- Cort
	 */
	if (systemcfg->platform != PLATFORM_ISERIES_LPAR) {
		struct device_node *cpu_node;
		int *fp;

		cpu_node = find_type_devices("cpu");
		if (cpu_node) {
			fp = (int *) get_property(cpu_node, "clock-frequency",
						  NULL);
			if (fp)
				seq_printf(m, "clock\t\t: %dMHz\n",
					   *fp / 1000000);
		}
	}

	if (ppc_md.setup_residual != NULL)
		ppc_md.setup_residual(m, cpu_id);

	seq_printf(m, "revision\t: %hd.%hd\n\n", maj, min);
	
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
 * Fetch the cmd_line from open firmware. */
void parse_cmd_line(unsigned long r3, unsigned long r4, unsigned long r5,
		  unsigned long r6, unsigned long r7)
{
	struct device_node *chosen;
	char *p;

#ifdef CONFIG_BLK_DEV_INITRD
	if ((initrd_start == 0) && r3 && r4 && r4 != 0xdeadbeef) {
		initrd_start = (r3 >= KERNELBASE) ? r3 : (unsigned long)__va(r3);
		initrd_end = initrd_start + r4;
		ROOT_DEV = Root_RAM0;
		initrd_below_start_ok = 1;
	}
#endif

	cmd_line[0] = 0;

#ifdef CONFIG_CMDLINE
	strlcpy(cmd_line, CONFIG_CMDLINE, sizeof(cmd_line));
#endif /* CONFIG_CMDLINE */

	chosen = find_devices("chosen");
	if (chosen != NULL) {
		p = get_property(chosen, "bootargs", NULL);
		if (p != NULL && p[0] != 0)
			strlcpy(cmd_line, p, sizeof(cmd_line));
	}

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


char *bi_tag2str(unsigned long tag)
{
	switch (tag) {
	case BI_FIRST:
		return "BI_FIRST";
	case BI_LAST:
		return "BI_LAST";
	case BI_CMD_LINE:
		return "BI_CMD_LINE";
	case BI_BOOTLOADER_ID:
		return "BI_BOOTLOADER_ID";
	case BI_INITRD:
		return "BI_INITRD";
	case BI_SYSMAP:
		return "BI_SYSMAP";
	case BI_MACHTYPE:
		return "BI_MACHTYPE";
	default:
		return "BI_UNKNOWN";
	}
}

int parse_bootinfo(void)
{
	struct bi_record *rec;
	extern char *sysmap;
	extern unsigned long sysmap_size;

	rec = prom.bi_recs;

	if ( rec == NULL || rec->tag != BI_FIRST )
		return -1;

	for ( ; rec->tag != BI_LAST ; rec = bi_rec_next(rec) ) {
		switch (rec->tag) {
		case BI_CMD_LINE:
			memcpy(cmd_line, (void *)rec->data, rec->size);
			break;
		case BI_SYSMAP:
			sysmap = (char *)((rec->data[0] >= (KERNELBASE))
					? rec->data[0] : (unsigned long)__va(rec->data[0]));
			sysmap_size = rec->data[1];
			break;
#ifdef CONFIG_BLK_DEV_INITRD
		case BI_INITRD:
			initrd_start = (unsigned long)__va(rec->data[0]);
			initrd_end = initrd_start + rec->data[1];
			ROOT_DEV = Root_RAM0;
			initrd_below_start_ok = 1;
			break;
#endif /* CONFIG_BLK_DEV_INITRD */
		}
	}

	return 0;
}

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
extern void sort_exception_table(void);

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
	xmon_map_scc();
	if (strstr(cmd_line, "xmon"))
		xmon(0);
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

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = klimit;
	
	/* Save unparsed command line copy for /proc/cmdline */
	strcpy(saved_command_line, cmd_line);
	*cmdline_p = cmd_line;

	/* set up the bootmem stuff with available memory */
	do_init_bootmem();

	ppc_md.setup_arch();

	paging_init();
	sort_exception_table();
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
