/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/blkdev.h>
#include <linux/types.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include <scsi.h>
#include <scsi/scsi_host.h>
#include <asm/pgtable.h>

MODULE_AUTHOR("SGI");
MODULE_DESCRIPTION("Debug VM information");
MODULE_LICENSE("GPL");

struct __vmflags {
	unsigned long mask;
	char *name;
};

static struct __vmflags vmflags[] = {
	{ VM_READ, "VM_READ " },
	{ VM_WRITE, "VM_WRITE " },
	{ VM_EXEC, "VM_EXEC " },
	{ VM_SHARED, "VM_SHARED " },
	{ VM_MAYREAD, "VM_MAYREAD " },
	{ VM_MAYWRITE, "VM_MAYWRITE " },
	{ VM_MAYEXEC, "VM_MAYEXEC " },
	{ VM_MAYSHARE, "VM_MAYSHARE " },
	{ VM_GROWSDOWN, "VM_GROWSDOWN " },
	{ VM_GROWSUP, "VM_GROWSUP " },
	{ VM_PFNMAP, "VM_PFNMAP " },
	{ VM_DENYWRITE, "VM_DENYWRITE " },
	{ VM_EXECUTABLE, "VM_EXECUTABLE " },
	{ VM_LOCKED, "VM_LOCKED " },
	{ VM_IO, "VM_IO " },
	{ VM_SEQ_READ, "VM_SEQ_READ " },
	{ VM_RAND_READ, "VM_RAND_READ " },
	{ VM_DONTCOPY, "VM_DONTCOPY " },
	{ VM_DONTEXPAND, "VM_DONTEXPAND " },
	{ VM_RESERVED, "VM_RESERVED " },
	{ VM_ACCOUNT, "VM_ACCOUNT " },
	{ VM_HUGETLB, "VM_HUGETLB " },
	{ VM_NONLINEAR, "VM_NONLINEAR " },
	{ VM_MAPPED_COPY, "VM_MAPPED_COPY " },
	{ VM_INSERTPAGE, "VM_INSERTPAGE " },
	{ 0, "" }
};

static int
kdbm_print_vm(struct vm_area_struct *vp, unsigned long addr, int verbose_flg)
{
	struct __vmflags *tp;

	kdb_printf("struct vm_area_struct at 0x%lx for %d bytes\n",
		   addr, (int) sizeof (struct vm_area_struct));

	kdb_printf("vm_start = 0x%p   vm_end = 0x%p\n", (void *) vp->vm_start,
		   (void *) vp->vm_end);
	kdb_printf("vm_page_prot = 0x%lx\n", pgprot_val(vp->vm_page_prot));

	kdb_printf("vm_flags: ");
	for (tp = vmflags; tp->mask; tp++) {
		if (vp->vm_flags & tp->mask) {
			kdb_printf(" %s", tp->name);
		}
	}
	kdb_printf("\n");

	if (!verbose_flg)
		return 0;

	kdb_printf("vm_mm = 0x%p\n", (void *) vp->vm_mm);
	kdb_printf("vm_next = 0x%p\n", (void *) vp->vm_next);
	kdb_printf("shared.vm_set.list.next = 0x%p\n", (void *) vp->shared.vm_set.list.next);
	kdb_printf("shared.vm_set.list.prev = 0x%p\n", (void *) vp->shared.vm_set.list.prev);
	kdb_printf("shared.vm_set.parent = 0x%p\n", (void *) vp->shared.vm_set.parent);
	kdb_printf("shared.vm_set.head = 0x%p\n", (void *) vp->shared.vm_set.head);
	kdb_printf("anon_vma_node.next = 0x%p\n", (void *) vp->anon_vma_node.next);
	kdb_printf("anon_vma_node.prev = 0x%p\n", (void *) vp->anon_vma_node.prev);
	kdb_printf("vm_ops = 0x%p\n", (void *) vp->vm_ops);
	if (vp->vm_ops != NULL) {
		kdb_printf("vm_ops->open = 0x%p\n", vp->vm_ops->open);
		kdb_printf("vm_ops->close = 0x%p\n", vp->vm_ops->close);
#ifdef HAVE_VMOP_MPROTECT
		kdb_printf("vm_ops->mprotect = 0x%p\n", vp->vm_ops->mprotect);
#endif
	}
	kdb_printf("vm_pgoff = 0x%lx\n", vp->vm_pgoff);
	kdb_printf("vm_file = 0x%p\n", (void *) vp->vm_file);
	kdb_printf("vm_private_data = 0x%p\n", vp->vm_private_data);

	return 0;
}

