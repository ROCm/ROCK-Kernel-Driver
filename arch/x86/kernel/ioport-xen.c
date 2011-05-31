/*
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus. 32/64 bits code unification by Miguel Botón.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <linux/syscalls.h>
#include <linux/bitmap.h>
#include <asm/syscalls.h>
#include <xen/interface/physdev.h>

/*
 * this changes the io permissions bitmap in the current task.
 */
asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	struct thread_struct *t = &current->thread;
	struct physdev_set_iobitmap set_iobitmap;

	if ((from + num <= from) || (from + num > IO_BITMAP_BITS))
		return -EINVAL;
	if (turn_on && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/*
	 * If it's the first ioperm() call in this thread's lifetime, set the
	 * IO bitmap up. ioperm() is much less timing critical than clone(),
	 * this is why we delay this operation until now:
	 */
	if (!t->io_bitmap_ptr) {
		unsigned long *bitmap = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);

		if (!bitmap)
			return -ENOMEM;

		memset(bitmap, 0xff, IO_BITMAP_BYTES);
		t->io_bitmap_ptr = bitmap;
		set_thread_flag(TIF_IO_BITMAP);

		set_xen_guest_handle(set_iobitmap.bitmap, (char *)bitmap);
		set_iobitmap.nr_ports = IO_BITMAP_BITS;
		WARN_ON(HYPERVISOR_physdev_op(PHYSDEVOP_set_iobitmap,
					      &set_iobitmap));
	}

	if (turn_on)
		bitmap_clear(t->io_bitmap_ptr, from, num);
	else
		bitmap_set(t->io_bitmap_ptr, from, num);

	return 0;
}

/*
 * sys_iopl has to be used when you want to access the IO ports
 * beyond the 0x3ff range: to get the full 65536 ports bitmapped
 * you'd need 8kB of bitmaps/process, which is a bit excessive.
 */
long sys_iopl(unsigned int level, struct pt_regs *regs)
{
	struct thread_struct *t = &current->thread;
	unsigned int old = t->iopl >> 12;

	if (level > 3)
		return -EINVAL;
	/* Trying to gain more privileges? */
	if (level > old) {
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
	}
	t->iopl = level << 12;
	set_iopl_mask(t->iopl);

	return 0;
}
