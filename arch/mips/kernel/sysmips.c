/*
 * MIPS specific syscalls
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2000 by Ralf Baechle
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/ptrace.h>

#include <asm/cachectl.h>
#include <asm/pgalloc.h>
#include <asm/sysmips.h>
#include <asm/uaccess.h>

extern asmlinkage void syscall_trace(void);

/*
 * How long a hostname can we get from user space?
 *  -EFAULT if invalid area or too long
 *  0 if ok
 *  >0 EFAULT after xx bytes
 */
static inline int
get_max_hostname(unsigned long address)
{
	struct vm_area_struct * vma;

	vma = find_vma(current->mm, address);
	if (!vma || vma->vm_start > address || !(vma->vm_flags & VM_READ))
		return -EFAULT;
	address = vma->vm_end - address;
	if (address > PAGE_SIZE)
		return 0;
	if (vma->vm_next && vma->vm_next->vm_start == vma->vm_end &&
	   (vma->vm_next->vm_flags & VM_READ))
		return 0;
	return address;
}

asmlinkage int
sys_sysmips(int cmd, int arg1, int arg2, int arg3)
{
	int	*p;
	char	*name;
	int	tmp, len, retval, errno;

	switch(cmd) {
	case SETNAME: {
		char nodename[__NEW_UTS_LEN + 1];

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		name = (char *) arg1;

		len = strncpy_from_user(nodename, name, __NEW_UTS_LEN);
		if (len < 0) 
			return -EFAULT;
		nodename[__NEW_UTS_LEN] = '\0';

		down_write(&uts_sem);
		strlcpy(system_utsname.nodename, nodename,
			sizeof(system_utsname.nodename));
		up_write(&uts_sem);
		return 0;
	}

	case MIPS_ATOMIC_SET: {
#ifdef CONFIG_CPU_HAS_LLSC
		unsigned int tmp;

		p = (int *) arg1;
		errno = verify_area(VERIFY_WRITE, p, sizeof(*p));
		if (errno)
			return errno;
		errno = 0;

		__asm__(".set\tpush\t\t\t# sysmips(MIPS_ATOMIC, ...)\n\t"
			".set\tmips2\n\t"
			".set\tnoat\n\t"
			"1:\tll\t%0, %4\n\t"
			"move\t$1, %3\n\t"
			"2:\tsc\t$1, %1\n\t"
			"beqz\t$1, 1b\n\t"
			".set\tpop\n\t"
			".section\t.fixup,\"ax\"\n"
			"3:\tli\t%2, 1\t\t\t# error\n\t"
			".previous\n\t"
			".section\t__ex_table,\"a\"\n\t"
			".word\t1b, 3b\n\t"
			".word\t2b, 3b\n\t"
			".previous\n\t"
			: "=&r" (tmp), "=o" (* (u32 *) p), "=r" (errno)
			: "r" (arg2), "o" (* (u32 *) p), "2" (errno)
			: "$1");

		if (errno)
			return -EFAULT;

		/* We're skipping error handling etc.  */
		if (current->ptrace & PT_TRACESYS)
			syscall_trace();

		((struct pt_regs *)&cmd)->regs[2] = tmp;
		((struct pt_regs *)&cmd)->regs[7] = 0;

		__asm__ __volatile__(
			"move\t$29, %0\n\t"
			"j\to32_ret_from_sys_call"
			: /* No outputs */
			: "r" (&cmd));
		/* Unreached */
#else
	printk("sys_sysmips(MIPS_ATOMIC_SET, ...) not ready for !CONFIG_CPU_HAS_LLSC\n");
#endif
	}

	case MIPS_FIXADE:
		tmp = current->thread.mflags & ~3;
		current->thread.mflags = tmp | (arg1 & 3);
		retval = 0;
		goto out;

	case FLUSH_CACHE:
		flush_cache_all();
		retval = 0;
		goto out;

	case MIPS_RDNVRAM:
		retval = -EIO;
		goto out;

	default:
		retval = -EINVAL;
		goto out;
	}

out:
	return retval;
}

/*
 * No implemented yet ...
 */
asmlinkage int
sys_cachectl(char *addr, int nbytes, int op)
{
	return -ENOSYS;
}
