/*
 *  linux/fs/exec.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */
/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 *
 * Demand loading changed July 1993 by Eric Youngdale.   Use mmap instead,
 * current->executable is only used by the procfs.  This allows a dispatch
 * table to check for several different types  of binary formats.  We keep
 * trying until we recognize the file or we run out of supported binary
 * formats. 
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/swap.h>
#include <linux/utsname.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/rmap-locking.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

int core_uses_pid;
char core_pattern[65] = "core";
/* The maximal length of core_pattern is also specified in sysctl.c */

static struct linux_binfmt *formats;
static rwlock_t binfmt_lock = RW_LOCK_UNLOCKED;

int register_binfmt(struct linux_binfmt * fmt)
{
	struct linux_binfmt ** tmp = &formats;

	if (!fmt)
		return -EINVAL;
	if (fmt->next)
		return -EBUSY;
	write_lock(&binfmt_lock);
	while (*tmp) {
		if (fmt == *tmp) {
			write_unlock(&binfmt_lock);
			return -EBUSY;
		}
		tmp = &(*tmp)->next;
	}
	fmt->next = formats;
	formats = fmt;
	write_unlock(&binfmt_lock);
	return 0;	
}

EXPORT_SYMBOL(register_binfmt);

int unregister_binfmt(struct linux_binfmt * fmt)
{
	struct linux_binfmt ** tmp = &formats;

	write_lock(&binfmt_lock);
	while (*tmp) {
		if (fmt == *tmp) {
			*tmp = fmt->next;
			write_unlock(&binfmt_lock);
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	write_unlock(&binfmt_lock);
	return -EINVAL;
}

EXPORT_SYMBOL(unregister_binfmt);

static inline void put_binfmt(struct linux_binfmt * fmt)
{
	module_put(fmt->module);
}

/*
 * Note that a shared library must be both readable and executable due to
 * security reasons.
 *
 * Also note that we take the address to load from from the file itself.
 */
asmlinkage long sys_uselib(const char __user * library)
{
	struct file * file;
	struct nameidata nd;
	int error;

	nd.intent.open.flags = FMODE_READ;
	error = __user_walk(library, LOOKUP_FOLLOW|LOOKUP_OPEN, &nd);
	if (error)
		goto out;

	error = -EINVAL;
	if (!S_ISREG(nd.dentry->d_inode->i_mode))
		goto exit;

	error = permission(nd.dentry->d_inode, MAY_READ | MAY_EXEC, &nd);
	if (error)
		goto exit;

	file = dentry_open(nd.dentry, nd.mnt, O_RDONLY);
	error = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;

	error = -ENOEXEC;
	if(file->f_op) {
		struct linux_binfmt * fmt;

		read_lock(&binfmt_lock);
		for (fmt = formats ; fmt ; fmt = fmt->next) {
			if (!fmt->load_shlib)
				continue;
			if (!try_module_get(fmt->module))
				continue;
			read_unlock(&binfmt_lock);
			error = fmt->load_shlib(file);
			read_lock(&binfmt_lock);
			put_binfmt(fmt);
			if (error != -ENOEXEC)
				break;
		}
		read_unlock(&binfmt_lock);
	}
	fput(file);
out:
  	return error;
exit:
	path_release(&nd);
	goto out;
}

/*
 * count() counts the number of strings in array ARGV.
 */
static int count(char __user * __user * argv, int max)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			char __user * p;

			if (get_user(p, argv))
				return -EFAULT;
			if (!p)
				break;
			argv++;
			if(++i > max)
				return -E2BIG;
		}
	}
	return i;
}

/*
 * 'copy_strings()' copies argument/environment strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 */
int copy_strings(int argc,char __user * __user * argv, struct linux_binprm *bprm)
{
	struct page *kmapped_page = NULL;
	char *kaddr = NULL;
	int ret;

	while (argc-- > 0) {
		char __user *str;
		int len;
		unsigned long pos;

		if (get_user(str, argv+argc) ||
				!(len = strnlen_user(str, bprm->p))) {
			ret = -EFAULT;
			goto out;
		}

		if (bprm->p < len)  {
			ret = -E2BIG;
			goto out;
		}

		bprm->p -= len;
		/* XXX: add architecture specific overflow check here. */
		pos = bprm->p;

		while (len > 0) {
			int i, new, err;
			int offset, bytes_to_copy;
			struct page *page;

			offset = pos % PAGE_SIZE;
			i = pos/PAGE_SIZE;
			page = bprm->page[i];
			new = 0;
			if (!page) {
				page = alloc_page(GFP_HIGHUSER);
				bprm->page[i] = page;
				if (!page) {
					ret = -ENOMEM;
					goto out;
				}
				new = 1;
			}

			if (page != kmapped_page) {
				if (kmapped_page)
					kunmap(kmapped_page);
				kmapped_page = page;
				kaddr = kmap(kmapped_page);
			}
			if (new && offset)
				memset(kaddr, 0, offset);
			bytes_to_copy = PAGE_SIZE - offset;
			if (bytes_to_copy > len) {
				bytes_to_copy = len;
				if (new)
					memset(kaddr+offset+len, 0,
						PAGE_SIZE-offset-len);
			}
			err = copy_from_user(kaddr+offset, str, bytes_to_copy);
			if (err) {
				ret = -EFAULT;
				goto out;
			}

			pos += bytes_to_copy;
			str += bytes_to_copy;
			len -= bytes_to_copy;
		}
	}
	ret = 0;
out:
	if (kmapped_page)
		kunmap(kmapped_page);
	return ret;
}

