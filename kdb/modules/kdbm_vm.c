/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
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

MODULE_AUTHOR("SGI");
MODULE_DESCRIPTION("Debug VM information");
MODULE_LICENSE("GPL");

struct __vmflags {
	unsigned long mask;
	char *name;
};

static struct __vmflags vmflags[] = {
	{ VM_READ, "READ" },
	{ VM_WRITE, "WRITE" },
	{ VM_EXEC, "EXEC" },
	{ VM_SHARED, "SHARED" },
	{ VM_MAYREAD, "MAYREAD" },
	{ VM_MAYWRITE, "MAYWRITE" },
	{ VM_MAYEXEC, "MAYEXEC" },
	{ VM_MAYSHARE, "MAYSHARE" },
	{ VM_GROWSDOWN, "GROWSDOWN" },
	{ VM_GROWSUP, "GROWSUP" },
	{ VM_SHM, "SHM" },
	{ VM_DENYWRITE, "DENYWRITE" },
	{ VM_EXECUTABLE, "EXECUTABLE" },
	{ VM_LOCKED, "LOCKED" },
	{ VM_IO , "IO " },
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
		kdb_printf("vm_ops->nopage = 0x%p\n", vp->vm_ops->nopage);
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
kdbm_vm(int argc, const char **argv, const char **envp, struct pt_regs *regs)
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
					  NULL, regs))
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
		if (pte_exec(*pte))
			kdb_printf("X");
		if (pte_write(*pte))
			kdb_printf("W");
		if (pte_read(*pte))
			kdb_printf("R");
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
kdbm_pte(int argc, const char **argv, const char **envp, struct pt_regs *regs)
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
	struct mm_struct *mm;
	pgd_t *pgd;
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
		struct mm_struct copy_of_mm;


		nextarg = 2;
		if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset,
					  NULL, regs))
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
			pmd = pmd_offset(pgd, addr);
			if (pmd_present(*pmd)) {
				pte = pte_offset_map(pmd, addr);
				if (pte_present(*pte)) {
					kdbm_print_pte(pte);
				}
			}
		}

		kdb_printf("\n");
		addr += PAGE_SIZE;
	}

	return 0;
}

static int
kdbm_fp(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	struct file   f;
	struct inode *i = NULL;
	struct dentry d;
	int nextarg;
	unsigned long addr;
	long offset;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) ||
	    (diag = kdb_getarea(f, addr)) ||
	    (diag = kdb_getarea(d, (unsigned long)f.f_dentry)))
		goto out;
	if (!(i = kmalloc(sizeof(*i), GFP_ATOMIC))) {
		kdb_printf("kdbm_fp: cannot kmalloc inode\n");
		goto out;
	}
	if ((diag = kdb_getarea(*i, (unsigned long)d.d_inode)))
		goto out;

	kdb_printf("name.name 0x%p  name.len  %d\n",
		    d.d_name.name, d.d_name.len);

	kdb_printf("File Pointer at 0x%lx\n", addr);

	kdb_printf(" f_list.nxt = 0x%p f_list.prv = 0x%p\n",
					f.f_list.next, f.f_list.prev);

	kdb_printf(" f_dentry = 0x%p f_op = 0x%p\n",
					f.f_dentry, f.f_op);

	kdb_printf(" f_count = %d f_flags = 0x%x f_mode = 0x%x\n",
					f.f_count.counter, f.f_flags, f.f_mode);

	kdb_printf(" f_pos = %Ld\n",
					f.f_pos);

	kdb_printf("\nDirectory Entry at 0x%p\n", f.f_dentry);
	kdb_printf(" d_name.len = %d d_name.name = 0x%p>\n",
					d.d_name.len, d.d_name.name);

	kdb_printf(" d_count = %d d_flags = 0x%x d_inode = 0x%p\n",
					atomic_read(&d.d_count), d.d_flags, d.d_inode);

	kdb_printf(" d_hash.nxt = 0x%p d_hash.prv = 0x%p\n",
					d.d_hash.next, d.d_hash.pprev);

	kdb_printf(" d_lru.nxt = 0x%p d_lru.prv = 0x%p\n",
					d.d_lru.next, d.d_lru.prev);

	kdb_printf(" d_child.nxt = 0x%p d_child.prv = 0x%p\n",
					d.d_child.next, d.d_child.prev);

	kdb_printf(" d_subdirs.nxt = 0x%p d_subdirs.prv = 0x%p\n",
					d.d_subdirs.next, d.d_subdirs.prev);

	kdb_printf(" d_alias.nxt = 0x%p d_alias.prv = 0x%p\n",
					d.d_alias.next, d.d_alias.prev);

	kdb_printf(" d_op = 0x%p d_sb = 0x%p\n\n",
					d.d_op, d.d_sb);


	kdb_printf("\nInode Entry at 0x%p\n", d.d_inode);

	kdb_printf(" i_mode = 0%o  i_nlink = %d  i_rdev = 0x%x\n",
					i->i_mode, i->i_nlink, i->i_rdev);

	kdb_printf(" i_ino = %ld i_count = %d\n",
					i->i_ino, atomic_read(&i->i_count));

	kdb_printf(" i_hash.nxt = 0x%p i_hash.prv = 0x%p\n",
					i->i_hash.next, i->i_hash.pprev);

	kdb_printf(" i_list.nxt = 0x%p i_list.prv = 0x%p\n",
					i->i_list.next, i->i_list.prev);

	kdb_printf(" i_dentry.nxt = 0x%p i_dentry.prv = 0x%p\n",
					i->i_dentry.next, i->i_dentry.prev);

out:
	if (i)
		kfree(i);
	return diag;
}