static int
kdbm_print_vmp(struct vm_area_struct *vp, int verbose_flg)
{
	struct __vmflags *tp;

	if (verbose_flg) {
		kdb_printf("0x%lx:  ", (unsigned long) vp);
	}

	kdb_printf("0x%p  0x%p ", (void *) vp->vm_start, (void *) vp->vm_end);

	for (tp = vmflags; tp->mask; tp++) {
		if (vp->vm_flags & tp->mask) {
			kdb_printf(" %s", tp->name);
		}
	}
	kdb_printf("\n");

	return 0;
}

/*
 * kdbm_vm
 *
 *     This function implements the 'vm' command.  Print a vm_area_struct.
 *
 *     vm [-v] <address>	Print vm_area_struct at <address>
 *     vmp [-v] <pid>		Print all vm_area_structs for <pid>
 */

static int
kdbm_vm(int argc, const char **argv)
{
	unsigned long addr;
	long offset = 0;
	int nextarg;
	int diag;
	int verbose_flg = 0;

	if (argc == 2) {
		if (strcmp(argv[1], "-v") != 0) {
			return KDB_ARGCOUNT;
		}
		verbose_flg = 1;
	} else if (argc != 1) {
		return KDB_ARGCOUNT;
	}

	if (strcmp(argv[0], "vmp") == 0) {
		struct task_struct *g, *tp;
		struct vm_area_struct *vp;
		pid_t pid;

		if ((diag = kdbgetularg(argv[argc], (unsigned long *) &pid)))
			return diag;

		kdb_do_each_thread(g, tp) {
			if (tp->pid == pid) {
				if (tp->mm != NULL) {
					if (verbose_flg)
						kdb_printf
						    ("vm_area_struct       ");
					kdb_printf
					    ("vm_start            vm_end              vm_flags\n");
					vp = tp->mm->mmap;
					while (vp != NULL) {
						kdbm_print_vmp(vp, verbose_flg);
						vp = vp->vm_next;
					}
				}
				return 0;
			}
		} kdb_while_each_thread(g, tp);

		kdb_printf("No process with pid == %d found\n", pid);

	} else {
		struct vm_area_struct v;

		nextarg = argc;
		if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset,
					  NULL))
		    || (diag = kdb_getarea(v, addr)))
			return (diag);

		kdbm_print_vm(&v, addr, verbose_flg);
	}

	return 0;
}

static int
kdbm_print_pte(pte_t * pte)
{
	kdb_printf("0x%lx (", (unsigned long) pte_val(*pte));

	if (pte_present(*pte)) {
#ifdef	pte_exec
		if (pte_exec(*pte))
			kdb_printf("X");
#endif
		if (pte_write(*pte))
			kdb_printf("W");
#ifdef	pte_read
		if (pte_read(*pte))
			kdb_printf("R");
#endif
		if (pte_young(*pte))
			kdb_printf("A");
		if (pte_dirty(*pte))
			kdb_printf("D");

	} else {
		kdb_printf("OFFSET=0x%lx ", swp_offset(pte_to_swp_entry(*pte)));
		kdb_printf("TYPE=0x%ulx", swp_type(pte_to_swp_entry(*pte)));
	}

	kdb_printf(")");

	/* final newline is output by caller of kdbm_print_pte() */

	return 0;
}

/*
 * kdbm_pte
 *
 *     This function implements the 'pte' command.  Print all pte_t structures
 *     that map to the given virtual address range (<address> through <address>
 *     plus <nbytes>) for the given process. The default value for nbytes is
 *     one.
 *
 *     pte -m <mm> <address> [<nbytes>]    Print all pte_t structures for
 *					   virtual <address> in address space
 *					   of <mm> which is a pointer to a
 *					   mm_struct
 *     pte -p <pid> <address> [<nbytes>]   Print all pte_t structures for
 *					   virtual <address> in address space
 *					   of <pid>
 */