/*
 * Like copy_strings, but get argv and its values from kernel memory.
 */
int copy_strings_kernel(int argc,char ** argv, struct linux_binprm *bprm)
{
	int r;
	mm_segment_t oldfs = get_fs();
	set_fs(KERNEL_DS);
	r = copy_strings(argc, (char __user * __user *)argv, bprm);
	set_fs(oldfs);
	return r;
}

EXPORT_SYMBOL(copy_strings_kernel);

#ifdef CONFIG_MMU
/*
 * This routine is used to map in a page into an address space: needed by
 * execve() for the initial stack and environment pages.
 *
 * tsk->mmap_sem is held for writing.
 */
void put_dirty_page(struct task_struct *tsk, struct page *page,
			unsigned long address, pgprot_t prot)
{
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte;
	struct pte_chain *pte_chain;

	if (page_count(page) != 1)
		printk(KERN_ERR "mem_map disagrees with %p at %08lx\n",
				page, address);

	pgd = pgd_offset(tsk->mm, address);
	pte_chain = pte_chain_alloc(GFP_KERNEL);
	if (!pte_chain)
		goto out_sig;
	spin_lock(&tsk->mm->page_table_lock);
	pmd = pmd_alloc(tsk->mm, pgd, address);
	if (!pmd)
		goto out;
	pte = pte_alloc_map(tsk->mm, pmd, address);
	if (!pte)
		goto out;
	if (!pte_none(*pte)) {
		pte_unmap(pte);
		goto out;
	}
	lru_cache_add_active(page);
	flush_dcache_page(page);
	set_pte(pte, pte_mkdirty(pte_mkwrite(mk_pte(page, prot))));
	pte_chain = page_add_rmap(page, pte, pte_chain);
	pte_unmap(pte);
	tsk->mm->rss++;
	spin_unlock(&tsk->mm->page_table_lock);

	/* no need for flush_tlb */
	pte_chain_free(pte_chain);
	return;
out:
	spin_unlock(&tsk->mm->page_table_lock);
out_sig:
	__free_page(page);
	force_sig(SIGKILL, tsk);
	pte_chain_free(pte_chain);
	return;
}

