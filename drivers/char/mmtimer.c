/*
 * Intel Multimedia Timer device implementation for SGI SN platforms.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2003 Silicon Graphics, Inc.  All rights reserved.
 *
 * This driver implements a subset of the interface required by the
 * IA-PC Multimedia Timers Draft Specification (rev. 0.97) from Intel.
 *
 * 11/01/01 - jbarnes - initial revision
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/mmtimer.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <asm/sn/addrs.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/mmtimer_private.h>

#undef MMTIMER_INTERRUPT_SUPPORT

MODULE_AUTHOR("Jesse Barnes <jbarnes@sgi.com>");
MODULE_DESCRIPTION("Multimedia timer support");
MODULE_LICENSE("GPL");

static int mmtimer_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
static int mmtimer_mmap(struct file *file, struct vm_area_struct *vma);

/*
 * Period in femtoseconds (10^-15 s)
 */
static unsigned long mmtimer_femtoperiod = 0;

static struct file_operations mmtimer_fops = {
	owner:	THIS_MODULE,
	mmap:	mmtimer_mmap,
	ioctl:	mmtimer_ioctl,
};

/*
 * Comparators and their associated info.  Bedrock has
 * two comparison registers.
 */
#ifdef MMTIMER_INTERRUPT_SUPPORT
static mmtimer_t timers[] = { { SPIN_LOCK_UNLOCKED, 0, 0,
				(unsigned long *)RTC_COMPARE_A_ADDR, 0 }, 
			      { SPIN_LOCK_UNLOCKED, 0, 0,
				(unsigned long *)RTC_COMPARE_B_ADDR, 0 } };
#endif

/**
 * mmtimer_ioctl - ioctl interface for /dev/mmtimer
 * @inode: inode of the device
 * @file: file structure for the device
 * @cmd: command to execute
 * @arg: optional argument to command
 *
 * Executes the command specified by @cmd.  Returns 0 for success, <0 for failure.
 * Valid commands are
 *
 * %MMTIMER_GETOFFSET - Should return the offset (relative to the start 
 * of the page where the registers are mapped) for the counter in question.
 *
 * %MMTIMER_GETRES - Returns the resolution of the clock in femto (10^-15) 
 * seconds
 *
 * %MMTIMER_GETFREQ - Copies the frequency of the clock in Hz to the address
 * specified by @arg
 *
 * %MMTIMER_GETBITS - Returns the number of bits in the clock's counter
 *
 * %MMTIMER_GETNUM - Returns the umber of comparators available
 *
 * %MMTIMER_MMAPAVAIL - Returns 1 if the registers can be mmap'd into userspace
 *
 * %MMTIMER_SETPERIODIC - Sets the comparator in question to the value specified.
 * The interrupt handler will add the value specified to the comparator after a 
 * match.  In this case, @arg is the address of a struct mmtimer_alarm.
 *
 * %MMTIMER_SETONESHOT - Like the above, but the comparator is not updated 
 * after the match.  @arg is also the same as above.
 *
 * %MMTIMER_GETCOUNTER - Gets the current value in the counter and places it
 * in the address specified by @arg.
 */
static int
mmtimer_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
#ifdef MMTIMER_INTERRUPT_SUPPORT
	mmtimer_alarm_t alm;
	unsigned long flags;
#endif

	switch (cmd) {
	case MMTIMER_GETOFFSET:	/* offset of the counter */
		/*
		 * SN RTC registers are on their own 64k page
		 */
		if(PAGE_SIZE <= (1 << 16))
			ret = (((long)RTC_COUNTER_ADDR) & (PAGE_SIZE-1)) / 8;
		else
			ret = -ENOSYS;
		break;

	case MMTIMER_GETRES: /* resolution of the clock in 10^-15 s */
		if(copy_to_user((unsigned long *)arg, &mmtimer_femtoperiod, sizeof(unsigned long)))
			return -EFAULT;
		break;

	case MMTIMER_GETFREQ: /* frequency in Hz */
		if(copy_to_user((unsigned long *)arg, &sn_rtc_cycles_per_second, sizeof(unsigned long)))
			return -EFAULT;
		ret = 0;
		break;

	case MMTIMER_GETBITS: /* number of bits in the clock */
		ret = RTC_BITS;
		break;

	case MMTIMER_GETNUM: /* number of comparators available */
		ret = 1;
		break;

	case MMTIMER_MMAPAVAIL: /* can we mmap the clock into userspace? */
		ret = (PAGE_SIZE <= (1 << 16)) ? 1 : 0;
		break;

	case MMTIMER_SETPERIODIC: /* set a periodically signalling timer */
#ifdef MMTIMER_INTERRUPT_SUPPORT
		if(copy_from_user(&alm, (mmtimer_alarm_t *)arg, sizeof(mmtimer_alarm_t)))
			return -EFAULT;
		if(alm.id < 0 || alm.id > NUM_COMPARATORS) {
			if(timers[alm.id].process) {
				ret = -EBUSY;
			}
			else {
				spin_lock_irqsave(&timers[alm.id].timer_lock, flags);
				timers[alm.id].periodic = 1;
				*(timers[alm.id].compare) = alm.value;
				timers[alm.id].process = current;
				timers[alm.id].signo = alm.signo;
				MMTIMER_ENABLE_INT(alm.id);
				spin_unlock_irqrestore(&timers[alm.id].timer_lock, flags);
			}
		}
		else
#endif /* MMTIMER_INTERRUPT_SUPPORT */
			ret = -ENOSYS;
		break;

	case MMTIMER_SETONESHOT: /* set a one shot alarm */
#ifdef MMTIMER_INTERRUPT_SUPPORT
		if(copy_from_user(&alm, (mmtimer_alarm_t *)arg, sizeof(mmtimer_alarm_t)))
			return -EFAULT;
		if(alm.id != 0 || alm.id != 1) {
			if(timers[alm.id].process) {
				ret = -EBUSY;
			}
			else {
				spin_lock_irqsave(&timers[alm.id].timer_lock, flags);
				timers[alm.id].periodic = 0;
				*(timers[alm.id].compare) = alm.value;
				timers[alm.id].process = current;
				timers[alm.id].signo = alm.signo;
				MMTIMER_ENABLE_INT(alm.id);
				spin_unlock_irqrestore(&timers[alm.id].timer_lock, flags);
			}
		}
		else
#endif /* MMTIMER_INTERRUPT_SUPPORT */
			ret = -ENOSYS;
		break;

	case MMTIMER_GETCOUNTER:
		if(copy_to_user((unsigned long *)arg, RTC_COUNTER_ADDR, sizeof(unsigned long)))
			return -EFAULT;
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	return ret;
}