static int
kdbm_pte(int argc, const char **argv)
{
	unsigned long addr;
	long offset = 0;
	int nextarg;
	unsigned long nbytes = 1;
	long npgs;
	int diag;
	int found;
	pid_t pid;
	struct task_struct *tp;
	struct mm_struct *mm, copy_of_mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	if (argc < 3 || argc > 4) {
		return KDB_ARGCOUNT;
	}

	 if (strcmp(argv[1], "-p") == 0) {
		if ((diag = kdbgetularg(argv[2], (unsigned long *) &pid))) {
			return diag;
		}

		found = 0;
		for_each_process(tp) {
			if (tp->pid == pid) {
				if (tp->mm != NULL) {
					found = 1;
					break;
				}
				kdb_printf("task structure's mm field is NULL\n");
				return 0;
			}
		}

		if (!found) {
			kdb_printf("No process with pid == %d found\n", pid);
			return 0;
		}
		mm = tp->mm;
	} else if (strcmp(argv[1], "-m") == 0) {


		nextarg = 2;
		if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset,
					  NULL))
		    || (diag = kdb_getarea(copy_of_mm, addr)))
			return (diag);
		mm = &copy_of_mm;
	} else {
		return KDB_ARGCOUNT;
	}

	if ((diag = kdbgetularg(argv[3], &addr))) {
		return diag;
	}

	if (argc == 4) {
		if ((diag = kdbgetularg(argv[4], &nbytes))) {
			return diag;
		}
	}

	kdb_printf("vaddr              pte\n");

	npgs = ((((addr & ~PAGE_MASK) + nbytes) + ~PAGE_MASK) >> PAGE_SHIFT);
	while (npgs-- > 0) {

		kdb_printf("0x%p ", (void *) (addr & PAGE_MASK));

		pgd = pgd_offset(mm, addr);
		if (pgd_present(*pgd)) {
			pud = pud_offset(pgd, addr);
			if (pud_present(*pud)) {
				pmd = pmd_offset(pud, addr);
				if (pmd_present(*pmd)) {
					pte = pte_offset_map(pmd, addr);
					if (pte_present(*pte)) {
						kdbm_print_pte(pte);
					}
				}
			}
		}

		kdb_printf("\n");
		addr += PAGE_SIZE;
	}

	return 0;
}

/*
 * kdbm_rpte
 *
 *     This function implements the 'rpte' command.  Print all pte_t structures
 *     that contain the given physical page range (<pfn> through <pfn>
 *     plus <npages>) for the given process. The default value for npages is
 *     one.
 *
 *     rpte -m <mm> <pfn> [<npages>]	   Print all pte_t structures for
 *					   physical page <pfn> in address space
 *					   of <mm> which is a pointer to a
 *					   mm_struct
 *     rpte -p <pid> <pfn> [<npages>]	   Print all pte_t structures for
 *					   physical page <pfn> in address space
 *					   of <pid>
 */

