/******************************************************************************
 * privcmd.c
 * 
 * Interface to privileged domain-0 commands.
 * 
 * Copyright (c) 2002-2004, K A Fraser, B Dragovic
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <asm/hypervisor.h>

#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>
#include <asm/hypervisor.h>
#include <xen/public/privcmd.h>
#include <xen/interface/xen.h>
#include <xen/xen_proc.h>
#include <xen/features.h>

static struct proc_dir_entry *privcmd_intf;
static struct proc_dir_entry *capabilities_intf;

#ifndef CONFIG_XEN_PRIVILEGED_GUEST
#define HAVE_ARCH_PRIVCMD_MMAP
#endif
#ifndef HAVE_ARCH_PRIVCMD_MMAP
static int privcmd_enforce_singleshot_mapping(struct vm_area_struct *vma);
#endif

static long privcmd_ioctl(struct file *file,
			  unsigned int cmd, unsigned long data)
{
	long ret = -ENOSYS;
	void __user *udata = (void __user *) data;

	switch (cmd) {
	case IOCTL_PRIVCMD_HYPERCALL: {
		privcmd_hypercall_t hypercall;
  
		if (copy_from_user(&hypercall, udata, sizeof(hypercall)))
			return -EFAULT;

#ifdef CONFIG_X86
		if (hypercall.op >= (PAGE_SIZE >> 5))
			break;
		ret = _hypercall(long, (unsigned int)hypercall.op,
				 (unsigned long)hypercall.arg[0],
				 (unsigned long)hypercall.arg[1],
				 (unsigned long)hypercall.arg[2],
				 (unsigned long)hypercall.arg[3],
				 (unsigned long)hypercall.arg[4]);
#else
		ret = privcmd_hypercall(&hypercall);
#endif
	}
	break;

#ifdef CONFIG_XEN_PRIVILEGED_GUEST

	case IOCTL_PRIVCMD_MMAP: {
#define MMAP_NR_PER_PAGE (int)((PAGE_SIZE-sizeof(struct list_head))/sizeof(privcmd_mmap_entry_t))
		privcmd_mmap_t mmapcmd;
		privcmd_mmap_entry_t *msg;
		privcmd_mmap_entry_t __user *p;
		struct mm_struct *mm = current->mm;
		struct vm_area_struct *vma;
		unsigned long va;
		int i, rc;
		LIST_HEAD(pagelist);
		struct list_head *l,*l2;

		if (!is_initial_xendomain())
			return -EPERM;

		if (copy_from_user(&mmapcmd, udata, sizeof(mmapcmd)))
			return -EFAULT;

		p = mmapcmd.entry;
		for (i = 0; i < mmapcmd.num;) {
			int nr = min(mmapcmd.num - i, MMAP_NR_PER_PAGE);

			rc = -ENOMEM;
			l = (struct list_head *) __get_free_page(GFP_KERNEL);
			if (l == NULL)
				goto mmap_out;

			INIT_LIST_HEAD(l);
			list_add_tail(l, &pagelist);
			msg = (privcmd_mmap_entry_t*)(l + 1);

			rc = -EFAULT;
			if (copy_from_user(msg, p, nr*sizeof(*msg)))
				goto mmap_out;
			i += nr;
			p += nr;
		}

		l = pagelist.next;
		msg = (privcmd_mmap_entry_t*)(l + 1);

		down_write(&mm->mmap_sem);

		vma = find_vma(mm, msg->va);
		rc = -EINVAL;
		if (!vma || (msg->va != vma->vm_start) ||
		    !privcmd_enforce_singleshot_mapping(vma))
			goto mmap_out;

		va = vma->vm_start;

		i = 0;
		list_for_each(l, &pagelist) {
			int nr = i + min(mmapcmd.num - i, MMAP_NR_PER_PAGE);

			msg = (privcmd_mmap_entry_t*)(l + 1);
			while (i<nr) {

				/* Do not allow range to wrap the address space. */
				rc = -EINVAL;
				if ((msg->npages > (LONG_MAX >> PAGE_SHIFT)) ||
				    ((unsigned long)(msg->npages << PAGE_SHIFT) >= -va))
					goto mmap_out;

				/* Range chunks must be contiguous in va space. */
				if ((msg->va != va) ||
				    ((msg->va+(msg->npages<<PAGE_SHIFT)) > vma->vm_end))
					goto mmap_out;

				if ((rc = direct_remap_pfn_range(
					     vma,
					     msg->va & PAGE_MASK,
					     msg->mfn,
					     msg->npages << PAGE_SHIFT,
					     vma->vm_page_prot,
					     mmapcmd.dom)) < 0)
					goto mmap_out;

				va += msg->npages << PAGE_SHIFT;
				msg++;
				i++;
			}
		}

		rc = 0;

	mmap_out:
		up_write(&mm->mmap_sem);
		list_for_each_safe(l,l2,&pagelist)
			free_page((unsigned long)l);
		ret = rc;
	}