int setup_arg_pages(struct linux_binprm *bprm, int executable_stack)
{
	unsigned long stack_base;
	struct vm_area_struct *mpnt;
	struct mm_struct *mm = current->mm;
	int i;
	long arg_size;

#ifdef CONFIG_STACK_GROWSUP
	/* Move the argument and environment strings to the bottom of the
	 * stack space.
	 */
	int offset, j;
	char *to, *from;

	/* Start by shifting all the pages down */
	i = 0;
	for (j = 0; j < MAX_ARG_PAGES; j++) {
		struct page *page = bprm->page[j];
		if (!page)
			continue;
		bprm->page[i++] = page;
	}

	/* Now move them within their pages */
	offset = bprm->p % PAGE_SIZE;
	to = kmap(bprm->page[0]);
	for (j = 1; j < i; j++) {
		memmove(to, to + offset, PAGE_SIZE - offset);
		from = kmap(bprm->page[j]);
		memcpy(to + PAGE_SIZE - offset, from, offset);
		kunmap(bprm->page[j - 1]);
		to = from;
	}
	memmove(to, to + offset, PAGE_SIZE - offset);
	kunmap(bprm->page[j - 1]);

	/* Adjust bprm->p to point to the end of the strings. */
	bprm->p = PAGE_SIZE * i - offset;

	/* Limit stack size to 1GB */
	stack_base = current->rlim[RLIMIT_STACK].rlim_max;
	if (stack_base > (1 << 30))
		stack_base = 1 << 30;
	stack_base = PAGE_ALIGN(STACK_TOP - stack_base);

	mm->arg_start = stack_base;
	arg_size = i << PAGE_SHIFT;

	/* zero pages that were copied above */
	while (i < MAX_ARG_PAGES)
		bprm->page[i++] = NULL;
#else
	stack_base = STACK_TOP - MAX_ARG_PAGES * PAGE_SIZE;
	mm->arg_start = bprm->p + stack_base;
	arg_size = STACK_TOP - (PAGE_MASK & (unsigned long) mm->arg_start);
#endif

	bprm->p += stack_base;
	if (bprm->loader)
		bprm->loader += stack_base;
	bprm->exec += stack_base;

	mpnt = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!mpnt)
		return -ENOMEM;

	if (security_vm_enough_memory(arg_size >> PAGE_SHIFT)) {
		kmem_cache_free(vm_area_cachep, mpnt);
		return -ENOMEM;
	}

	down_write(&mm->mmap_sem);
	{
		mpnt->vm_mm = mm;
#ifdef CONFIG_STACK_GROWSUP
		mpnt->vm_start = stack_base;
		mpnt->vm_end = PAGE_MASK &
			(PAGE_SIZE - 1 + (unsigned long) bprm->p);
#else
		mpnt->vm_start = PAGE_MASK & (unsigned long) bprm->p;
		mpnt->vm_end = STACK_TOP;
#endif
		/* Adjust stack execute permissions; explicitly enable
		 * for EXSTACK_ENABLE_X, disable for EXSTACK_DISABLE_X
		 * and leave alone (arch default) otherwise. */
		if (unlikely(executable_stack == EXSTACK_ENABLE_X))
			mpnt->vm_flags = VM_STACK_FLAGS |  VM_EXEC;
		else if (executable_stack == EXSTACK_DISABLE_X)
			mpnt->vm_flags = VM_STACK_FLAGS & ~VM_EXEC;
		else
			mpnt->vm_flags = VM_STACK_FLAGS;
		mpnt->vm_page_prot = protection_map[mpnt->vm_flags & 0x7];
		mpnt->vm_ops = NULL;
		mpnt->vm_pgoff = 0;
		mpnt->vm_file = NULL;
		INIT_LIST_HEAD(&mpnt->shared);
		mpnt->vm_private_data = (void *) 0;
		insert_vm_struct(mm, mpnt);
		mm->total_vm = (mpnt->vm_end - mpnt->vm_start) >> PAGE_SHIFT;
	}

	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		struct page *page = bprm->page[i];
		if (page) {
			bprm->page[i] = NULL;
			put_dirty_page(current, page, stack_base,
					mpnt->vm_page_prot);
		}
		stack_base += PAGE_SIZE;
	}
	up_write(&mm->mmap_sem);
	
	return 0;
}

EXPORT_SYMBOL(setup_arg_pages);

#define free_arg_pages(bprm) do { } while (0)

#else

static inline void free_arg_pages(struct linux_binprm *bprm)
{
	int i;

	for (i = 0; i < MAX_ARG_PAGES; i++) {
		if (bprm->page[i])
			__free_page(bprm->page[i]);
		bprm->page[i] = NULL;
	}
}

#endif /* CONFIG_MMU */

struct file *open_exec(const char *name)
{
	struct nameidata nd;
	int err;
	struct file *file;

	nd.intent.open.flags = FMODE_READ;
	err = path_lookup(name, LOOKUP_FOLLOW|LOOKUP_OPEN, &nd);
	file = ERR_PTR(err);

	if (!err) {
		struct inode *inode = nd.dentry->d_inode;
		file = ERR_PTR(-EACCES);
		if (!(nd.mnt->mnt_flags & MNT_NOEXEC) &&
		    S_ISREG(inode->i_mode)) {
			int err = permission(inode, MAY_EXEC, &nd);
			if (!err && !(inode->i_mode & 0111))
				err = -EACCES;
			file = ERR_PTR(err);
			if (!err) {
				file = dentry_open(nd.dentry, nd.mnt, O_RDONLY);
				if (!IS_ERR(file)) {
					err = deny_write_access(file);
					if (err) {
						fput(file);
						file = ERR_PTR(err);
					}
				}
out:
				return file;
			}
		}
		path_release(&nd);
	}
	goto out;
}

EXPORT_SYMBOL(open_exec);

int kernel_read(struct file *file, unsigned long offset,
	char *addr, unsigned long count)
{
	mm_segment_t old_fs;
	loff_t pos = offset;
	int result;

	old_fs = get_fs();
	set_fs(get_ds());
	/* The cast to a user pointer is valid due to the set_fs() */
	result = vfs_read(file, (void __user *)addr, count, &pos);
	set_fs(old_fs);
	return result;
}

EXPORT_SYMBOL(kernel_read);