static int
kdbm_rpte(int argc, const char **argv)
{
	unsigned long addr;
	unsigned long pfn;
	long offset = 0;
	int nextarg;
	unsigned long npages = 1;
	int diag;
	int found;
	pid_t pid;
	struct task_struct *tp;
	struct mm_struct *mm, copy_of_mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long g, u, m, t;

	if (argc < 3 || argc > 4) {
		return KDB_ARGCOUNT;
	}

	 if (strcmp(argv[1], "-p") == 0) {
		if ((diag = kdbgetularg(argv[2], (unsigned long *) &pid))) {
			return diag;
		}

		found = 0;
		for_each_process(tp) {
			if (tp->pid == pid) {
				if (tp->mm != NULL) {
					found = 1;
					break;
				}
				kdb_printf("task structure's mm field is NULL\n");
				return 0;
			}
		}

		if (!found) {
			kdb_printf("No process with pid == %d found\n", pid);
			return 0;
		}
		mm = tp->mm;
	} else if (strcmp(argv[1], "-m") == 0) {


		nextarg = 2;
		if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset,
					  NULL))
		    || (diag = kdb_getarea(copy_of_mm, addr)))
			return (diag);
		mm = &copy_of_mm;
	} else {
		return KDB_ARGCOUNT;
	}

	if ((diag = kdbgetularg(argv[3], &pfn))) {
		return diag;
	}

	if (argc == 4) {
		if ((diag = kdbgetularg(argv[4], &npages))) {
			return diag;
		}
	}

	/* spaces after vaddr depends on sizeof(unsigned long) */
	kdb_printf("pfn              vaddr%*s pte\n",
		   (int)(2*sizeof(unsigned long) + 2 - 5), " ");

	for (g = 0, pgd = pgd_offset(mm, 0UL); g < PTRS_PER_PGD; ++g, ++pgd) {
		if (pgd_none(*pgd) || pgd_bad(*pgd))
			continue;
		for (u = 0, pud = pud_offset(pgd, 0UL); u < PTRS_PER_PUD; ++u, ++pud) {
			if (pud_none(*pud) || pud_bad(*pud))
				continue;
			for (m = 0, pmd = pmd_offset(pud, 0UL); m < PTRS_PER_PMD; ++m, ++pmd) {
				if (pmd_none(*pmd) || pmd_bad(*pmd))
					continue;
				for (t = 0, pte = pte_offset_map(pmd, 0UL); t < PTRS_PER_PTE; ++t, ++pte) {
					if (pte_none(*pte))
						continue;
					if (pte_pfn(*pte) < pfn || pte_pfn(*pte) >= (pfn + npages))
						continue;
					addr = g << PGDIR_SHIFT;
#ifdef __ia64__
					/* IA64 plays tricks with the pgd mapping to save space.
					 * This reverses pgd_index().
					 */
					{
						unsigned long region = g >> (PAGE_SHIFT - 6);
						unsigned long l1index = g - (region << (PAGE_SHIFT - 6));
						addr = (region << 61) + (l1index << PGDIR_SHIFT);
					}
#endif
					addr += (m << PMD_SHIFT) + (t << PAGE_SHIFT);
					kdb_printf("0x%-14lx " kdb_bfd_vma_fmt0 " ",
						   pte_pfn(*pte), addr);
					kdbm_print_pte(pte);
					kdb_printf("\n");
				}
			}
		}
	}

	return 0;
}

static int
kdbm_print_dentry(unsigned long daddr)
{
	struct dentry d;
	int diag;
	char buf[256];

	kdb_printf("Dentry at 0x%lx\n", daddr);
	if ((diag = kdb_getarea(d, (unsigned long)daddr)))
		return diag;

	if ((d.d_name.len > sizeof(buf)) || (diag = kdb_getarea_size(buf, (unsigned long)(d.d_name.name), d.d_name.len)))
		kdb_printf(" d_name.len = %d d_name.name = 0x%p\n",
					d.d_name.len, d.d_name.name);
	else
		kdb_printf(" d_name.len = %d d_name.name = 0x%p <%.*s>\n",
					d.d_name.len, d.d_name.name,
					(int)(d.d_name.len), d.d_name.name);

	kdb_printf(" d_count = %d d_flags = 0x%x d_inode = 0x%p\n",
					atomic_read(&d.d_count), d.d_flags, d.d_inode);

	kdb_printf(" d_parent = 0x%p\n", d.d_parent);

	kdb_printf(" d_hash.nxt = 0x%p d_hash.prv = 0x%p\n",
					d.d_hash.next, d.d_hash.pprev);

	kdb_printf(" d_lru.nxt = 0x%p d_lru.prv = 0x%p\n",
					d.d_lru.next, d.d_lru.prev);

	kdb_printf(" d_child.nxt = 0x%p d_child.prv = 0x%p\n",
					d.d_u.d_child.next, d.d_u.d_child.prev);

	kdb_printf(" d_subdirs.nxt = 0x%p d_subdirs.prv = 0x%p\n",
					d.d_subdirs.next, d.d_subdirs.prev);

	kdb_printf(" d_alias.nxt = 0x%p d_alias.prv = 0x%p\n",
					d.d_alias.next, d.d_alias.prev);

	kdb_printf(" d_op = 0x%p d_sb = 0x%p d_fsdata = 0x%p\n",
					d.d_op, d.d_sb, d.d_fsdata);

	kdb_printf(" d_iname = %s\n",
					d.d_iname);

	if (d.d_inode) {
		struct inode i;
		kdb_printf("\nInode Entry at 0x%p\n", d.d_inode);
		if ((diag = kdb_getarea(i, (unsigned long)d.d_inode)))
			return diag;
		kdb_printf(" i_mode = 0%o  i_nlink = %d  i_rdev = 0x%x\n",
						i.i_mode, i.i_nlink, i.i_rdev);

		kdb_printf(" i_ino = %ld i_count = %d\n",
						i.i_ino, atomic_read(&i.i_count));

		kdb_printf(" i_hash.nxt = 0x%p i_hash.prv = 0x%p\n",
						i.i_hash.next, i.i_hash.pprev);

		kdb_printf(" i_list.nxt = 0x%p i_list.prv = 0x%p\n",
						i.i_list.next, i.i_list.prev);

		kdb_printf(" i_dentry.nxt = 0x%p i_dentry.prv = 0x%p\n",
						i.i_dentry.next, i.i_dentry.prev);

	}
	kdb_printf("\n");
	return 0;
}

