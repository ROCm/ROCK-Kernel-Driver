/*
 * linux/kernel/ptrace.c
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Common interfaces for "ptrace()" which we do not want
 * to continually duplicate across every architecture.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

int ptrace_attach(struct task_struct *task)
{
	task_lock(task);
	if (task->pid <= 1)
		goto bad;
	if (task == current)
		goto bad;
	if (!task->mm)
		goto bad;
	if(((current->uid != task->euid) ||
	    (current->uid != task->suid) ||
	    (current->uid != task->uid) ||
 	    (current->gid != task->egid) ||
 	    (current->gid != task->sgid) ||
 	    (!cap_issubset(task->cap_permitted, current->cap_permitted)) ||
 	    (current->gid != task->gid)) && !capable(CAP_SYS_PTRACE))
		goto bad;
	rmb();
	if (!task->mm->dumpable && !capable(CAP_SYS_PTRACE))
		goto bad;
	/* the same process cannot be attached many times */
	if (task->ptrace & PT_PTRACED)
		goto bad;

	/* Go */
	task->ptrace |= PT_PTRACED;
	if (capable(CAP_SYS_PTRACE))
		task->ptrace |= PT_PTRACE_CAP;
	task_unlock(task);

	write_lock_irq(&tasklist_lock);
	if (task->p_pptr != current) {
		REMOVE_LINKS(task);
		task->p_pptr = current;
		SET_LINKS(task);
	}
	write_unlock_irq(&tasklist_lock);

	send_sig(SIGSTOP, task, 1);
	return 0;

bad:
	task_unlock(task);
	return -EPERM;
}

int ptrace_detach(struct task_struct *child, unsigned int data)
{
	if ((unsigned long) data > _NSIG)
		return	-EIO;

	/* Architecture-specific hardware disable .. */
	ptrace_disable(child);

	/* .. re-parent .. */
	child->ptrace = 0;
	child->exit_code = data;
	write_lock_irq(&tasklist_lock);
	REMOVE_LINKS(child);
	child->p_pptr = child->p_opptr;
	SET_LINKS(child);
	write_unlock_irq(&tasklist_lock);

	/* .. and wake it up. */
	wake_up_process(child);
	return 0;
}

/*
 * Access another process' address space, one page at a time.
 */
static int access_one_page(struct mm_struct * mm, struct vm_area_struct * vma, unsigned long addr, void *buf, int len, int write)
{
	pgd_t * pgdir;
	pmd_t * pgmiddle;
	pte_t * pgtable;
	char *maddr; 
	struct page *page;

repeat:
	spin_lock(&mm->page_table_lock);
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (pgd_none(*pgdir))
		goto fault_in_page;
	if (pgd_bad(*pgdir))
		goto bad_pgd;
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle))
		goto fault_in_page;
	if (pmd_bad(*pgmiddle))
		goto bad_pmd;
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable))
		goto fault_in_page;
	if (write && (!pte_write(*pgtable) || !pte_dirty(*pgtable)))
		goto fault_in_page;
	page = pte_page(*pgtable);

	/* ZERO_PAGE is special: reads from it are ok even though it's marked reserved */
	if (page != ZERO_PAGE(addr) || write) {
		if ((!VALID_PAGE(page)) || PageReserved(page)) {
			spin_unlock(&mm->page_table_lock);
			return 0;
		}
	}
	get_page(page);
	spin_unlock(&mm->page_table_lock);
	flush_cache_page(vma, addr);

	if (write) {
		maddr = kmap(page);
		memcpy(maddr + (addr & ~PAGE_MASK), buf, len);
		flush_page_to_ram(page);
		flush_icache_page(vma, page);
		kunmap(page);
	} else {
		maddr = kmap(page);
		memcpy(buf, maddr + (addr & ~PAGE_MASK), len);
		flush_page_to_ram(page);
		kunmap(page);
	}
	put_page(page);
	return len;

fault_in_page:
	spin_unlock(&mm->page_table_lock);
	/* -1: out of memory. 0 - unmapped page */
	if (handle_mm_fault(mm, vma, addr, write) > 0)
		goto repeat;
	return 0;

bad_pgd:
	spin_unlock(&mm->page_table_lock);
	pgd_ERROR(*pgdir);
	return 0;

bad_pmd:
	spin_unlock(&mm->page_table_lock);
	pmd_ERROR(*pgmiddle);
	return 0;
}

static int access_mm(struct mm_struct *mm, struct vm_area_struct * vma, unsigned long addr, void *buf, int len, int write)
{
	int copied = 0;

	for (;;) {
		unsigned long offset = addr & ~PAGE_MASK;
		int this_len = PAGE_SIZE - offset;
		int retval;

		if (this_len > len)
			this_len = len;
		retval = access_one_page(mm, vma, addr, buf, this_len, write);
		copied += retval;
		if (retval != this_len)
			break;

		len -= retval;
		if (!len)
			break;

		addr += retval;
		buf += retval;

		if (addr < vma->vm_end)
			continue;	
		if (!vma->vm_next)
			break;
		if (vma->vm_next->vm_start != vma->vm_end)
			break;
	
		vma = vma->vm_next;
	}
	return copied;
}

int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write)
{
	int copied;
	struct mm_struct *mm;
	struct vm_area_struct * vma;

	/* Worry about races with exit() */
	task_lock(tsk);
	mm = tsk->mm;
	if (mm)
		atomic_inc(&mm->mm_users);
	task_unlock(tsk);
	if (!mm)
		return 0;

	down_read(&mm->mmap_sem);
	vma = find_extend_vma(mm, addr);
	copied = 0;
	if (vma)
		copied = access_mm(mm, vma, addr, buf, len, write);

	up_read(&mm->mmap_sem);
	mmput(mm);
	return copied;
}

int ptrace_readdata(struct task_struct *tsk, unsigned long src, char *dst, int len)
{
	int copied = 0;

	while (len > 0) {
		char buf[128];
		int this_len, retval;

		this_len = (len > sizeof(buf)) ? sizeof(buf) : len;
		retval = access_process_vm(tsk, src, buf, this_len, 0);
		if (!retval) {
			if (copied)
				break;
			return -EIO;
		}
		if (copy_to_user(dst, buf, retval))
			return -EFAULT;
		copied += retval;
		src += retval;
		dst += retval;
		len -= retval;			
	}
	return copied;
}

int ptrace_writedata(struct task_struct *tsk, char * src, unsigned long dst, int len)
{
	int copied = 0;

	while (len > 0) {
		char buf[128];
		int this_len, retval;

		this_len = (len > sizeof(buf)) ? sizeof(buf) : len;
		if (copy_from_user(buf, src, this_len))
			return -EFAULT;
		retval = access_process_vm(tsk, dst, buf, this_len, 1);
		if (!retval) {
			if (copied)
				break;
			return -EIO;
		}
		copied += retval;
		src += retval;
		dst += retval;
		len -= retval;			
	}
	return copied;
}
