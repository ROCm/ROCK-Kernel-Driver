/*
 * Machine check handler.
 * K8 parts Copyright 2002,2003 Andi Kleen, SuSE Labs.
 * Rest from unknown author(s). 
 * 2004 Andi Kleen. Rewrote most of it. 
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/rcupdate.h>
#include <linux/kallsyms.h>
#include <linux/sysdev.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <asm/processor.h> 
#include <asm/msr.h>
#include <asm/mce.h>
#include <asm/kdebug.h>
#include <asm/uaccess.h>

#define MISC_MCELOG_MINOR 227

static int mce_disabled __initdata;
/* 0: always panic, 1: panic if deadlock possible, 2: try to avoid panic */ 
static int tolerant = 2;
static int banks;
static unsigned long disabled_banks;

/*
 * Lockless MCE logging infrastructure.
 * This avoids deadlocks on printk locks without having to break locks. Also
 * separate MCEs from kernel messages to avoid bogus bug reports.
 */

struct mce_log mcelog = { 
	MCE_LOG_SIGNATURE,
	MCE_LOG_LEN,
}; 

static void mce_log(struct mce *mce)
{
	unsigned next, entry;
	mce->finished = 0;
	smp_wmb();
	for (;;) {
		entry = mcelog.next;
		read_barrier_depends();
		/* When the buffer fills up discard new entries. Assume 
		   that the earlier errors are the more interesting. */
		if (entry >= MCE_LOG_LEN) {
			set_bit(MCE_OVERFLOW, &mcelog.flags);
			return;
		}
		/* Old left over entry. Skip. */
		if (mcelog.entry[entry].finished)
			continue;
		smp_rmb();
		next = entry + 1;
		if (cmpxchg(&mcelog.next, entry, next) == entry)
			break;
	}
	memcpy(mcelog.entry + entry, mce, sizeof(struct mce));
	smp_wmb();
	mcelog.entry[entry].finished = 1;
	smp_wmb();
}

static void print_mce(struct mce *m)
{
	printk("CPU %d: Machine Check Exception: %16Lx Bank %d: %016Lx\n",
	       m->cpu, m->mcgstatus, m->bank, m->status);
	if (m->rip) {
		printk("RIP %02x:<%016Lx> ", m->cs, m->rip);
		if (m->cs == __KERNEL_CS)
			print_symbol("{%s}", m->rip);
		printk("\n");
	}
	printk("TSC %Lx ", m->tsc); 
	if (m->addr)
		printk("ADDR %Lx ", m->addr);
	if (m->misc)
		printk("MISC %Lx ", m->addr); 	
	printk("\n");
}

static void mce_panic(char *msg, struct mce *backup, unsigned long start)
{ 
	int i;
	oops_begin();
	for (i = 0; i < MCE_LOG_LEN; i++) {
		if (mcelog.entry[i].tsc < start)
			continue;
		print_mce(&mcelog.entry[i]); 
		if (mcelog.entry[i].tsc == backup->tsc)
			backup = NULL;
	}
	if (backup)
		print_mce(backup);
	panic(msg);
} 

static int mce_available(struct cpuinfo_x86 *c)
{
	return !mce_disabled && 
		test_bit(X86_FEATURE_MCE, &c->x86_capability) &&
		test_bit(X86_FEATURE_MCA, &c->x86_capability);
}

/* 
 * The actual machine check handler
 */

void do_machine_check(struct pt_regs * regs, long error_code)
{
	struct mce m;
	int nowayout = 0;
	int kill_it = 0;
	u64 mcestart;
	int i;

	if (regs)
		notify_die(DIE_NMI, "machine check", regs, error_code, 255, SIGKILL);
	if (!banks)
		return;

	memset(&m, 0, sizeof(struct mce));
	m.cpu = hard_smp_processor_id();
	rdmsrl(MSR_IA32_MCG_STATUS, m.mcgstatus);
	if (!regs && (m.mcgstatus & MCG_STATUS_MCIP))
		return;
	if (!(m.mcgstatus & MCG_STATUS_RIPV))
		kill_it = 1;
	if (regs && (m.mcgstatus & MCG_STATUS_EIPV)) {
		m.rip = regs->rip;
		m.cs = regs->cs;
	}
	
	rdtscll(mcestart);
	mb();

	for (i = 0; i < banks; i++) {
		if (test_bit(i, &disabled_banks))
			continue;

		rdmsrl(MSR_IA32_MC0_STATUS + i*4, m.status);
		if ((m.status & MCI_STATUS_VAL) == 0)
			continue;

		nowayout |= (tolerant < 1); 
		nowayout |= !!(m.status & (MCI_STATUS_OVER|MCI_STATUS_PCC));
		kill_it |= !!(m.status & MCI_STATUS_UC);
		m.bank = i;

		if (m.status & MCI_STATUS_MISCV)
			rdmsrl(MSR_IA32_MC0_MISC + i*4, m.misc);
		if (m.status & MCI_STATUS_ADDRV)
			rdmsrl(MSR_IA32_MC0_MISC + i*4, m.addr);

		rdtscll(m.tsc);
		wrmsrl(MSR_IA32_MC0_STATUS + i*4, 0);
		mce_log(&m);
	}
	wrmsrl(MSR_IA32_MCG_STATUS, 0);

	/* Never do anything final in the polling timer */
	if (!regs)
		return;
	if (nowayout)
		mce_panic("Machine check", &m, mcestart);
	if (kill_it) {
		int user_space = (m.rip && (m.cs & 3));
		
		/* When the machine was in user space and the CPU didn't get
		   confused it's normally not necessary to panic, unless you are 
		   paranoid (tolerant == 0) */ 
		if (!user_space && (panic_on_oops || tolerant < 2))
			mce_panic("Uncorrected machine check in kernel", &m, mcestart);

		/* do_exit takes an awful lot of locks and has as slight risk 
		   of deadlocking. If you don't want that don't set tolerant >= 2 */
		do_exit(SIGBUS);
	}
}