static int
kdbm_filp(int argc, const char **argv)
{
	struct file   f;
	int nextarg;
	unsigned long addr;
	long offset;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) ||
	    (diag = kdb_getarea(f, addr)))
		return diag;

	kdb_printf("File Pointer at 0x%lx\n", addr);

	kdb_printf(" fu_list.nxt = 0x%p fu_list.prv = 0x%p\n",
					f.f_u.fu_list.next, f.f_u.fu_list.prev);

	kdb_printf(" f_dentry = 0x%p f_vfsmnt = 0x%p f_op = 0x%p\n",
					f.f_dentry, f.f_vfsmnt, f.f_op);

	kdb_printf(" f_count = %d f_flags = 0x%x f_mode = 0x%x\n",
					atomic_read(&f.f_count), f.f_flags, f.f_mode);

	kdb_printf(" f_pos = %Ld\n", f.f_pos);
#ifdef	CONFIG_SECURITY
	kdb_printf(" security = 0x%p\n", f.f_security);
#endif

	kdb_printf(" private_data = 0x%p f_mapping = 0x%p\n\n",
					f.private_data, f.f_mapping);

	return kdbm_print_dentry((unsigned long)f.f_dentry);
}

static int
kdbm_fl(int argc, const char **argv)
{
	struct file_lock fl;
	int nextarg;
	unsigned long addr;
	long offset;
	int diag;


	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) ||
		(diag = kdb_getarea(fl, addr)))
			return diag;

	kdb_printf("File_lock at 0x%lx\n", addr);

	kdb_printf(" fl_next = 0x%p fl_link.nxt = 0x%p fl_link.prv = 0x%p\n",
			fl.fl_next, fl.fl_link.next, fl.fl_link.prev);
	kdb_printf(" fl_block.nxt = 0x%p fl_block.prv = 0x%p\n",
			fl.fl_block.next, fl.fl_block.prev);
	kdb_printf(" fl_owner = 0x%p fl_pid = %d fl_wait = 0x%p\n",
			fl.fl_owner, fl.fl_pid, &fl.fl_wait);
	kdb_printf(" fl_file = 0x%p fl_flags = 0x%x\n",
			fl.fl_file, fl.fl_flags);
	kdb_printf(" fl_type = %d fl_start = 0x%llx fl_end = 0x%llx\n",
			fl.fl_type, fl.fl_start, fl.fl_end);

	kdb_printf(" file_lock_operations");
	if (fl.fl_ops)
		kdb_printf("\n   fl_copy_lock = 0x%p fl_release_private = 0x%p\n",
			fl.fl_ops->fl_copy_lock, fl.fl_ops->fl_release_private);
	else
		kdb_printf("   empty\n");

	kdb_printf(" lock_manager_operations");
	if (fl.fl_lmops)
		kdb_printf("\n   fl_compare_owner = 0x%p fl_notify = 0x%p\n",
			fl.fl_lmops->fl_compare_owner, fl.fl_lmops->fl_notify);
	else
		kdb_printf("   empty\n");

	kdb_printf(" fl_fasync = 0x%p fl_break 0x%lx\n",
			fl.fl_fasync, fl.fl_break_time);

	return 0;
}