static int exec_mmap(struct mm_struct *mm)
{
	struct task_struct *tsk;
	struct mm_struct * old_mm, *active_mm;

	/* Add it to the list of mm's */
	spin_lock(&mmlist_lock);
	list_add(&mm->mmlist, &init_mm.mmlist);
	mmlist_nr++;
	spin_unlock(&mmlist_lock);

	/* Notify parent that we're no longer interested in the old VM */
	tsk = current;
	old_mm = current->mm;
	mm_release(tsk, old_mm);

	task_lock(tsk);
	active_mm = tsk->active_mm;
	tsk->mm = mm;
	tsk->active_mm = mm;
	activate_mm(active_mm, mm);
	task_unlock(tsk);
	if (old_mm) {
		if (active_mm != old_mm) BUG();
		mmput(old_mm);
		return 0;
	}
	mmdrop(active_mm);
	return 0;
}

/*
 * This function makes sure the current process has its own signal table,
 * so that flush_signal_handlers can later reset the handlers without
 * disturbing other processes.  (Other processes might share the signal
 * table via the CLONE_SIGHAND option to clone().)
 */
static inline int de_thread(struct task_struct *tsk)
{
	struct signal_struct *newsig, *oldsig = tsk->signal;
	struct sighand_struct *newsighand, *oldsighand = tsk->sighand;
	spinlock_t *lock = &oldsighand->siglock;
	int count;

	/*
	 * If we don't share sighandlers, then we aren't sharing anything
	 * and we can just re-use it all.
	 */
	if (atomic_read(&oldsighand->count) <= 1)
		return 0;

	newsighand = kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
	if (!newsighand)
		return -ENOMEM;

	spin_lock_init(&newsighand->siglock);
	atomic_set(&newsighand->count, 1);
	memcpy(newsighand->action, oldsighand->action, sizeof(newsighand->action));

	/*
	 * See if we need to allocate a new signal structure
	 */
	newsig = NULL;
	if (atomic_read(&oldsig->count) > 1) {
		newsig = kmem_cache_alloc(signal_cachep, GFP_KERNEL);
		if (!newsig) {
			kmem_cache_free(sighand_cachep, newsighand);
			return -ENOMEM;
		}
		atomic_set(&newsig->count, 1);
		newsig->group_exit = 0;
		newsig->group_exit_code = 0;
		newsig->group_exit_task = NULL;
		newsig->group_stop_count = 0;
		newsig->curr_target = NULL;
		init_sigpending(&newsig->shared_pending);

		newsig->pgrp = oldsig->pgrp;
		newsig->session = oldsig->session;
		newsig->leader = oldsig->leader;
		newsig->tty_old_pgrp = oldsig->tty_old_pgrp;
	}

	if (thread_group_empty(current))
		goto no_thread_group;

	/*
	 * Kill all other threads in the thread group.
	 * We must hold tasklist_lock to call zap_other_threads.
	 */
	read_lock(&tasklist_lock);
	spin_lock_irq(lock);
	if (oldsig->group_exit) {
		/*
		 * Another group action in progress, just
		 * return so that the signal is processed.
		 */
		spin_unlock_irq(lock);
		read_unlock(&tasklist_lock);
		kmem_cache_free(sighand_cachep, newsighand);
		if (newsig)
			kmem_cache_free(signal_cachep, newsig);
		return -EAGAIN;
	}
	oldsig->group_exit = 1;
	zap_other_threads(current);
	read_unlock(&tasklist_lock);

	/*
	 * Account for the thread group leader hanging around:
	 */
	count = 2;
	if (current->pid == current->tgid)
		count = 1;
	while (atomic_read(&oldsig->count) > count) {
		oldsig->group_exit_task = current;
		oldsig->notify_count = count;
		__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(lock);
		schedule();
		spin_lock_irq(lock);
	}
	spin_unlock_irq(lock);

	/*
	 * At this point all other threads have exited, all we have to
	 * do is to wait for the thread group leader to become inactive,
	 * and to assume its PID:
	 */
	if (current->pid != current->tgid) {
		struct task_struct *leader = current->group_leader, *parent;
		struct dentry *proc_dentry1, *proc_dentry2;
		unsigned long state, ptrace;

		/*
		 * Wait for the thread group leader to be a zombie.
		 * It should already be zombie at this point, most
		 * of the time.
		 */
		while (leader->state != TASK_ZOMBIE)
			yield();

		spin_lock(&leader->proc_lock);
		spin_lock(&current->proc_lock);
		proc_dentry1 = proc_pid_unhash(current);
		proc_dentry2 = proc_pid_unhash(leader);
		write_lock_irq(&tasklist_lock);

		if (leader->tgid != current->tgid)
			BUG();
		if (current->pid == current->tgid)
			BUG();
		/*
		 * An exec() starts a new thread group with the
		 * TGID of the previous thread group. Rehash the
		 * two threads with a switched PID, and release
		 * the former thread group leader:
		 */
		ptrace = leader->ptrace;
		parent = leader->parent;

		ptrace_unlink(current);
		ptrace_unlink(leader);
		remove_parent(current);
		remove_parent(leader);

		switch_exec_pids(leader, current);

		current->parent = current->real_parent = leader->real_parent;
		leader->parent = leader->real_parent = child_reaper;
		current->group_leader = current;
		leader->group_leader = leader;

		add_parent(current, current->parent);
		add_parent(leader, leader->parent);
		if (ptrace) {
			current->ptrace = ptrace;
			__ptrace_link(current, parent);
		}

		list_del(&current->tasks);
		list_add_tail(&current->tasks, &init_task.tasks);
		current->exit_signal = SIGCHLD;
		state = leader->state;

		write_unlock_irq(&tasklist_lock);
		spin_unlock(&leader->proc_lock);
		spin_unlock(&current->proc_lock);
		proc_pid_flush(proc_dentry1);
		proc_pid_flush(proc_dentry2);

		if (state != TASK_ZOMBIE)
			BUG();
		release_task(leader);
        }

no_thread_group:

	write_lock_irq(&tasklist_lock);
	spin_lock(&oldsighand->siglock);
	spin_lock(&newsighand->siglock);

	if (current == oldsig->curr_target)
		oldsig->curr_target = next_thread(current);
	if (newsig)
		current->signal = newsig;
	current->sighand = newsighand;
	init_sigpending(&current->pending);
	recalc_sigpending();

	spin_unlock(&newsighand->siglock);
	spin_unlock(&oldsighand->siglock);
	write_unlock_irq(&tasklist_lock);

	if (newsig && atomic_dec_and_test(&oldsig->count))
		kmem_cache_free(signal_cachep, oldsig);

	if (atomic_dec_and_test(&oldsighand->count))
		kmem_cache_free(sighand_cachep, oldsighand);

	if (!thread_group_empty(current))
		BUG();
	if (current->tgid != current->pid)
		BUG();
	return 0;
}
	
