/*
 *	Intel CPU Microcode Update driver for Linux
 *
 *	Copyright (C) 2000 Tigran Aivazian
 *
 *	This driver allows to upgrade microcode on Intel processors
 *	belonging to IA-32 family - PentiumPro, Pentium II, 
 *	Pentium III, Xeon, Pentium 4, etc.
 *
 *	Reference: Section 8.10 of Volume III, Intel Pentium 4 Manual, 
 *	Order Number 245472 or free download from:
 *		
 *	http://developer.intel.com/design/pentium4/manuals/245472.htm
 *
 *	For more information, go to http://www.urbanmyth.org/microcode
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	1.0	16 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Initial release.
 *	1.01	18 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Added read() support + cleanups.
 *	1.02	21 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Added 'device trimming' support. open(O_WRONLY) zeroes
 *		and frees the saved copy of applied microcode.
 *	1.03	29 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Made to use devfs (/dev/cpu/microcode) + cleanups.
 *	1.04	06 Jun 2000, Simon Trimmer <simon@veritas.com>
 *		Added misc device support (now uses both devfs and misc).
 *		Added MICROCODE_IOCFREE ioctl to clear memory.
 *	1.05	09 Jun 2000, Simon Trimmer <simon@veritas.com>
 *		Messages for error cases (non intel & no suitable microcode).
 *	1.06	03 Aug 2000, Tigran Aivazian <tigran@veritas.com>
 *		Removed ->release(). Removed exclusive open and status bitmap.
 *		Added microcode_rwsem to serialize read()/write()/ioctl().
 *		Removed global kernel lock usage.
 *	1.07	07 Sep 2000, Tigran Aivazian <tigran@veritas.com>
 *		Write 0 to 0x8B msr and then cpuid before reading revision,
 *		so that it works even if there were no update done by the
 *		BIOS. Otherwise, reading from 0x8B gives junk (which happened
 *		to be 0 on my machine which is why it worked even when I
 *		disabled update by the BIOS)
 *		Thanks to Eric W. Biederman <ebiederman@lnxi.com> for the fix.
 *	1.08	11 Dec 2000, Richard Schaal <richard.schaal@intel.com> and
 *			     Tigran Aivazian <tigran@veritas.com>
 *		Intel Pentium 4 processor support and bugfixes.
 *	1.09	30 Oct 2001, Tigran Aivazian <tigran@veritas.com>
 *		Bugfix for HT (Hyper-Threading) enabled processors
 *		whereby processor resources are shared by all logical processors
 *		in a single CPU package.
 *	1.10	28 Feb 2002 Asit K Mallick <asit.k.mallick@intel.com> and
 *		Tigran Aivazian <tigran@veritas.com>,
 *		Serialize updates as required on HT processors due to speculative
 *		nature of implementation.
 *	1.11	22 Mar 2001 Tigran Aivazian <tigran@veritas.com>
 *		Fix the panic when writing zero-length microcode chunk.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/spinlock.h>

#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/processor.h>


static spinlock_t microcode_update_lock = SPIN_LOCK_UNLOCKED;

#define MICROCODE_VERSION 	"1.11"

MODULE_DESCRIPTION("Intel CPU (IA-32) microcode update driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@veritas.com>");
MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;

#define MICRO_DEBUG 0

#if MICRO_DEBUG
#define printf(x...) printk(##x)
#else
#define printf(x...)
#endif

/* VFS interface */
static int microcode_open(struct inode *, struct file *);
static ssize_t microcode_read(struct file *, char *, size_t, loff_t *);
static ssize_t microcode_write(struct file *, const char *, size_t, loff_t *);
static int microcode_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

static int do_microcode_update(void);
static void do_update_one(void *);

/* read()/write()/ioctl() are serialized on this */
static DECLARE_RWSEM(microcode_rwsem);

static struct microcode *microcode; /* array of 2048byte microcode blocks */
static unsigned int microcode_num;  /* number of chunks in microcode */
static char *mc_applied;            /* array of applied microcode blocks */
static unsigned int mc_fsize;       /* file size of /dev/cpu/microcode */

/* we share file_operations between misc and devfs mechanisms */
static struct file_operations microcode_fops = {
	owner:		THIS_MODULE,
	read:		microcode_read,
	write:		microcode_write,
	ioctl:		microcode_ioctl,
	open:		microcode_open,
};

static struct miscdevice microcode_dev = {
	minor: MICROCODE_MINOR,
	name:	"microcode",
	fops:	&microcode_fops,
};

static devfs_handle_t devfs_handle;

