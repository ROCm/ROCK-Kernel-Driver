/******************************************************************************
 * privcmd.c
 * 
 * Interface to privileged domain-0 commands.
 * 
 * Copyright (c) 2002-2004, K A Fraser, B Dragovic
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
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
#include <xen/evtchn.h>

#ifndef CONFIG_PREEMPT
DEFINE_PER_CPU(bool, privcmd_hcall);
#endif

static inline void _privcmd_hcall(bool state)
{
#ifndef CONFIG_PREEMPT
	this_cpu_write(privcmd_hcall, state);
#endif
}

#ifndef CONFIG_XEN_PRIVILEGED_GUEST
#define HAVE_ARCH_PRIVCMD_MMAP
#endif
#ifndef HAVE_ARCH_PRIVCMD_MMAP
static int enforce_singleshot_mapping_fn(pte_t *pte, struct page *pmd_page,
					 unsigned long addr, void *data)
{
	return pte_none(*pte) ? 0 : -EBUSY;
}

static inline int enforce_singleshot_mapping(struct vm_area_struct *vma,
					     unsigned long addr,
					     unsigned long npages)
{
	return apply_to_page_range(vma->vm_mm, addr, npages << PAGE_SHIFT,
				   enforce_singleshot_mapping_fn, NULL) == 0;
}
#else
#define enforce_singleshot_mapping(vma, addr, npages) \
	privcmd_enforce_singleshot_mapping(vma)
#endif

static long privcmd_ioctl(struct file *file,
			  unsigned int cmd, unsigned long data)
{
	long ret;
	void __user *udata = (void __user *) data;
#ifdef CONFIG_XEN_PRIVILEGED_GUEST
	unsigned long i, addr, nr, nr_pages;
	int paged_out;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	LIST_HEAD(pagelist);
	struct list_head *l, *l2;
#endif

	switch (cmd) {
	case IOCTL_PRIVCMD_HYPERCALL: {
		privcmd_hypercall_t hypercall;
  
		if (copy_from_user(&hypercall, udata, sizeof(hypercall)))
			return -EFAULT;

#ifdef CONFIG_X86
		ret = -ENOSYS;
		if (hypercall.op >= (PAGE_SIZE >> 5))
			break;
		_privcmd_hcall(true);
		ret = _hypercall(long, (unsigned int)hypercall.op,
				 (unsigned long)hypercall.arg[0],
				 (unsigned long)hypercall.arg[1],
				 (unsigned long)hypercall.arg[2],
				 (unsigned long)hypercall.arg[3],
				 (unsigned long)hypercall.arg[4]);
#else
		_privcmd_hcall(true);
		ret = privcmd_hypercall(&hypercall);
#endif
		_privcmd_hcall(false);
	}
	break;

#ifdef CONFIG_XEN_PRIVILEGED_GUEST

	case IOCTL_PRIVCMD_MMAP: {
#define MMAP_NR_PER_PAGE \
	(unsigned long)((PAGE_SIZE - sizeof(*l)) / sizeof(*msg))
		privcmd_mmap_t mmapcmd;
		privcmd_mmap_entry_t *msg;
		privcmd_mmap_entry_t __user *p;

		if (!is_initial_xendomain())
			return -EPERM;

		if (copy_from_user(&mmapcmd, udata, sizeof(mmapcmd)))
			return -EFAULT;

		if (mmapcmd.num <= 0)
			return -EINVAL;

		p = mmapcmd.entry;
		for (i = 0; i < mmapcmd.num;) {
			if (i)
				cond_resched();

			nr = min(mmapcmd.num - i, MMAP_NR_PER_PAGE);

			ret = -ENOMEM;
			l = (struct list_head *) __get_free_page(GFP_KERNEL);
			if (l == NULL)
				goto mmap_out;

			INIT_LIST_HEAD(l);
			list_add_tail(l, &pagelist);
			msg = (privcmd_mmap_entry_t*)(l + 1);

			ret = -EFAULT;
			if (copy_from_user(msg, p, nr*sizeof(*msg)))
				goto mmap_out;
			i += nr;
			p += nr;
		}

		l = pagelist.next;
		msg = (privcmd_mmap_entry_t*)(l + 1);

		down_write(&mm->mmap_sem);

		vma = find_vma(mm, msg->va);
		ret = -EINVAL;
		if (!vma || (msg->va != vma->vm_start))
			goto mmap_out;

		addr = vma->vm_start;

		i = 0;
		list_for_each(l, &pagelist) {
			if (i)
				cond_resched();

			nr = i + min(mmapcmd.num - i, MMAP_NR_PER_PAGE);

			msg = (privcmd_mmap_entry_t*)(l + 1);
			while (i<nr) {

				/* Do not allow range to wrap the address space. */
				if ((msg->npages > (LONG_MAX >> PAGE_SHIFT)) ||
				    (((unsigned long)msg->npages << PAGE_SHIFT) >= -addr))
					goto mmap_out;

				/* Range chunks must be contiguous in va space. */
				if ((msg->va != addr) ||
				    ((msg->va+(msg->npages<<PAGE_SHIFT)) > vma->vm_end))
					goto mmap_out;

				addr += msg->npages << PAGE_SHIFT;
				msg++;
				i++;
			}
		}

		if (!enforce_singleshot_mapping(vma, vma->vm_start,
						(addr - vma->vm_start) >> PAGE_SHIFT))
			goto mmap_out;

		addr = vma->vm_start;
		i = 0;
		list_for_each(l, &pagelist) {
			if (i)
				cond_resched();

			nr = i + min(mmapcmd.num - i, MMAP_NR_PER_PAGE);

			msg = (privcmd_mmap_entry_t*)(l + 1);
			while (i < nr) {
				if ((ret = direct_remap_pfn_range(
					     vma,
					     msg->va & PAGE_MASK,
					     msg->mfn,
					     msg->npages << PAGE_SHIFT,
					     vma->vm_page_prot,
					     mmapcmd.dom)) < 0)
					goto mmap_out;

				addr += msg->npages << PAGE_SHIFT;
				msg++;
				i++;
			}
		}

		ret = 0;

	mmap_out:
		up_write(&mm->mmap_sem);
		i = 0;
		list_for_each_safe(l, l2, &pagelist) {
			if (!(++i & 7))
				cond_resched();
			free_page((unsigned long)l);
		}
	}
