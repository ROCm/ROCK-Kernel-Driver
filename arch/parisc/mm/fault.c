/* $Id: fault.c,v 1.5 2000/01/26 16:20:29 jsm Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *
 * Copyright (C) 1995, 1996, 1997, 1998 by Ralf Baechle
 * Copyright 1999 SuSE GmbH (Philipp Rumpf, prumpf@tux.org)
 * Copyright 1999 Hewlett Packard Co.
 *
 */

#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/uaccess.h>


/* Defines for parisc_acctyp()	*/
#define READ		0
#define WRITE		1

/* Various important other fields */
#define bit22set(x)		(x & 0x00000200)
#define bits23_25set(x)		(x & 0x000001c0)
#define isGraphicsFlushRead(x)	((x & 0xfc003fdf) == 0x04001a80)
				/* extended opcode is 0x6a */

#define BITSSET		0x1c0	/* for identifying LDCW */

/*
 * parisc_acctyp(unsigned int inst) --
 *    Given a PA-RISC memory access instruction, determine if the
 *    the instruction would perform a memory read or memory write
 *    operation.
 *
 *    This function assumes that the given instruction is a memory access
 *    instruction (i.e. you should really only call it if you know that
 *    the instruction has generated some sort of a memory access fault).
 *
 * Returns:
 *   VM_READ  if read operation
 *   VM_WRITE if write operation
 *   VM_EXEC  if execute operation
 */
static unsigned long
parisc_acctyp(unsigned long code, unsigned int inst)
{
	if (code == 6 || code == 16)
	    return VM_EXEC;

	switch (inst & 0xf0000000) {
	case 0x40000000: /* load */
	case 0x50000000: /* new load */
		return VM_READ;

	case 0x60000000: /* store */
	case 0x70000000: /* new store */
		return VM_WRITE;

	case 0x20000000: /* coproc */
	case 0x30000000: /* coproc2 */
		if (bit22set(inst))
			return VM_WRITE;

	case 0x0: /* indexed/memory management */
		if (bit22set(inst)) {
			/*
			 * Check for the 'Graphics Flush Read' instruction.
			 * It resembles an FDC instruction, except for bits
			 * 20 and 21. Any combination other than zero will
			 * utilize the block mover functionality on some
			 * older PA-RISC platforms.  The case where a block
			 * move is performed from VM to graphics IO space
			 * should be treated as a READ.
			 *
			 * The significance of bits 20,21 in the FDC
			 * instruction is:
			 *
			 *   00  Flush data cache (normal instruction behavior)
			 *   01  Graphics flush write  (IO space -> VM)
			 *   10  Graphics flush read   (VM -> IO space)
			 *   11  Graphics flush read/write (VM <-> IO space)
			 */
			if (isGraphicsFlushRead(inst))
				return VM_READ;
			return VM_WRITE;
		} else {
			/*
			 * Check for LDCWX and LDCWS (semaphore instructions).
			 * If bits 23 through 25 are all 1's it is one of
			 * the above two instructions and is a write.
			 *
			 * Note: With the limited bits we are looking at,
			 * this will also catch PROBEW and PROBEWI. However,
			 * these should never get in here because they don't
			 * generate exceptions of the type:
			 *   Data TLB miss fault/data page fault
			 *   Data memory protection trap
			 */
			if (bits23_25set(inst) == BITSSET)
				return VM_WRITE;
		}
		return VM_READ; /* Default */
	}
	return VM_READ; /* Default */
}

#undef bit22set
#undef bits23_25set
#undef isGraphicsFlushRead
#undef BITSSET

/* This is similar to expand_stack(), except that it is for stacks
 * that grow upwards.
 */