static void mce_clear_all(void)
{
	int i;
	for (i = 0; i < banks; i++)
		wrmsrl(MSR_IA32_MC0_STATUS + i*4, 0);
	wrmsrl(MSR_IA32_MCG_STATUS, 0);
}

/*
 * Periodic polling timer for "silent" machine check errors.
 */

static int check_interval = 3600; /* one hour */
static void mcheck_timer(void *data);
static DECLARE_WORK(mcheck_work, mcheck_timer, NULL);

static void mcheck_check_cpu(void *info)
{
	if (mce_available(&current_cpu_data))
		do_machine_check(NULL, 0);
}

static void mcheck_timer(void *data)
{
	on_each_cpu(mcheck_check_cpu, NULL, 1, 1);
	schedule_delayed_work(&mcheck_work, check_interval * HZ);
}


static __init int periodic_mcheck_init(void)
{ 
	if (check_interval)
		schedule_delayed_work(&mcheck_work, check_interval*HZ);
	return 0;
} 
__initcall(periodic_mcheck_init);


/* 
 * Initialize Machine Checks for a CPU.
 */
static void mce_init(void *dummy)
{
	u64 cap;
	int i;

	rdmsrl(MSR_IA32_MCG_CAP, cap);
	if (cap & MCG_CTL_P)
		wrmsr(MSR_IA32_MCG_CTL, 0xffffffff, 0xffffffff);

	banks = cap & 0xff;

	mce_clear_all(); 
	for (i = 0; i < banks; i++) {
		u64 val = test_bit(i, &disabled_banks) ? 0 : ~0UL;
		wrmsrl(MSR_IA32_MC0_CTL+4*i, val);
		wrmsrl(MSR_IA32_MC0_STATUS+4*i, 0);
	}	

	set_in_cr4(X86_CR4_MCE);
}

/* 
 * Called for each booted CPU to set up machine checks.
 * Must be called with preempt off. 
 */
void __init mcheck_init(struct cpuinfo_x86 *c)
{
	static unsigned long mce_cpus __initdata = 0;

	if (test_and_set_bit(smp_processor_id(), &mce_cpus) || !mce_available(c))
		return;

	mce_init(NULL);
}

/*
 * Character device to read and clear the MCE log.
 */

static void collect_tscs(void *data) 
{ 
	unsigned long *cpu_tsc = (unsigned long *)data;
	rdtscll(cpu_tsc[smp_processor_id()]);
} 

static ssize_t mce_read(struct file *filp, char *ubuf, size_t usize, loff_t *off)
{
	unsigned long cpu_tsc[NR_CPUS];
	static DECLARE_MUTEX(mce_read_sem);
	unsigned next;
	char *buf = ubuf;
	int i, err;

	down(&mce_read_sem); 
	next = mcelog.next;
	read_barrier_depends();
		
	/* Only supports full reads right now */
	if (*off != 0 || usize < MCE_LOG_LEN*sizeof(struct mce)) { 
		up(&mce_read_sem);
		return -EINVAL;
	}

	err = 0;
	for (i = 0; i < next; i++) {
		if (!mcelog.entry[i].finished)
			continue;
		smp_rmb();
		err |= copy_to_user(buf, mcelog.entry + i, sizeof(struct mce));
		buf += sizeof(struct mce); 
	} 

	memset(mcelog.entry, 0, next * sizeof(struct mce));
	mcelog.next = 0;
	smp_wmb(); 
	
	synchronize_kernel();	

	/* Collect entries that were still getting written before the synchronize. */

	on_each_cpu(collect_tscs, cpu_tsc, 1, 1);
	for (i = next; i < MCE_LOG_LEN; i++) { 
		if (mcelog.entry[i].finished && 
		    mcelog.entry[i].tsc < cpu_tsc[mcelog.entry[i].cpu]) {  
			err |= copy_to_user(buf, mcelog.entry+i, sizeof(struct mce));
			smp_rmb();
			buf += sizeof(struct mce);
			memset(&mcelog.entry[i], 0, sizeof(struct mce));
		}
	} 	
	up(&mce_read_sem);
	return err ? -EFAULT : buf - ubuf; 
}