/*
 * These functions flushes out all traces of the currently running executable
 * so that a new one can be started
 */

static inline void flush_old_files(struct files_struct * files)
{
	long j = -1;

	spin_lock(&files->file_lock);
	for (;;) {
		unsigned long set, i;

		j++;
		i = j * __NFDBITS;
		if (i >= files->max_fds || i >= files->max_fdset)
			break;
		set = files->close_on_exec->fds_bits[j];
		if (!set)
			continue;
		files->close_on_exec->fds_bits[j] = 0;
		spin_unlock(&files->file_lock);
		for ( ; set ; i++,set >>= 1) {
			if (set & 1) {
				sys_close(i);
			}
		}
		spin_lock(&files->file_lock);

	}
	spin_unlock(&files->file_lock);
}

int flush_old_exec(struct linux_binprm * bprm)
{
	char * name;
	int i, ch, retval;
	struct files_struct *files;

	/*
	 * Make sure we have a private signal table and that
	 * we are unassociated from the previous thread group.
	 */
	retval = de_thread(current);
	if (retval)
		goto out;

	/*
	 * Make sure we have private file handles. Ask the
	 * fork helper to do the work for us and the exit
	 * helper to do the cleanup of the old one.
	 */
	files = current->files;		/* refcounted so safe to hold */
	retval = unshare_files();
	if (retval)
		goto out;
	/*
	 * Release all of the old mmap stuff
	 */
	retval = exec_mmap(bprm->mm);
	if (retval)
		goto mmap_failed;

	bprm->mm = NULL;		/* We're using it now */

	/* This is the point of no return */
	steal_locks(files);
	put_files_struct(files);

	current->sas_ss_sp = current->sas_ss_size = 0;

	if (current->euid == current->uid && current->egid == current->gid)
		current->mm->dumpable = 1;
	name = bprm->filename;
	for (i=0; (ch = *(name++)) != '\0';) {
		if (ch == '/')
			i = 0;
		else
			if (i < 15)
				current->comm[i++] = ch;
	}
	current->comm[i] = '\0';

	flush_thread();

	if (bprm->e_uid != current->euid || bprm->e_gid != current->egid || 
	    permission(bprm->file->f_dentry->d_inode,MAY_READ, NULL))
		current->mm->dumpable = 0;

	/* An exec changes our domain. We are no longer part of the thread
	   group */

	current->self_exec_id++;
			
	flush_signal_handlers(current, 0);
	flush_old_files(current->files);

	return 0;

mmap_failed:
	put_files_struct(current->files);
	current->files = files;
out:
	return retval;
}

EXPORT_SYMBOL(flush_old_exec);

/*
 * We mustn't allow tracing of suid binaries, unless
 * the tracer has the capability to trace anything..
 */
static inline int must_not_trace_exec(struct task_struct * p)
{
	return (p->ptrace & PT_PTRACED) && !(p->ptrace & PT_PTRACE_CAP);
}

/* 
 * Fill the binprm structure from the inode. 
 * Check permissions, then read the first 128 (BINPRM_BUF_SIZE) bytes
 */