static int
kdbm_fl(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	struct file_lock fl;
	int nextarg;
	unsigned long addr;
	long offset;
	int diag;


	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) ||
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

	kdb_printf(" fl_notify = 0x%p fl_insert = 0x%p fl_remove = 0x%p\n",
			fl.fl_notify, fl.fl_insert, fl.fl_remove);

	kdb_printf(" fl_fasync = 0x%p fl_break 0x%lx\n",
			fl.fl_fasync, fl.fl_break_time);

	return 0;
}


static int
kdbm_dentry(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	struct dentry d;
	int nextarg;
	unsigned long addr;
	long offset;
	int diag;
	char buf[256];

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) ||
	    (diag = kdb_getarea(d, addr)))
		return diag;


	kdb_printf("Dentry at 0x%lx\n", addr);

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
					d.d_child.next, d.d_child.prev);

	kdb_printf(" d_subdirs.nxt = 0x%p d_subdirs.prv = 0x%p\n",
					d.d_subdirs.next, d.d_subdirs.prev);

	kdb_printf(" d_alias.nxt = 0x%p d_alias.prv = 0x%p\n",
					d.d_alias.next, d.d_alias.prev);

	kdb_printf(" d_op = 0x%p d_sb = 0x%p\n\n",
					d.d_op, d.d_sb);

	return 0;
}

static int
kdbm_sh(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	int nextarg;
	unsigned long addr;
	long offset = 0L;
	struct Scsi_Host sh;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)) ||
	    (diag = kdb_getarea(sh, addr)))
		return diag;

	kdb_printf("Scsi_Host at 0x%lx\n", addr);
	kdb_printf("host_queue = 0x%p\n", sh.__devices.next);
	kdb_printf("ehandler = 0x%p eh_wait = 0x%p  en_notify = 0x%p eh_action = 0x%p\n",
		   sh.ehandler, sh.eh_wait, sh.eh_notify, sh.eh_action);
	kdb_printf("eh_active = 0x%d host_wait = 0x%p hostt = 0x%p\n",
		   sh.eh_active, &sh.host_wait, sh.hostt);
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
kdbm_sd(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	int nextarg;
	unsigned long addr;
	long offset = 0L;
	struct scsi_device *sd = NULL;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)))
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
		   sd->id, sd->lun, sd->channel, sd->single_lun, sd->device_blocked);
	kdb_printf("queue_depth = %d current_tag = %d  scsi_level = %d\n",
		   sd->queue_depth, sd->current_tag, sd->scsi_level);
	kdb_printf("%8.8s %16.16s %4.4s\n", sd->vendor, sd->model, sd->rev);
out:
	if (sd)
		kfree(sd);
	return diag;
}

static int
kdbm_sc(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int diag;
	int nextarg;
	unsigned long addr;
	long offset = 0L;
	struct scsi_cmnd *sc = NULL;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs)))
		goto out;
	if (!(sc = kmalloc(sizeof(*sc), GFP_ATOMIC))) {
		kdb_printf("kdbm_sc: cannot kmalloc sc\n");
		goto out;
	}
	if ((diag = kdb_getarea(*sc, addr)))
		goto out;

	kdb_printf("scsi_cmnd at 0x%lx\n", addr);
	kdb_printf("state = %d  owner = %d  device = 0x%p\nb",
		    sc->state, sc->owner, sc->device);
	kdb_printf("next = 0x%p  eh_state = %d done = 0x%p\n",
		   sc->list.next, sc->eh_state, sc->done);
	kdb_printf("serial_number = %ld  serial_num_at_to = %ld retries = %d timeout = %d\n",
		   sc->serial_number, sc->serial_number_at_timeout, sc->retries, sc->timeout);
	kdb_printf("cmd_len = %d  old_cmd_len = %d\n",
		   sc->cmd_len, sc->old_cmd_len);
	kdb_printf("cmnd = [%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x]\n",
		   sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3], sc->cmnd[4],
		   sc->cmnd[5], sc->cmnd[6], sc->cmnd[7], sc->cmnd[8], sc->cmnd[9],
		   sc->cmnd[10], sc->cmnd[11]);
	kdb_printf("data_cmnd = [%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x/%2.2x]\n",
		   sc->data_cmnd[0], sc->data_cmnd[1], sc->data_cmnd[2], sc->data_cmnd[3], sc->data_cmnd[4],
		   sc->data_cmnd[5], sc->data_cmnd[6], sc->data_cmnd[7], sc->data_cmnd[8], sc->data_cmnd[9],
		   sc->data_cmnd[10], sc->data_cmnd[11]);
	kdb_printf("request_buffer = 0x%p  request_bufflen = %d\n",
		   sc->request_buffer, sc->request_bufflen);
	kdb_printf("use_sg = %d  old_use_sg = %d sglist_len = %d abore_reason = %d\n",
		   sc->use_sg, sc->old_use_sg, sc->sglist_len, sc->abort_reason);
	kdb_printf("bufflen = %d  buffer = 0x%p  underflow = %d transfersize = %d\n",
		   sc->bufflen, sc->buffer, sc->underflow, sc->transfersize);
	kdb_printf("tag = %d pid = %ld\n",
		   sc->tag, sc->pid);

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
	kdb_register("dentry", kdbm_dentry, "<dentry>", "Display interesting dentry stuff", 0);
	kdb_register("filp", kdbm_fp, "<filp>", "Display interesting filp stuff", 0);
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
	kdb_unregister("dentry");
	kdb_unregister("filp");
	kdb_unregister("fl");
	kdb_unregister("sh");
	kdb_unregister("sd");
	kdb_unregister("sc");
}

module_init(kdbm_vm_init)
module_exit(kdbm_vm_exit)