static int mce_ioctl(struct inode *i, struct file *f,unsigned int cmd, unsigned long arg)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM; 
	switch (cmd) {
	case MCE_GET_RECORD_LEN: 
		return put_user(sizeof(struct mce), (int *)arg);
	case MCE_GET_LOG_LEN:
		return put_user(MCE_LOG_LEN, (int *)arg);		
	case MCE_GETCLEAR_FLAGS: {
		unsigned flags;
		do { 
			flags = mcelog.flags;
		} while (cmpxchg(&mcelog.flags, flags, 0) != flags); 
		return put_user(flags, (int *)arg); 
	}
	default:
		return -ENOTTY; 
	} 
}

#if 0 /* for testing */
static ssize_t mce_write(struct file *f, const char __user *buf, size_t sz, loff_t *off)
{
	struct mce m;
	if (sz != sizeof(struct mce))
		return -EINVAL;
	copy_from_user(&m, buf, sizeof(struct mce));
	m.finished = 0;
	mce_log(&m);
	return sizeof(struct mce);
}
#endif

static struct file_operations mce_chrdev_ops = {
	.read = mce_read,
	.ioctl = mce_ioctl,
	//.write = mce_write
};

static struct miscdevice mce_log_device = {
	MISC_MCELOG_MINOR,
	"mcelog",
	&mce_chrdev_ops,
};

/* 
 * Old style boot options parsing. Only for compatibility. 
 */

static int __init mcheck_disable(char *str)
{
	mce_disabled = 1;
	return 0;
}

/* mce=off disable machine check */
static int __init mcheck_enable(char *str)
{
	if (!strcmp(str, "off"))
		mce_disabled = 1;
	else
		printk("mce= argument %s ignored. Please use /sys", str); 
	return 0;
}

__setup("nomce", mcheck_disable);
__setup("mce", mcheck_enable);

/* 
 * Sysfs support
 */ 

/* On resume clear all MCE state. Don't want to see leftovers from the BIOS. */
static int mce_resume(struct sys_device *dev)
{
	mce_clear_all();
	on_each_cpu(mce_init, NULL, 1, 1);
	return 0;
}

/* Reinit MCEs after user configuration changes */
static void mce_restart(void) 
{ 
	if (check_interval)
		cancel_delayed_work(&mcheck_work);
	/* Timer race is harmless here */
	on_each_cpu(mce_init, NULL, 1, 1);       
	if (check_interval)
		schedule_delayed_work(&mcheck_work, check_interval*HZ);
}

static struct sysdev_class mce_sysclass = {
	.resume = mce_resume,
	set_kset_name("machinecheck"),
};

static struct sys_device device_mce = {
	.id	= 0,
	.cls	= &mce_sysclass,
};

/* Why are there no generic functions for this? */
#define ACCESSOR(name, start) \
	static ssize_t show_ ## name(struct sys_device *s, char *buf) { 	   	   \
		return sprintf(buf, "%lu\n", (unsigned long)name);		   \
	} 									   \
	static ssize_t set_ ## name(struct sys_device *s,const char *buf,size_t siz) { \
		char *end; 							   \
		unsigned long new = simple_strtoul(buf, &end, 0); 		   \
		if (end == buf) return -EINVAL;					   \
		name = new;							   \
		start; 								   \
		return end-buf;		     					   \
	}									   \
	static SYSDEV_ATTR(name, 0644, show_ ## name, set_ ## name);

ACCESSOR(disabled_banks,mce_restart())
ACCESSOR(tolerant,)
ACCESSOR(check_interval,mce_restart())

static __init int mce_init_device(void)
{
	int err;
	if (!mce_available(&boot_cpu_data))
		return -EIO;
	err = sysdev_class_register(&mce_sysclass);
	if (!err)
		err = sysdev_register(&device_mce);
	if (!err) { 
		/* could create per CPU objects, but is not worth it. */
		sysdev_create_file(&device_mce, &attr_disabled_banks); 
		sysdev_create_file(&device_mce, &attr_tolerant); 
		sysdev_create_file(&device_mce, &attr_check_interval);
	} 
	
	misc_register(&mce_log_device);
	return err;

}
device_initcall(mce_init_device);
