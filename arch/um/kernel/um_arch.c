/* 
 * Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/sched.h"
#include "linux/notifier.h"
#include "linux/mm.h"
#include "linux/types.h"
#include "linux/tty.h"
#include "linux/init.h"
#include "linux/bootmem.h"
#include "linux/spinlock.h"
#include "linux/utsname.h"
#include "linux/sysrq.h"
#include "linux/seq_file.h"
#include "linux/delay.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/ptrace.h"
#include "asm/elf.h"
#include "asm/user.h"
#include "ubd_user.h"
#include "asm/current.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "mprot.h"
#include "mem_user.h"
#include "mem.h"
#include "umid.h"
#include "initrd.h"
#include "init.h"
#include "os.h"

#define DEFAULT_COMMAND_LINE "root=6200"

unsigned long thread_saved_pc(struct task_struct *task)
{
	return(os_process_pc(task->thread.extern_pid));
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	int index;

	index = (struct cpuinfo_um *)v - cpu_data;
#ifdef CONFIG_SMP
	if (!(cpu_online_map & (1 << index)))
		return 0;
#endif

	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		   loops_per_jiffy/(500000/HZ),
		   (loops_per_jiffy/(5000/HZ)) % 100);
	seq_printf(m, "host\t\t: %s\n", host_info);

	return(0);
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? cpu_data + *pos : NULL;
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
	start:	c_start,
	next:	c_next,
	stop:	c_stop,
	show:	show_cpuinfo,
};

pte_t * __bad_pagetable(void)
{
	panic("Someone should implement __bad_pagetable");
	return(NULL);
}

extern void start_kernel(void);

extern int debug;
extern int debug_stop;

static int start_kernel_proc(void *unused)
{
	int pid;

	block_signals();
	pid = os_getpid();

	cpu_tasks[0].pid = pid;
	cpu_tasks[0].task = current;
#ifdef CONFIG_SMP
 	cpu_online_map = 1;
#endif
	if(debug) os_stop_process(pid);
	start_kernel();
	return(0);
}

extern unsigned long high_physmem;

#ifdef CONFIG_HOST_2G_2G
#define TOP 0x80000000
#else
#define TOP 0xc0000000
#endif

#define SIZE ((CONFIG_NEST_LEVEL + CONFIG_KERNEL_HALF_GIGS) * 0x20000000)
#define START (TOP - SIZE)

unsigned long host_task_size;
unsigned long task_size;

void set_task_sizes(int arg)
{
	/* Round up to the nearest 4M */
	host_task_size = ROUND_4M((unsigned long) &arg);
	task_size = START;
}

unsigned long uml_physmem;
unsigned long uml_reserved;

unsigned long start_vm;
unsigned long end_vm;

int ncpus = 1;

static char *argv1_begin = NULL;
static char *argv1_end = NULL;

static int have_root __initdata = 0;
long physmem_size = 32 * 1024 * 1024;

void set_cmdline(char *cmd)
{
	char *umid, *ptr;
	if(honeypot) return;

	umid = get_umid(1);
	if(umid != NULL){
		snprintf(argv1_begin, 
			 (argv1_end - argv1_begin) * sizeof(*ptr), 
			 "(%s)", umid);
		ptr = &argv1_begin[strlen(argv1_begin)];
	}
	else ptr = argv1_begin;

	snprintf(ptr, (argv1_end - ptr) * sizeof(*ptr), " [%s]", cmd);
	memset(argv1_begin + strlen(argv1_begin), '\0', 
	       argv1_end - argv1_begin - strlen(argv1_begin));  
}

static char *usage_string = 
"User Mode Linux v%s\n"
"	available at http://user-mode-linux.sourceforge.net/\n\n";

static int __init uml_version_setup(char *line, int *add)
{
	printf("%s\n", system_utsname.release);
	exit(0);
}

__uml_setup("--version", uml_version_setup,
"--version\n"
"    Prints the version number of the kernel.\n\n"
);

static int __init uml_root_setup(char *line, int *add)
{
	have_root = 1;
	return 0;
}

__uml_setup("root=", uml_root_setup,
"root=<file containing the root fs>\n"
"    This is actually used by the generic kernel in exactly the same\n"
"    way as in any other kernel. If you configure a number of block\n"
"    devices and want to boot off something other than ubd0, you \n"
"    would use something like:\n"
"        root=/dev/ubd5\n\n"
);

#ifdef CONFIG_SMP
static int __init uml_ncpus_setup(char *line, int *add)
{
       if (!sscanf(line, "%d", &ncpus)) {
               printk("Couldn't parse [%s]\n", line);
               return -1;
       }

       return 0;
}

__uml_setup("ncpus=", uml_ncpus_setup,
"ncpus=<# of desired CPUs>\n"
"    This tells an SMP kernel how many virtual processors to start.\n"
"    Currently, this has no effect because SMP isn't enabled.\n\n" 
);
#endif