/**
 * mmtimer_mmap - maps the clock's registers into userspace
 * @file: file structure for the device
 * @vma: VMA to map the registers into
 *
 * Calls remap_page_range() to map the clock's registers into
 * the calling process' address space.
 */
static int
mmtimer_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long mmtimer_addr;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	if (vma->vm_flags & VM_WRITE)
		return -EPERM;

	if (PAGE_SIZE > (1 << 16))
		return -ENOSYS;

	vma->vm_flags |= (VM_IO | VM_SHM | VM_LOCKED );
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	mmtimer_addr = __pa(RTC_COUNTER_ADDR);
	mmtimer_addr &= ~(PAGE_SIZE - 1);
	mmtimer_addr &= 0xfffffffffffffffUL;
	
	if (remap_page_range(vma, vma->vm_start, mmtimer_addr, PAGE_SIZE, vma->vm_page_prot)) {
		printk(KERN_ERR "remap_page_range failed in mmtimer.c\n");
		return -EAGAIN;
	}
	
	return 0;
}

#ifdef MMTIMER_INTERRUPT_SUPPORT
/**
 * mmtimer_interrupt - timer interrupt handler
 * @irq: irq received
 * @dev_id: device the irq came from
 * @regs: register state upon receipt of the interrupt
 *
 * Called when one of the comarators matches the counter, this
 * routine will send signals to processes that have requested
 * them.
 */
static void
mmtimer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	int i;

	/*
	 * Do this once for each comparison register
	 */
	for(i = 0; i < NUM_COMPARATORS; i++) {
		if(MMTIMER_INT_PENDING(i)) {
			spin_lock_irqsave(&timers[i].timer_lock, flags);
			force_sig(timers[i].signo, timers[i].process);
			if(timers[i].periodic)
				*(timers[i].compare) += timers[i].periodic;
			else {
				timers[i].process = 0;
				MMTIMER_DISABLE_INT(i);
			}
			spin_unlock_irqrestore(&timers[i].timer_lock, flags);
		}
	}
}
#endif /* MMTIMER_INTERRUPT_SUPPORT */

static struct miscdevice mmtimer_miscdev = {
	SGI_MMTIMER,
	MMTIMER_NAME,
	&mmtimer_fops
};

/**
 * mmtimer_init - device initialization routine
 *
 * Does initial setup for the mmtimer device.
 */
static int __init
mmtimer_init(void)
{
#ifdef MMTIMER_INTERRUPT_SUPPORT
	int irq;
#endif

	/*
	 * Sanity check the cycles/sec variable
	 */
	if (sn_rtc_cycles_per_second < 100000) {
		printk(KERN_ERR "%s: unable to determine clock frequency\n", MMTIMER_NAME);
		return -1;
	}
#ifdef MMTIMER_INTERRUPT_SUPPORT
	irq = 4; /* or whatever the RTC interrupt is */
	if(request_irq(irq, mmtimer_interrupt, SA_INTERRUPT, MMTIMER_NAME, NULL))
		return -1;
#endif /* MMTIMER_INTERRUPT_SUPPORT */

	mmtimer_femtoperiod = ((unsigned long)1E15 + sn_rtc_cycles_per_second / 2) / 
		sn_rtc_cycles_per_second;

	strcpy(mmtimer_miscdev.devfs_name, MMTIMER_NAME);
	if (misc_register(&mmtimer_miscdev)) {
		printk(KERN_ERR "%s: failed to register device\n", MMTIMER_NAME);
		return -1;
	}

	printk(KERN_INFO "%s: v%s, %ld MHz\n", MMTIMER_DESC, MMTIMER_VERSION, sn_rtc_cycles_per_second/(unsigned long)1E6);

	return 0;
}

module_init(mmtimer_init);