static int
kdbm_dentry(int argc, const char **argv)
{
	int nextarg;
	unsigned long addr;
	long offset;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)))
		return diag;

	return kdbm_print_dentry(addr);
}

static int
kdbm_kobject(int argc, const char **argv)
{
	struct kobject k;
	int nextarg;
	unsigned long addr;
	long offset;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) ||
	    (diag = kdb_getarea(k, addr)))
		return diag;


	kdb_printf("kobject at 0x%lx\n", addr);

	if (k.name) {
		char c;
		kdb_printf(" name 0x%p", k.name);
		if (kdb_getarea(c, (unsigned long)k.name) == 0)
			kdb_printf(" '%s'", k.name);
		kdb_printf("\n");
	}

	if (k.name != kobject_name((struct kobject *)addr))
		kdb_printf(" name '%." __stringify(KOBJ_NAME_LEN) "s'\n", k.name);

	kdb_printf(" kref.refcount %d'\n", atomic_read(&k.kref.refcount));

	kdb_printf(" entry.next = 0x%p entry.prev = 0x%p\n",
					k.entry.next, k.entry.prev);

	kdb_printf(" parent = 0x%p kset = 0x%p ktype = 0x%p sd = 0x%p\n",
					k.parent, k.kset, k.ktype, k.sd);

	return 0;
}

static int
kdbm_sh(int argc, const char **argv)
{
	int diag;
	int nextarg;
	unsigned long addr;
	long offset = 0L;
	struct Scsi_Host sh;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) ||
	    (diag = kdb_getarea(sh, addr)))
		return diag;

	kdb_printf("Scsi_Host at 0x%lx\n", addr);
	kdb_printf("host_queue = 0x%p\n", sh.__devices.next);
	kdb_printf("ehandler = 0x%p eh_action = 0x%p\n",
		   sh.ehandler, sh.eh_action);
	kdb_printf("host_wait = 0x%p hostt = 0x%p\n",
		   &sh.host_wait, sh.hostt);
	kdb_printf("host_failed = %d  host_no = %d resetting = %d\n",
		   sh.host_failed, sh.host_no, sh.resetting);
	kdb_printf("max id/lun/channel = [%d/%d/%d]  this_id = %d\n",
		   sh.max_id, sh.max_lun, sh.max_channel, sh.this_id);
	kdb_printf("can_queue = %d cmd_per_lun = %d  sg_tablesize = %d u_isa_dma = %d\n",
		   sh.can_queue, sh.cmd_per_lun, sh.sg_tablesize, sh.unchecked_isa_dma);
	kdb_printf("host_blocked = %d  reverse_ordering = %d \n",
		   sh.host_blocked, sh.reverse_ordering);

	return 0;
}

static int
kdbm_sd(int argc, const char **argv)
{
	int diag;
	int nextarg;
	unsigned long addr;
	long offset = 0L;
	struct scsi_device *sd = NULL;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)))
		goto out;
	if (!(sd = kmalloc(sizeof(*sd), GFP_ATOMIC))) {
		kdb_printf("kdbm_sd: cannot kmalloc sd\n");
		goto out;
	}
	if ((diag = kdb_getarea(*sd, addr)))
		goto out;

	kdb_printf("scsi_device at 0x%lx\n", addr);
	kdb_printf("next = 0x%p   prev = 0x%p  host = 0x%p\n",
		   sd->siblings.next, sd->siblings.prev, sd->host);
	kdb_printf("device_busy = %d   current_cmnd 0x%p\n",
		   sd->device_busy, sd->current_cmnd);
	kdb_printf("id/lun/chan = [%d/%d/%d]  single_lun = %d  device_blocked = %d\n",
		   sd->id, sd->lun, sd->channel, sd->sdev_target->single_lun, sd->device_blocked);
	kdb_printf("queue_depth = %d current_tag = %d  scsi_level = %d\n",
		   sd->queue_depth, sd->current_tag, sd->scsi_level);
	kdb_printf("%8.8s %16.16s %4.4s\n", sd->vendor, sd->model, sd->rev);