static int __init Usage(char *line, int *add)
{
 	const char **p;

	printf(usage_string, system_utsname.release);
 	p = &__uml_help_start;
 	while (p < &__uml_help_end) {
 		printf("%s", *p);
 		p++;
 	}
	exit(0);
}

__uml_setup("--help", Usage,
"--help\n"
"    Prints this message.\n\n"
);

static int __init uml_checksetup(char *line, int *add)
{
	struct uml_param *p;

	p = &__uml_setup_start;
	while(p < &__uml_setup_end) {
		int n;

		n = strlen(p->str);
		if(!strncmp(line, p->str, n)){
			if (p->setup_func(line + n, add)) return 1;
		}
		p++;
	}
	return 0;
}

static void __init uml_postsetup(void)
{
	initcall_t *p;

	p = &__uml_postsetup_start;
	while(p < &__uml_postsetup_end){
		(*p)();
		p++;
	}
	return;
}

extern int debug_trace;
unsigned long brk_start;

static struct vm_reserved kernel_vm_reserved;

int linux_main(int argc, char **argv)
{
	unsigned long avail;
	unsigned long virtmem_size;
	unsigned int i, add, err;
	void *sp;

	for (i = 1; i < argc; i++){
		if((i == 1) && (argv[i][0] == ' ')) continue;
		add = 1;
		uml_checksetup(argv[i], &add);
		if(add) add_arg(saved_command_line, argv[i]);
	}
	if(have_root == 0) add_arg(saved_command_line, DEFAULT_COMMAND_LINE);

	if(!jail)
		remap_data(ROUND_DOWN(&_stext), ROUND_UP(&_etext), 1);
	remap_data(ROUND_DOWN(&_sdata), ROUND_UP(&_edata), 1);
	brk_start = (unsigned long) sbrk(0);
	remap_data(ROUND_DOWN(&__bss_start), ROUND_UP(brk_start), 1);

	uml_physmem = START;

	/* Reserve up to 4M after the current brk */
	uml_reserved = ROUND_4M(brk_start) + (1 << 22);

	setup_machinename(system_utsname.machine);

	argv1_begin = argv[1];
	argv1_end = &argv[1][strlen(argv[1])];
  
	set_usable_vm(uml_physmem, get_kmem_end());
	high_physmem = uml_physmem + physmem_size;
	high_memory = (void *) high_physmem;
	setup_physmem(uml_physmem, uml_reserved, physmem_size);

	/* Kernel vm starts after physical memory and is either the size
	 * of physical memory or the remaining space left in the kernel
	 * area of the address space, whichever is smaller.
	 */

	start_vm = VMALLOC_START;
	if(start_vm >= get_kmem_end())
		panic("Physical memory too large to allow any kernel "
		      "virtual memory");

	virtmem_size = physmem_size;
	avail = get_kmem_end() - start_vm;
	if(physmem_size > avail) virtmem_size = avail;
	end_vm = start_vm + virtmem_size;

	if(virtmem_size < physmem_size)
		printk(KERN_INFO "Kernel virtual memory size shrunk to %ld "
		       "bytes\n", virtmem_size);

	err = reserve_vm(high_physmem, end_vm, &kernel_vm_reserved);
	if(err)	panic("Failed to reserve VM area for kernel VM\n");

  	uml_postsetup();

	init_task.thread.kernel_stack = (unsigned long) &init_thread_info + 
		2 * PAGE_SIZE;

	task_protections((unsigned long) &init_thread_info);
	sp = (void *) init_task.thread.kernel_stack + 2 * PAGE_SIZE - 
		sizeof(unsigned long);
	return(signals(start_kernel_proc, sp));
}

static int panic_exit(struct notifier_block *self, unsigned long unused1,
		      void *unused2)
{
#ifdef CONFIG_SYSRQ
	handle_sysrq('p', &current->thread.regs, NULL, NULL);
#endif
	machine_halt();
	return(0);
}

static struct notifier_block panic_exit_notifier = {
	notifier_call :		panic_exit,
	next :			NULL,
	priority :		0
};

void __init setup_arch(char **cmdline_p)
{
	notifier_chain_register(&panic_notifier_list, &panic_exit_notifier);
	paging_init();
 	strcpy(command_line, saved_command_line);
 	*cmdline_p = command_line;
	setup_hostinfo();
}

void __init check_bugs(void)
{
	arch_check_bugs();
	check_ptrace();
	check_sigio();
}

spinlock_t pid_lock = SPIN_LOCK_UNLOCKED;

void lock_pid(void)
{
	spin_lock(&pid_lock);
}

void unlock_pid(void)
{
	spin_unlock(&pid_lock);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