static int __init microcode_init(void)
{
	int error;

	error = misc_register(&microcode_dev);
	if (error)
		printk(KERN_WARNING 
			"microcode: can't misc_register on minor=%d\n",
			MICROCODE_MINOR);

	devfs_handle = devfs_register(NULL, "cpu/microcode",
			DEVFS_FL_DEFAULT, 0, 0, S_IFREG | S_IRUSR | S_IWUSR, 
			&microcode_fops, NULL);
	if (devfs_handle == NULL && error) {
		printk(KERN_ERR "microcode: failed to devfs_register()\n");
		goto out;
	}
	error = 0;
	printk(KERN_INFO 
		"IA-32 Microcode Update Driver: v%s <tigran@veritas.com>\n", 
		MICROCODE_VERSION);

out:
	return error;
}

static void __exit microcode_exit(void)
{
	misc_deregister(&microcode_dev);
	devfs_unregister(devfs_handle);
	if (mc_applied)
		kfree(mc_applied);
	printk(KERN_INFO "IA-32 Microcode Update Driver v%s unregistered\n", 
			MICROCODE_VERSION);
}

module_init(microcode_init)
module_exit(microcode_exit)

static int microcode_open(struct inode *unused1, struct file *unused2)
{
	return capable(CAP_SYS_RAWIO) ? 0 : -EPERM;
}

/*
 * update_req[cpu].err is set to 1 if update failed on 'cpu', 0 otherwise
 * if err==0, microcode[update_req[cpu].slot] points to applied block of microcode
 */
struct update_req {
	int err;
	int slot;
} update_req[NR_CPUS];

static int do_microcode_update(void)
{
	int i, error = 0, err;
	struct microcode *m;

	if (smp_call_function(do_update_one, NULL, 1, 1) != 0) {
		printk(KERN_ERR "microcode: IPI timeout, giving up\n");
		return -EIO;
	}
	do_update_one(NULL);

	for (i=0; i<smp_num_cpus; i++) {
		err = update_req[i].err;
		error += err;
		if (!err) {
			m = (struct microcode *)mc_applied + i;
			memcpy(m, &microcode[update_req[i].slot], sizeof(struct microcode));
		}
	}
	return error;
}

static void do_update_one(void *unused)
{
	int cpu_num = smp_processor_id();
	struct cpuinfo_x86 *c = cpu_data + cpu_num;
	struct update_req *req = update_req + cpu_num;
	unsigned int pf = 0, val[2], rev, sig;
	unsigned long flags;
	int i;

	req->err = 1; /* assume update will fail on this cpu */

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
		test_bit(X86_FEATURE_IA64, &c->x86_capability)){
		printk(KERN_ERR "microcode: CPU%d not a capable Intel processor\n", cpu_num);
		return;
	}

	sig = c->x86_mask + (c->x86_model<<4) + (c->x86<<8);

	if ((c->x86_model >= 5) || (c->x86 > 6)) {
		/* get processor flags from MSR 0x17 */
		rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		pf = 1 << ((val[1] >> 18) & 7);
	}

	for (i=0; i<microcode_num; i++)
		if (microcode[i].sig == sig && microcode[i].pf == pf &&
		    microcode[i].ldrver == 1 && microcode[i].hdrver == 1) {
			int sum = 0;
			struct microcode *m = &microcode[i];
			unsigned int *sump = (unsigned int *)(m+1);

			printf("Microcode\n");
			printf("   Header Revision %d\n",microcode[i].hdrver);
			printf("   Date %x/%x/%x\n",
				((microcode[i].date >> 24 ) & 0xff),
				((microcode[i].date >> 16 ) & 0xff),
				(microcode[i].date & 0xFFFF));
			printf("   Type %x Family %x Model %x Stepping %x\n",
				((microcode[i].sig >> 12) & 0x3),
				((microcode[i].sig >> 8) & 0xf),
				((microcode[i].sig >> 4) & 0xf),
				((microcode[i].sig & 0xf)));
			printf("   Checksum %x\n",microcode[i].cksum);
			printf("   Loader Revision %x\n",microcode[i].ldrver);
			printf("   Processor Flags %x\n\n",microcode[i].pf);

			req->slot = i;

			/* serialize access to update decision */
			spin_lock_irqsave(&microcode_update_lock, flags);          

			/* trick, to work even if there was no prior update by the BIOS */
			wrmsr(MSR_IA32_UCODE_REV, 0, 0);
			__asm__ __volatile__ ("cpuid" : : : "ax", "bx", "cx", "dx");

			/* get current (on-cpu) revision into rev (ignore val[0]) */
			rdmsr(MSR_IA32_UCODE_REV, val[0], rev);
			
			if (microcode[i].rev < rev) {
				spin_unlock_irqrestore(&microcode_update_lock, flags);
				printk(KERN_ERR 
				       "microcode: CPU%d not 'upgrading' to earlier revision"
				       " %d (current=%d)\n", cpu_num, microcode[i].rev, rev);
				return;
			} else if (microcode[i].rev == rev) {
				/* notify the caller of success on this cpu */
				req->err = 0;
				spin_unlock_irqrestore(&microcode_update_lock, flags);
				printk(KERN_ERR 
					"microcode: CPU%d already at revision"
					" %d (current=%d)\n", cpu_num, microcode[i].rev, rev);
				return;
			}

			/* Verify the checksum */
			while (--sump >= (unsigned int *)m)
				sum += *sump;
			if (sum != 0) {
				req->err = 1;
				spin_unlock_irqrestore(&microcode_update_lock, flags);
				printk(KERN_ERR "microcode: CPU%d aborting, "
				       "bad checksum\n", cpu_num);
				return;
			}
			
			/* write microcode via MSR 0x79 */
			wrmsr(MSR_IA32_UCODE_WRITE, (unsigned int)(m->bits), 0);

			/* serialize */
			__asm__ __volatile__ ("cpuid" : : : "ax", "bx", "cx", "dx");

			/* get the current revision from MSR 0x8B */
			rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);

			/* notify the caller of success on this cpu */
			req->err = 0;
			spin_unlock_irqrestore(&microcode_update_lock, flags);
			printk(KERN_INFO "microcode: CPU%d updated from revision "
			       "%d to %d, date=%08x\n", 
			       cpu_num, rev, val[1], microcode[i].date);
			return;
		}
	
	printk(KERN_ERR
	       "microcode: CPU%d no microcode found! (sig=%x, pflags=%d)\n", 
	       cpu_num, sig, pf);
}