int prepare_binprm(struct linux_binprm *bprm)
{
	int mode;
	struct inode * inode = bprm->file->f_dentry->d_inode;
	int retval;

	mode = inode->i_mode;
	/*
	 * Check execute perms again - if the caller has CAP_DAC_OVERRIDE,
	 * vfs_permission lets a non-executable through
	 */
	if (!(mode & 0111))	/* with at least _one_ execute bit set */
		return -EACCES;
	if (bprm->file->f_op == NULL)
		return -EACCES;

	bprm->e_uid = current->euid;
	bprm->e_gid = current->egid;

	if(!(bprm->file->f_vfsmnt->mnt_flags & MNT_NOSUID)) {
		/* Set-uid? */
		if (mode & S_ISUID)
			bprm->e_uid = inode->i_uid;

		/* Set-gid? */
		/*
		 * If setgid is set but no group execute bit then this
		 * is a candidate for mandatory locking, not a setgid
		 * executable.
		 */
		if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP))
			bprm->e_gid = inode->i_gid;
	}

	/* fill in binprm security blob */
	retval = security_bprm_set(bprm);
	if (retval)
		return retval;

	memset(bprm->buf,0,BINPRM_BUF_SIZE);
	return kernel_read(bprm->file,0,bprm->buf,BINPRM_BUF_SIZE);
}

EXPORT_SYMBOL(prepare_binprm);

/*
 * This function is used to produce the new IDs and capabilities
 * from the old ones and the file's capabilities.
 *
 * The formula used for evolving capabilities is:
 *
 *       pI' = pI
 * (***) pP' = (fP & X) | (fI & pI)
 *       pE' = pP' & fE          [NB. fE is 0 or ~0]
 *
 * I=Inheritable, P=Permitted, E=Effective // p=process, f=file
 * ' indicates post-exec(), and X is the global 'cap_bset'.
 *
 */

void compute_creds(struct linux_binprm *bprm)
{
	task_lock(current);
	if (bprm->e_uid != current->uid || bprm->e_gid != current->gid) {
                current->mm->dumpable = 0;
		
		if (must_not_trace_exec(current)
		    || atomic_read(&current->fs->count) > 1
		    || atomic_read(&current->files->count) > 1
		    || atomic_read(&current->sighand->count) > 1) {
			if(!capable(CAP_SETUID)) {
				bprm->e_uid = current->uid;
				bprm->e_gid = current->gid;
			}
		}
	}

        current->suid = current->euid = current->fsuid = bprm->e_uid;
        current->sgid = current->egid = current->fsgid = bprm->e_gid;

	task_unlock(current);

	security_bprm_compute_creds(bprm);
}

EXPORT_SYMBOL(compute_creds);

void remove_arg_zero(struct linux_binprm *bprm)
{
	if (bprm->argc) {
		unsigned long offset;
		char * kaddr;
		struct page *page;

		offset = bprm->p % PAGE_SIZE;
		goto inside;

		while (bprm->p++, *(kaddr+offset++)) {
			if (offset != PAGE_SIZE)
				continue;
			offset = 0;
			kunmap_atomic(kaddr, KM_USER0);
inside:
			page = bprm->page[bprm->p/PAGE_SIZE];
			kaddr = kmap_atomic(page, KM_USER0);
		}
		kunmap_atomic(kaddr, KM_USER0);
		bprm->argc--;
	}
}

EXPORT_SYMBOL(remove_arg_zero);

/*
 * cycle the list of binary formats handler, until one recognizes the image
 */
int search_binary_handler(struct linux_binprm *bprm,struct pt_regs *regs)
{
	int try,retval=0;
	struct linux_binfmt *fmt;
#ifdef __alpha__
	/* handle /sbin/loader.. */
	{
	    struct exec * eh = (struct exec *) bprm->buf;

	    if (!bprm->loader && eh->fh.f_magic == 0x183 &&
		(eh->fh.f_flags & 0x3000) == 0x3000)
	    {
		struct file * file;
		unsigned long loader;

		allow_write_access(bprm->file);
		fput(bprm->file);
		bprm->file = NULL;

	        loader = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);

		file = open_exec("/sbin/loader");
		retval = PTR_ERR(file);
		if (IS_ERR(file))
			return retval;

		/* Remember if the application is TASO.  */
		bprm->sh_bang = eh->ah.entry < 0x100000000;

		bprm->file = file;
		bprm->loader = loader;
		retval = prepare_binprm(bprm);
		if (retval<0)
			return retval;
		/* should call search_binary_handler recursively here,
		   but it does not matter */
	    }
	}