out:
	if (sd)
		kfree(sd);
	return diag;
}

static int
kdbm_sc(int argc, const char **argv)
{
	int diag;
	int nextarg;
	unsigned long addr;
	long offset = 0L;
	struct scsi_cmnd *sc = NULL;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)))
		goto out;
	if (!(sc = kmalloc(sizeof(*sc), GFP_ATOMIC))) {
		kdb_printf("kdbm_sc: cannot kmalloc sc\n");
		goto out;
	}
	if ((diag = kdb_getarea(*sc, addr)))
		goto out;

	kdb_printf("scsi_cmnd at 0x%lx\n", addr);
	kdb_printf("device = 0x%p  next = 0x%p\n",
		   sc->device, sc->list.next);
	kdb_printf("serial_number = %ld  retries = %d\n",
		   sc->serial_number, sc->retries);
	kdb_printf("cmd_len = %d\n", sc->cmd_len);
	kdb_printf("cmnd = [%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x]\n",
		   sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3], sc->cmnd[4],
		   sc->cmnd[5], sc->cmnd[6], sc->cmnd[7], sc->cmnd[8], sc->cmnd[9],
		   sc->cmnd[10], sc->cmnd[11]);
	kdb_printf("request_buffer = 0x%p  request_bufflen = %d\n",
		   scsi_sglist(sc), scsi_bufflen(sc));
	kdb_printf("use_sg = %d\n", scsi_sg_count(sc));
	kdb_printf("underflow = %d transfersize = %d\n",
		   sc->underflow, sc->transfersize);
	kdb_printf("tag = %d\n", sc->tag);

out:
	if (sc)
		kfree(sc);
	return diag;
}

static int __init kdbm_vm_init(void)
{
	kdb_register("vm", kdbm_vm, "[-v] <vaddr>", "Display vm_area_struct", 0);
	kdb_register("vmp", kdbm_vm, "[-v] <pid>", "Display all vm_area_struct for <pid>", 0);
	kdb_register("pte", kdbm_pte, "( -m <mm> | -p <pid> ) <vaddr> [<nbytes>]", "Display pte_t for mm_struct or pid", 0);
	kdb_register("rpte", kdbm_rpte, "( -m <mm> | -p <pid> ) <pfn> [<npages>]", "Find pte_t containing pfn for mm_struct or pid", 0);
	kdb_register("dentry", kdbm_dentry, "<dentry>", "Display interesting dentry stuff", 0);
	kdb_register("kobject", kdbm_kobject, "<kobject>", "Display interesting kobject stuff", 0);
	kdb_register("filp", kdbm_filp, "<filp>", "Display interesting filp stuff", 0);
	kdb_register("fl", kdbm_fl, "<fl>", "Display interesting file_lock stuff", 0);
	kdb_register("sh", kdbm_sh, "<vaddr>", "Show scsi_host", 0);
	kdb_register("sd", kdbm_sd, "<vaddr>", "Show scsi_device", 0);
	kdb_register("sc", kdbm_sc, "<vaddr>", "Show scsi_cmnd", 0);

	return 0;
}

static void __exit kdbm_vm_exit(void)
{
	kdb_unregister("vm");
	kdb_unregister("vmp");
	kdb_unregister("pte");
	kdb_unregister("rpte");
	kdb_unregister("dentry");
	kdb_unregister("kobject");
	kdb_unregister("filp");
	kdb_unregister("fl");
	kdb_unregister("sh");
	kdb_unregister("sd");
	kdb_unregister("sc");
}

module_init(kdbm_vm_init)
module_exit(kdbm_vm_exit)