static inline int expand_stackup(struct vm_area_struct * vma, unsigned long address)
{
	unsigned long grow;

	address += 4 + PAGE_SIZE - 1;
	address &= PAGE_MASK;
	grow = (address - vma->vm_end) >> PAGE_SHIFT;
	if (address - vma->vm_start > current->rlim[RLIMIT_STACK].rlim_cur ||
	    ((vma->vm_mm->total_vm + grow) << PAGE_SHIFT) > current->rlim[RLIMIT_AS].rlim_cur)
		return -ENOMEM;
	vma->vm_end = address;
	vma->vm_mm->total_vm += grow;
	if (vma->vm_flags & VM_LOCKED)
		vma->vm_mm->locked_vm += grow;
	return 0;
}


/* This is similar to find_vma(), except that it understands that stacks
 * grow up rather than down.
 * XXX Optimise by making use of cache and avl tree as per find_vma().
 */

struct vm_area_struct * pa_find_vma(struct mm_struct * mm, unsigned long addr)
{
	struct vm_area_struct *vma = NULL;

	if (mm) {
		vma = mm->mmap;
		if (!vma || addr < vma->vm_start)
			return NULL;
		while (vma->vm_next && addr >= vma->vm_next->vm_start)
			vma = vma->vm_next;
	}
	return vma;
}


/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
extern void parisc_terminate(char *, struct pt_regs *, int, unsigned long);

void do_page_fault(struct pt_regs *regs, unsigned long code,
			      unsigned long address)
{
	struct vm_area_struct * vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	const struct exception_table_entry *fix;
	unsigned long acc_type;

	if (in_interrupt() || !mm)
		goto no_context;

	down(&mm->mmap_sem);
	vma = pa_find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (address < vma->vm_end)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSUP) || expand_stackup(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access. We still need to
 * check the access permissions.
 */

good_area:

	acc_type = parisc_acctyp(code,regs->iir);

	if ((vma->vm_flags & acc_type) != acc_type)
		goto bad_area;

	/*
	 * If for any reason at all we couldn't handle the fault, make
	 * sure we exit gracefully rather than endlessly redo the
	 * fault.
	 */

	switch (handle_mm_fault(mm, vma, address, (acc_type & VM_WRITE) != 0)) {
	      case 1:
		++current->min_flt;
		break;
	      case 2:
		++current->maj_flt;
		break;
	      case 0:
		/*
		 * We ran out of memory, or some other thing happened
		 * to us that made us unable to handle the page fault
		 * gracefully.
		 */
		goto bad_area;
	      default:
		goto out_of_memory;
	}
	up(&mm->mmap_sem);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 */
bad_area:
	up(&mm->mmap_sem);

	if (user_mode(regs)) {
		struct siginfo si;

		printk("\ndo_page_fault() pid=%d command='%s'\n",
		    tsk->pid, tsk->comm);
		show_regs(regs);
		/* FIXME: actually we need to get the signo and code correct */
		si.si_signo = SIGSEGV;
		si.si_errno = 0;
		si.si_code = SEGV_MAPERR;
		si.si_addr = (void *) address;
		force_sig_info(SIGSEGV, &si, current);
		return;
	}

no_context:

	if (!user_mode(regs)) {

		fix = search_exception_table(regs->iaoq[0]);

		if (fix) {

			if (fix->skip & 1) 
				regs->gr[8] = -EFAULT;
			if (fix->skip & 2)
				regs->gr[9] = 0;

			regs->iaoq[0] += ((fix->skip) & ~3);

			/*
			 * NOTE: In some cases the faulting instruction
			 * may be in the delay slot of a branch. We
			 * don't want to take the branch, so we don't
			 * increment iaoq[1], instead we set it to be
			 * iaoq[0]+4, and clear the B bit in the PSW
			 */

			regs->iaoq[1] = regs->iaoq[0] + 4;
			regs->gr[0] &= ~PSW_B; /* IPSW in gr[0] */

			return;
		}
	}

	parisc_terminate("Bad Address (null pointer deref?)",regs,code,address);

  out_of_memory:
	up(&mm->mmap_sem);
	printk("VM: killing process %s\n", current->comm);
	if (user_mode(regs))
		do_exit(SIGKILL);
	goto no_context;
}