#endif
	retval = security_bprm_check(bprm);
	if (retval)
		return retval;

	/* kernel module loader fixup */
	/* so we don't try to load run modprobe in kernel space. */
	set_fs(USER_DS);
	for (try=0; try<2; try++) {
		read_lock(&binfmt_lock);
		for (fmt = formats ; fmt ; fmt = fmt->next) {
			int (*fn)(struct linux_binprm *, struct pt_regs *) = fmt->load_binary;
			if (!fn)
				continue;
			if (!try_module_get(fmt->module))
				continue;
			read_unlock(&binfmt_lock);
			retval = fn(bprm, regs);
			if (retval >= 0) {
				put_binfmt(fmt);
				allow_write_access(bprm->file);
				if (bprm->file)
					fput(bprm->file);
				bprm->file = NULL;
				current->did_exec = 1;
				return retval;
			}
			read_lock(&binfmt_lock);
			put_binfmt(fmt);
			if (retval != -ENOEXEC || bprm->mm == NULL)
				break;
			if (!bprm->file) {
				read_unlock(&binfmt_lock);
				return retval;
			}
		}
		read_unlock(&binfmt_lock);
		if (retval != -ENOEXEC || bprm->mm == NULL) {
			break;
#ifdef CONFIG_KMOD
		}else{
#define printable(c) (((c)=='\t') || ((c)=='\n') || (0x20<=(c) && (c)<=0x7e))
			if (printable(bprm->buf[0]) &&
			    printable(bprm->buf[1]) &&
			    printable(bprm->buf[2]) &&
			    printable(bprm->buf[3]))
				break; /* -ENOEXEC */
			request_module("binfmt-%04x", *(unsigned short *)(&bprm->buf[2]));
#endif
		}
	}
	return retval;
}

EXPORT_SYMBOL(search_binary_handler);

/*
 * sys_execve() executes a new program.
 */
int do_execve(char * filename,
	char __user *__user *argv,
	char __user *__user *envp,
	struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct file *file;
	int retval;
	int i;

	sched_balance_exec();

	file = open_exec(filename);

	retval = PTR_ERR(file);
	if (IS_ERR(file))
		return retval;

	bprm.p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	memset(bprm.page, 0, MAX_ARG_PAGES*sizeof(bprm.page[0]));

	bprm.file = file;
	bprm.filename = filename;
	bprm.interp = filename;
	bprm.sh_bang = 0;
	bprm.loader = 0;
	bprm.exec = 0;
	bprm.security = NULL;
	bprm.mm = mm_alloc();
	retval = -ENOMEM;
	if (!bprm.mm)
		goto out_file;

	retval = init_new_context(current, bprm.mm);
	if (retval < 0)
		goto out_mm;

	bprm.argc = count(argv, bprm.p / sizeof(void *));
	if ((retval = bprm.argc) < 0)
		goto out_mm;

	bprm.envc = count(envp, bprm.p / sizeof(void *));
	if ((retval = bprm.envc) < 0)
		goto out_mm;

	retval = security_bprm_alloc(&bprm);
	if (retval)
		goto out;

	retval = prepare_binprm(&bprm);
	if (retval < 0)
		goto out;

	retval = copy_strings_kernel(1, &bprm.filename, &bprm);
	if (retval < 0)
		goto out;

	bprm.exec = bprm.p;
	retval = copy_strings(bprm.envc, envp, &bprm);
	if (retval < 0)
		goto out;

	retval = copy_strings(bprm.argc, argv, &bprm);
	if (retval < 0)
		goto out;

	retval = search_binary_handler(&bprm,regs);
	if (retval >= 0) {
		free_arg_pages(&bprm);

		/* execve success */
		security_bprm_free(&bprm);
		return retval;
	}

out:
	/* Something went wrong, return the inode and free the argument pages*/
	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		struct page * page = bprm.page[i];
		if (page)
			__free_page(page);
	}

	if (bprm.security)
		security_bprm_free(&bprm);

out_mm:
	if (bprm.mm)
		mmdrop(bprm.mm);

out_file:
	if (bprm.file) {
		allow_write_access(bprm.file);
		fput(bprm.file);
	}
	return retval;
}

EXPORT_SYMBOL(do_execve);

int set_binfmt(struct linux_binfmt *new)
{
	struct linux_binfmt *old = current->binfmt;

	if (new) {
		if (!try_module_get(new->module))
			return -1;
	}
	current->binfmt = new;
	if (old)
		module_put(old->module);
	return 0;
}

EXPORT_SYMBOL(set_binfmt);

#define CORENAME_MAX_SIZE 64

/* format_corename will inspect the pattern parameter, and output a
 * name into corename, which must have space for at least
 * CORENAME_MAX_SIZE bytes plus one byte for the zero terminator.
 */