#undef MMAP_NR_PER_PAGE
	break;

	case IOCTL_PRIVCMD_MMAPBATCH: {
#define MMAPBATCH_NR_PER_PAGE \
	(unsigned long)((PAGE_SIZE - sizeof(*l)) / sizeof(*mfn))
		privcmd_mmapbatch_t m;
		xen_pfn_t __user *p;
		xen_pfn_t *mfn;

		if (!is_initial_xendomain())
			return -EPERM;

		if (copy_from_user(&m, udata, sizeof(m)))
			return -EFAULT;

		nr_pages = m.num;
		addr = m.addr;
		if (m.num <= 0 || nr_pages > (LONG_MAX >> PAGE_SHIFT) ||
		    addr != m.addr || nr_pages > (-addr >> PAGE_SHIFT))
			return -EINVAL;

		p = m.arr;
		for (i=0; i<nr_pages; )	{
			if (i)
				cond_resched();

			nr = min(nr_pages - i, MMAPBATCH_NR_PER_PAGE);

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

		vma = find_vma(mm, addr);
		ret = -EINVAL;
		if (!vma ||
		    addr < vma->vm_start ||
		    addr + (nr_pages << PAGE_SHIFT) > vma->vm_end ||
		    !enforce_singleshot_mapping(vma, addr, nr_pages)) {
			up_write(&mm->mmap_sem);
			goto mmapbatch_out;
		}

		i = 0;
		ret = 0;
		paged_out = 0;
		list_for_each(l, &pagelist) {
			if (i)
				cond_resched();

			nr = i + min(nr_pages - i, MMAPBATCH_NR_PER_PAGE);
			mfn = (unsigned long *)(l + 1);

			while (i<nr) {
				int rc;

				rc = direct_remap_pfn_range(vma, addr & PAGE_MASK,
				                            *mfn, PAGE_SIZE,
				                            vma->vm_page_prot, m.dom);
				if(rc < 0) {
					if (rc == -ENOENT)
					{
						*mfn |= PRIVCMD_MMAPBATCH_PAGED_ERROR;
						paged_out = 1;
					}
					else
						*mfn |= PRIVCMD_MMAPBATCH_MFN_ERROR;
					ret++;
				}
				mfn++; i++; addr += PAGE_SIZE;
			}
		}

		up_write(&mm->mmap_sem);
		if (ret > 0) {
			p = m.arr;
			i = 0;
			if (paged_out)
				ret = -ENOENT;
			else
				ret = 0;
			list_for_each(l, &pagelist) {
				if (i)
					cond_resched();

				nr = min(nr_pages - i, MMAPBATCH_NR_PER_PAGE);
				mfn = (unsigned long *)(l + 1);
				if (copy_to_user(p, mfn, nr*sizeof(*mfn)))
					ret = -EFAULT;
				i += nr; p += nr;
			}
		}
	mmapbatch_out:
		i = 0;
		list_for_each_safe(l, l2, &pagelist) {
			if (!(++i & 7))
				cond_resched();
			free_page((unsigned long)l);
		}
	}
	break;

	case IOCTL_PRIVCMD_MMAPBATCH_V2: {
		privcmd_mmapbatch_v2_t m;
		const xen_pfn_t __user *p;
		xen_pfn_t *mfn;
		int *err;

		if (!is_initial_xendomain())
			return -EPERM;

		if (copy_from_user(&m, udata, sizeof(m)))
			return -EFAULT;

		nr_pages = m.num;
		addr = m.addr;
		if (m.num <= 0 || nr_pages > (ULONG_MAX >> PAGE_SHIFT) ||
		    addr != m.addr || nr_pages > (-addr >> PAGE_SHIFT))
			return -EINVAL;

		p = m.arr;
		for (i = 0; i < nr_pages; i += nr, p += nr) {
			if (i)
				cond_resched();

			nr = min(nr_pages - i, MMAPBATCH_NR_PER_PAGE);

			ret = -ENOMEM;
			l = (struct list_head *)__get_free_page(GFP_KERNEL);
			if (l == NULL)
				goto mmapbatch_v2_out;

			INIT_LIST_HEAD(l);
			list_add_tail(l, &pagelist);

			mfn = (void *)(l + 1);
			ret = -EFAULT;
			if (copy_from_user(mfn, p, nr * sizeof(*mfn)))
				goto mmapbatch_v2_out;
		}

		down_write(&mm->mmap_sem);

		vma = find_vma(mm, addr);
		ret = -EINVAL;
		if (!vma ||
		    addr < vma->vm_start ||
		    addr + (nr_pages << PAGE_SHIFT) > vma->vm_end ||
		    !enforce_singleshot_mapping(vma, addr, nr_pages)) {
			up_write(&mm->mmap_sem);
			goto mmapbatch_v2_out;
		}

		i = 0;
		ret = 0;
		paged_out = 0;
		list_for_each(l, &pagelist) {
			if (i)
				cond_resched();

			nr = i + min(nr_pages - i, MMAPBATCH_NR_PER_PAGE);
			mfn = (void *)(l + 1);
			err = (void *)(l + 1);
			BUILD_BUG_ON(sizeof(*err) > sizeof(*mfn));

			while (i < nr) {
				int rc;

				rc = direct_remap_pfn_range(vma, addr & PAGE_MASK,
				                            *mfn, PAGE_SIZE,
				                            vma->vm_page_prot, m.dom);
				if (rc < 0) {
					if (rc == -ENOENT)
						paged_out = 1;
					ret++;
				} else
					BUG_ON(rc > 0);
				*err++ = rc;
				mfn++; i++; addr += PAGE_SIZE;
			}
		}

		up_write(&mm->mmap_sem);

		if (ret > 0) {
			int __user *p = m.err;

			ret = paged_out ? -ENOENT : 0;
			i = 0;
			list_for_each(l, &pagelist) {
				if (i)
					cond_resched();

				nr = min(nr_pages - i, MMAPBATCH_NR_PER_PAGE);
				err = (void *)(l + 1);
				if (copy_to_user(p, err, nr * sizeof(*err)))
					ret = -EFAULT;
				i += nr; p += nr;
			}
		} else if (clear_user(m.err, nr_pages * sizeof(*m.err)))
			ret = -EFAULT;

	mmapbatch_v2_out:
		i = 0;
		list_for_each_safe(l, l2, &pagelist) {
			if (!(++i & 7))
				cond_resched();
			free_page((unsigned long)l);
		}
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

	/* DONTCOPY is essential for Xen because copy_page_range doesn't know
	 * how to recreate these mappings */
	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTCOPY | VM_DONTEXPAND |
			 VM_DONTDUMP;
	vma->vm_ops = &privcmd_vm_ops;
	vma->vm_private_data = NULL;

	return 0;
}
#endif

static const struct file_operations privcmd_file_ops = {
	.open = nonseekable_open,
	.llseek = no_llseek,
	.unlocked_ioctl = privcmd_ioctl,
#ifdef CONFIG_XEN_PRIVILEGED_GUEST
	.mmap = privcmd_mmap
#endif
};

static int capabilities_show(struct seq_file *m, void *v)
{
	int len = 0;

	if (is_initial_xendomain())
		len = seq_printf(m, "control_d\n");

	return len;
}

static int capabilities_open(struct inode *inode, struct file *file)
{
	return single_open(file, capabilities_show, PDE_DATA(inode));
}

static const struct file_operations capabilities_fops = {
	.open = capabilities_open,
	.llseek = seq_lseek,
	.read = seq_read,
	.release = single_release
};

static int __init privcmd_init(void)
{
	if (!is_running_on_xen())
		return -ENODEV;

	create_xen_proc_entry("privcmd", S_IFREG|S_IRUSR,
			      &privcmd_file_ops, NULL);
	create_xen_proc_entry("capabilities", S_IFREG|S_IRUGO,
			      &capabilities_fops, NULL);

	return 0;
}

__initcall(privcmd_init);