#undef MMAP_NR_PER_PAGE
	break;

	case IOCTL_PRIVCMD_MMAPBATCH: {
#define MMAPBATCH_NR_PER_PAGE (unsigned long)((PAGE_SIZE-sizeof(struct list_head))/sizeof(unsigned long))
		privcmd_mmapbatch_t m;
		struct mm_struct *mm = current->mm;
		struct vm_area_struct *vma;
		xen_pfn_t __user *p;
		unsigned long addr, *mfn, nr_pages;
		int i;
		LIST_HEAD(pagelist);
		struct list_head *l, *l2;

		if (!is_initial_xendomain())
			return -EPERM;

		if (copy_from_user(&m, udata, sizeof(m)))
			return -EFAULT;

		nr_pages = m.num;
		if ((m.num <= 0) || (nr_pages > (LONG_MAX >> PAGE_SHIFT)))
			return -EINVAL;

		p = m.arr;
		for (i=0; i<nr_pages; )	{
			int nr = min(nr_pages - i, MMAPBATCH_NR_PER_PAGE);

			ret = -ENOMEM;
			l = (struct list_head *)__get_free_page(GFP_KERNEL);
			if (l == NULL)
				goto mmapbatch_out;

			INIT_LIST_HEAD(l);
			list_add_tail(l, &pagelist);

			mfn = (unsigned long*)(l + 1);
			ret = -EFAULT;
			if (copy_from_user(mfn, p, nr*sizeof(*mfn)))
				goto mmapbatch_out;

			i += nr; p+= nr;
		}

		down_write(&mm->mmap_sem);

		vma = find_vma(mm, m.addr);
		ret = -EINVAL;
		if (!vma ||
		    (m.addr != vma->vm_start) ||
		    ((m.addr + (nr_pages << PAGE_SHIFT)) != vma->vm_end) ||
		    !privcmd_enforce_singleshot_mapping(vma)) {
			up_write(&mm->mmap_sem);
			goto mmapbatch_out;
		}

		p = m.arr;
		addr = m.addr;
		i = 0;
		ret = 0;
		list_for_each(l, &pagelist) {
			int nr = i + min(nr_pages - i, MMAPBATCH_NR_PER_PAGE);
			mfn = (unsigned long *)(l + 1);

			while (i<nr) {
				if(direct_remap_pfn_range(vma, addr & PAGE_MASK,
							  *mfn, PAGE_SIZE,
							  vma->vm_page_prot, m.dom) < 0) {
					*mfn |= 0xf0000000U;
					ret++;
				}
				mfn++; i++; addr += PAGE_SIZE;
			}
		}

		up_write(&mm->mmap_sem);
		if (ret > 0) {
			p = m.arr;
			i = 0;
			ret = 0;
			list_for_each(l, &pagelist) {
				int nr = min(nr_pages - i, MMAPBATCH_NR_PER_PAGE);
				mfn = (unsigned long *)(l + 1);
				if (copy_to_user(p, mfn, nr*sizeof(*mfn)))
					ret = -EFAULT;
				i += nr; p += nr;
			}
		}
	mmapbatch_out:
		list_for_each_safe(l,l2,&pagelist)
			free_page((unsigned long)l);
#undef MMAPBATCH_NR_PER_PAGE
	}
	break;

#endif /* CONFIG_XEN_PRIVILEGED_GUEST */

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifndef HAVE_ARCH_PRIVCMD_MMAP
static int privcmd_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

static struct vm_operations_struct privcmd_vm_ops = {
	.fault = privcmd_fault
};

static int privcmd_mmap(struct file * file, struct vm_area_struct * vma)
{
	/* Unsupported for auto-translate guests. */
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return -ENOSYS;

	/* DONTCOPY is essential for Xen as copy_page_range is broken. */
	vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP | VM_DONTCOPY;
	vma->vm_ops = &privcmd_vm_ops;
	vma->vm_private_data = NULL;

	return 0;
}

static int privcmd_enforce_singleshot_mapping(struct vm_area_struct *vma)
{
	return (xchg(&vma->vm_private_data, (void *)1) == NULL);
}
#endif

static const struct file_operations privcmd_file_ops = {
	.unlocked_ioctl = privcmd_ioctl,
#ifdef CONFIG_XEN_PRIVILEGED_GUEST
	.mmap = privcmd_mmap,
#endif
};

static int capabilities_read(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	int len = 0;
	*page = 0;

	if (is_initial_xendomain())
		len = sprintf( page, "control_d\n" );

	*eof = 1;
	return len;
}

static int __init privcmd_init(void)
{
	if (!is_running_on_xen())
		return -ENODEV;

	privcmd_intf = create_xen_proc_entry("privcmd", 0400);
	if (privcmd_intf != NULL)
		privcmd_intf->proc_fops = &privcmd_file_ops;

	capabilities_intf = create_xen_proc_entry("capabilities", 0400 );
	if (capabilities_intf != NULL)
		capabilities_intf->read_proc = capabilities_read;

	return 0;
}

__initcall(privcmd_init);