void format_corename(char *corename, const char *pattern, long signr)
{
	const char *pat_ptr = pattern;
	char *out_ptr = corename;
	char *const out_end = corename + CORENAME_MAX_SIZE;
	int rc;
	int pid_in_pattern = 0;

	/* Repeat as long as we have more pattern to process and more output
	   space */
	while (*pat_ptr) {
		if (*pat_ptr != '%') {
			if (out_ptr == out_end)
				goto out;
			*out_ptr++ = *pat_ptr++;
		} else {
			switch (*++pat_ptr) {
			case 0:
				goto out;
			/* Double percent, output one percent */
			case '%':
				if (out_ptr == out_end)
					goto out;
				*out_ptr++ = '%';
				break;
			/* pid */
			case 'p':
				pid_in_pattern = 1;
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%d", current->tgid);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* uid */
			case 'u':
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%d", current->uid);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* gid */
			case 'g':
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%d", current->gid);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* signal that caused the coredump */
			case 's':
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%ld", signr);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* UNIX time of coredump */
			case 't': {
				struct timeval tv;
				do_gettimeofday(&tv);
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%lu", tv.tv_sec);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			}
			/* hostname */
			case 'h':
				down_read(&uts_sem);
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%s", system_utsname.nodename);
				up_read(&uts_sem);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* executable */
			case 'e':
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%s", current->comm);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			default:
				break;
			}
			++pat_ptr;
		}
	}
	/* Backward compatibility with core_uses_pid:
	 *
	 * If core_pattern does not include a %p (as is the default)
	 * and core_uses_pid is set, then .%pid will be appended to
	 * the filename */
	if (!pid_in_pattern
            && (core_uses_pid || atomic_read(&current->mm->mm_users) != 1)) {
		rc = snprintf(out_ptr, out_end - out_ptr,
			      ".%d", current->tgid);
		if (rc > out_end - out_ptr)
			goto out;
		out_ptr += rc;
	}
      out:
	*out_ptr = 0;
}

static void zap_threads (struct mm_struct *mm)
{
	struct task_struct *g, *p;
	struct task_struct *tsk = current;
	struct completion *vfork_done = tsk->vfork_done;

	/*
	 * Make sure nobody is waiting for us to release the VM,
	 * otherwise we can deadlock when we wait on each other
	 */
	if (vfork_done) {
		tsk->vfork_done = NULL;
		complete(vfork_done);
	}

	read_lock(&tasklist_lock);
	do_each_thread(g,p)
		if (mm == p->mm && p != tsk) {
			force_sig_specific(SIGKILL, p);
			mm->core_waiters++;
		}
	while_each_thread(g,p);

	read_unlock(&tasklist_lock);
}

static void coredump_wait(struct mm_struct *mm)
{
	DECLARE_COMPLETION(startup_done);

	mm->core_waiters++; /* let other threads block */
	mm->core_startup_done = &startup_done;

	/* give other threads a chance to run: */
	yield();

	zap_threads(mm);
	if (--mm->core_waiters) {
		up_write(&mm->mmap_sem);
		wait_for_completion(&startup_done);
	} else
		up_write(&mm->mmap_sem);
	BUG_ON(mm->core_waiters);
}

int do_coredump(long signr, int exit_code, struct pt_regs * regs)
{
	char corename[CORENAME_MAX_SIZE + 1];
	struct mm_struct *mm = current->mm;
	struct linux_binfmt * binfmt;
	struct inode * inode;
	struct file * file;
	int retval = 0;

	lock_kernel();
	binfmt = current->binfmt;
	if (!binfmt || !binfmt->core_dump)
		goto fail;
	down_write(&mm->mmap_sem);
	if (!mm->dumpable) {
		up_write(&mm->mmap_sem);
		goto fail;
	}
	mm->dumpable = 0;
	init_completion(&mm->core_done);
	current->signal->group_exit = 1;
	current->signal->group_exit_code = exit_code;
	coredump_wait(mm);

	if (current->rlim[RLIMIT_CORE].rlim_cur < binfmt->min_coredump)
		goto fail_unlock;

 	format_corename(corename, core_pattern, signr);
	file = filp_open(corename, O_CREAT | 2 | O_NOFOLLOW | O_LARGEFILE, 0600);
	if (IS_ERR(file))
		goto fail_unlock;
	inode = file->f_dentry->d_inode;
	if (inode->i_nlink > 1)
		goto close_fail;	/* multiple links - don't dump */
	if (d_unhashed(file->f_dentry))
		goto close_fail;

	if (!S_ISREG(inode->i_mode))
		goto close_fail;
	if (!file->f_op)
		goto close_fail;
	if (!file->f_op->write)
		goto close_fail;
	if (do_truncate(file->f_dentry, 0) != 0)
		goto close_fail;

	retval = binfmt->core_dump(signr, regs, file);

	current->signal->group_exit_code |= 0x80;
close_fail:
	filp_close(file, NULL);
fail_unlock:
	complete_all(&mm->core_done);
fail:
	unlock_kernel();
	return retval;
}