static ssize_t microcode_read(struct file *file, char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret = 0;

	down_read(&microcode_rwsem);
	if (*ppos >= mc_fsize)
		goto out;
	if (*ppos + len > mc_fsize)
		len = mc_fsize - *ppos;
	ret = -EFAULT;
	if (copy_to_user(buf, mc_applied + *ppos, len))
		goto out;
	*ppos += len;
	ret = len;
out:
	up_read(&microcode_rwsem);
	return ret;
}

static ssize_t microcode_write(struct file *file, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret;

	if (!len || len % sizeof(struct microcode) != 0) {
		printk(KERN_ERR "microcode: can only write in N*%d bytes units\n", 
			sizeof(struct microcode));
		return -EINVAL;
	}
	if ((len >> PAGE_SHIFT) > num_physpages) {
		printk(KERN_ERR "microcode: too much data (max %ld pages)\n", num_physpages);
		return -EINVAL;
	}
	down_write(&microcode_rwsem);
	if (!mc_applied) {
		mc_applied = kmalloc(smp_num_cpus*sizeof(struct microcode),
				GFP_KERNEL);
		if (!mc_applied) {
			up_write(&microcode_rwsem);
			printk(KERN_ERR "microcode: out of memory for saved microcode\n");
			return -ENOMEM;
		}
	}
	
	microcode_num = len/sizeof(struct microcode);
	microcode = vmalloc(len);
	if (!microcode) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	if (copy_from_user(microcode, buf, len)) {
		ret = -EFAULT;
		goto out_fsize;
	}

	if(do_microcode_update()) {
		ret = -EIO;
		goto out_fsize;
	} else {
		mc_fsize = smp_num_cpus * sizeof(struct microcode);
		ret = (ssize_t)len;
	}
out_fsize:
	devfs_set_file_size(devfs_handle, mc_fsize);
	vfree(microcode);
out_unlock:
	up_write(&microcode_rwsem);
	return ret;
}

static int microcode_ioctl(struct inode *inode, struct file *file, 
		unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
		case MICROCODE_IOCFREE:
			down_write(&microcode_rwsem);
			if (mc_applied) {
				int bytes = smp_num_cpus * sizeof(struct microcode);

				devfs_set_file_size(devfs_handle, 0);
				kfree(mc_applied);
				mc_applied = NULL;
				printk(KERN_INFO "microcode: freed %d bytes\n", bytes);
				mc_fsize = 0;
				up_write(&microcode_rwsem);
				return 0;
			}
			up_write(&microcode_rwsem);
			return -ENODATA;

		default:
			printk(KERN_ERR "microcode: unknown ioctl cmd=%d\n", cmd);
			return -EINVAL;
	}
	return -EINVAL;
}
