/*
 *  linux/fs/proc/array.c
 *
 *  Copyright (C) 1992  by Linus Torvalds
 *  based on ideas by Darren Senn
 *
 * Fixes:
 * Michael. K. Johnson: stat,statm extensions.
 *                      <johnsonm@stolaf.edu>
 *
 * Pauline Middelink :  Made cmdline,envline only break at '\0's, to
 *                      make sure SET_PROCTITLE works. Also removed
 *                      bad '!' which forced address recalculation for
 *                      EVERY character on the current page.
 *                      <middelin@polyware.iaf.nl>
 *
 * Danny ter Haar    :	added cpuinfo
 *			<dth@cistron.nl>
 *
 * Alessandro Rubini :  profile extension.
 *                      <rubini@ipvvis.unipv.it>
 *
 * Jeff Tranter      :  added BogoMips field to cpuinfo
 *                      <Jeff_Tranter@Mitel.COM>
 *
 * Bruno Haible      :  remove 4K limit for the maps file
 *			<haible@ma2s2.mathematik.uni-karlsruhe.de>
 *
 * Yves Arrouye      :  remove removal of trailing spaces in get_array.
 *			<Yves.Arrouye@marin.fdn.fr>
 *
 * Jerome Forissier  :  added per-CPU time information to /proc/stat
 *                      and /proc/<pid>/cpu extension
 *                      <forissier@isia.cma.fr>
 *			- Incorporation and non-SMP safe operation
 *			of forissier patch in 2.1.78 by
 *			Hans Marcus <crowbar@concepts.nl>
 *
 * aeb@cwi.nl        :  /proc/partitions
 *
 *
 * Alan Cox	     :  security fixes.
 *			<Alan.Cox@linux.org>
 *
 * Al Viro           :  safe handling of mm_struct
 *
 * Gerhard Wichert   :  added BIGMEM support
 * Siemens AG           <Gerhard.Wichert@pdb.siemens.de>
 *
 * Al Viro & Jeff Garzik :  moved most of the thing into base.c and
 *			 :  proc_misc.c. The rest may eventually go into
 *			 :  base.c too.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/mman.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/highmem.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/processor.h>

/* Gcc optimizes away "strlen(x)" for constant x */
#define ADDBUF(buffer, string) \
do { memcpy(buffer, string, strlen(string)); \
     buffer += strlen(string); } while (0)

static inline char * task_name(struct task_struct *p, char * buf)
{
	int i;
	char * name;

	ADDBUF(buf, "Name:\t");
	name = p->comm;
	i = sizeof(p->comm);
	do {
		unsigned char c = *name;
		name++;
		i--;
		*buf = c;
		if (!c)
			break;
		if (c == '\\') {
			buf[1] = c;
			buf += 2;
			continue;
		}
		if (c == '\n') {
			buf[0] = '\\';
			buf[1] = 'n';
			buf += 2;
			continue;
		}
		buf++;
	} while (i);
	*buf = '\n';
	return buf+1;
}

/*
 * The task state array is a strange "bitmap" of
 * reasons to sleep. Thus "running" is zero, and
 * you can test for combinations of others with
 * simple bit tests.
 */
static const char *task_state_array[] = {
	"R (running)",		/*  0 */
	"S (sleeping)",		/*  1 */
	"D (disk sleep)",	/*  2 */
	"Z (zombie)",		/*  4 */
	"T (stopped)",		/*  8 */
	"W (paging)"		/* 16 */
};

static inline const char * get_task_state(struct task_struct *tsk)
{
	unsigned int state = tsk->state & (TASK_RUNNING |
					   TASK_INTERRUPTIBLE |
					   TASK_UNINTERRUPTIBLE |
					   TASK_ZOMBIE |
					   TASK_STOPPED);
	const char **p = &task_state_array[0];

	while (state) {
		p++;
		state >>= 1;
	}
	return *p;
}

static inline char * task_state(struct task_struct *p, char *buffer)
{
	int g;

	read_lock(&tasklist_lock);
	buffer += sprintf(buffer,
		"State:\t%s\n"
		"Pid:\t%d\n"
		"PPid:\t%d\n"
		"TracerPid:\t%d\n"
		"Uid:\t%d\t%d\t%d\t%d\n"
		"Gid:\t%d\t%d\t%d\t%d\n",
		get_task_state(p),
		p->pid, p->p_opptr->pid, p->p_pptr->pid != p->p_opptr->pid ? p->p_pptr->pid : 0,
		p->uid, p->euid, p->suid, p->fsuid,
		p->gid, p->egid, p->sgid, p->fsgid);
	read_unlock(&tasklist_lock);	
	task_lock(p);
	buffer += sprintf(buffer,
		"FDSize:\t%d\n"
		"Groups:\t",
		p->files ? p->files->max_fds : 0);
	task_unlock(p);

	for (g = 0; g < p->ngroups; g++)
		buffer += sprintf(buffer, "%d ", p->groups[g]);

	buffer += sprintf(buffer, "\n");
	return buffer;
}

static inline char * task_mem(struct mm_struct *mm, char *buffer)
{
	struct vm_area_struct * vma;
	unsigned long data = 0, stack = 0;
	unsigned long exec = 0, lib = 0;

	down(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		unsigned long len = (vma->vm_end - vma->vm_start) >> 10;
		if (!vma->vm_file) {
			data += len;
			if (vma->vm_flags & VM_GROWSDOWN)
				stack += len;
			continue;
		}
		if (vma->vm_flags & VM_WRITE)
			continue;
		if (vma->vm_flags & VM_EXEC) {
			exec += len;
			if (vma->vm_flags & VM_EXECUTABLE)
				continue;
			lib += len;
		}
	}
	buffer += sprintf(buffer,
		"VmSize:\t%8lu kB\n"
		"VmLck:\t%8lu kB\n"
		"VmRSS:\t%8lu kB\n"
		"VmData:\t%8lu kB\n"
		"VmStk:\t%8lu kB\n"
		"VmExe:\t%8lu kB\n"
		"VmLib:\t%8lu kB\n",
		mm->total_vm << (PAGE_SHIFT-10),
		mm->locked_vm << (PAGE_SHIFT-10),
		mm->rss << (PAGE_SHIFT-10),
		data - stack, stack,
		exec - lib, lib);
	up(&mm->mmap_sem);
	return buffer;
}

static void collect_sigign_sigcatch(struct task_struct *p, sigset_t *ign,
				    sigset_t *catch)
{
	struct k_sigaction *k;
	int i;

	sigemptyset(ign);
	sigemptyset(catch);

	if (p->sig) {
		k = p->sig->action;
		for (i = 1; i <= _NSIG; ++i, ++k) {
			if (k->sa.sa_handler == SIG_IGN)
				sigaddset(ign, i);
			else if (k->sa.sa_handler != SIG_DFL)
				sigaddset(catch, i);
		}
	}
}

static inline char * task_sig(struct task_struct *p, char *buffer)
{
	sigset_t ign, catch;

	buffer += sprintf(buffer, "SigPnd:\t");
	buffer = render_sigset_t(&p->pending.signal, buffer);
	*buffer++ = '\n';
	buffer += sprintf(buffer, "SigBlk:\t");
	buffer = render_sigset_t(&p->blocked, buffer);
	*buffer++ = '\n';

	collect_sigign_sigcatch(p, &ign, &catch);
	buffer += sprintf(buffer, "SigIgn:\t");
	buffer = render_sigset_t(&ign, buffer);
	*buffer++ = '\n';
	buffer += sprintf(buffer, "SigCgt:\t"); /* Linux 2.0 uses "SigCgt" */
	buffer = render_sigset_t(&catch, buffer);
	*buffer++ = '\n';

	return buffer;
}

extern inline char *task_cap(struct task_struct *p, char *buffer)
{
    return buffer + sprintf(buffer, "CapInh:\t%016x\n"
			    "CapPrm:\t%016x\n"
			    "CapEff:\t%016x\n",
			    cap_t(p->cap_inheritable),
			    cap_t(p->cap_permitted),
			    cap_t(p->cap_effective));
}


int proc_pid_status(struct task_struct *task, char * buffer)
{
	char * orig = buffer;
	struct mm_struct *mm;
#if defined(CONFIG_ARCH_S390)
	int line,len;
#endif

	buffer = task_name(task, buffer);
	buffer = task_state(task, buffer);
	task_lock(task);
	mm = task->mm;
	if(mm)
		atomic_inc(&mm->mm_users);
	task_unlock(task);
	if (mm) {
		buffer = task_mem(mm, buffer);
		mmput(mm);
	}
	buffer = task_sig(task, buffer);
	buffer = task_cap(task, buffer);
#if defined(CONFIG_ARCH_S390)
	for(line=0;(len=sprintf_regs(line,buffer,task,NULL,NULL))!=0;line++)
		buffer+=len;
#endif
	return buffer - orig;
}

int proc_pid_stat(struct task_struct *task, char * buffer)
{
	unsigned long vsize, eip, esp, wchan;
	long priority, nice;
	int tty_pgrp = -1, tty_nr = 0;
	sigset_t sigign, sigcatch;
	char state;
	int res;
	pid_t ppid;
	struct mm_struct *mm;

	state = *get_task_state(task);
	vsize = eip = esp = 0;
	task_lock(task);
	mm = task->mm;
	if(mm)
		atomic_inc(&mm->mm_users);
	if (task->tty) {
		tty_pgrp = task->tty->pgrp;
		tty_nr = kdev_t_to_nr(task->tty->device);
	}
	task_unlock(task);
	if (mm) {
		struct vm_area_struct *vma;
		down(&mm->mmap_sem);
		vma = mm->mmap;
		while (vma) {
			vsize += vma->vm_end - vma->vm_start;
			vma = vma->vm_next;
		}
		eip = KSTK_EIP(task);
		esp = KSTK_ESP(task);
		up(&mm->mmap_sem);
	}

	wchan = get_wchan(task);

	collect_sigign_sigcatch(task, &sigign, &sigcatch);

	/* scale priority and nice values from timeslices to -20..20 */
	/* to make it look like a "normal" Unix priority/nice value  */
	priority = task->counter;
	priority = 20 - (priority * 10 + DEF_COUNTER / 2) / DEF_COUNTER;
	nice = task->nice;

	read_lock(&tasklist_lock);
	ppid = task->p_opptr->pid;
	read_unlock(&tasklist_lock);
	res = sprintf(buffer,"%d (%s) %c %d %d %d %d %d %lu %lu \
%lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %lu %lu %ld %lu %lu %lu %lu %lu \
%lu %lu %lu %lu %lu %lu %lu %lu %d %d\n",
		task->pid,
		task->comm,
		state,
		ppid,
		task->pgrp,
		task->session,
	        tty_nr,
		tty_pgrp,
		task->flags,
		task->min_flt,
		task->cmin_flt,
		task->maj_flt,
		task->cmaj_flt,
		task->times.tms_utime,
		task->times.tms_stime,
		task->times.tms_cutime,
		task->times.tms_cstime,
		priority,
		nice,
		0UL /* removed */,
		task->it_real_value,
		task->start_time,
		vsize,
		mm ? mm->rss : 0, /* you might want to shift this left 3 */
		task->rlim[RLIMIT_RSS].rlim_cur,
		mm ? mm->start_code : 0,
		mm ? mm->end_code : 0,
		mm ? mm->start_stack : 0,
		esp,
		eip,
		/* The signal information here is obsolete.
		 * It must be decimal for Linux 2.0 compatibility.
		 * Use /proc/#/status for real-time signals.
		 */
		task->pending.signal.sig[0] & 0x7fffffffUL,
		task->blocked.sig[0] & 0x7fffffffUL,
		sigign      .sig[0] & 0x7fffffffUL,
		sigcatch    .sig[0] & 0x7fffffffUL,
		wchan,
		task->nswap,
		task->cnswap,
		task->exit_signal,
		task->processor);
	if(mm)
		mmput(mm);
	return res;
}
		
static inline void statm_pte_range(pmd_t * pmd, unsigned long address, unsigned long size,
	int * pages, int * shared, int * dirty, int * total)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t page = *pte;
		struct page *ptpage;

		address += PAGE_SIZE;
		pte++;
		if (pte_none(page))
			continue;
		++*total;
		if (!pte_present(page))
			continue;
		++*pages;
		if (pte_dirty(page))
			++*dirty;
		ptpage = pte_page(page);
		if ((!VALID_PAGE(ptpage)) || 
					PageReserved(ptpage))
			continue;
		if (page_count(pte_page(page)) > 1)
			++*shared;
	} while (address < end);
}

static inline void statm_pmd_range(pgd_t * pgd, unsigned long address, unsigned long size,
	int * pages, int * shared, int * dirty, int * total)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd)) {
		pgd_ERROR(*pgd);
		pgd_clear(pgd);
		return;
	}
	pmd = pmd_offset(pgd, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		statm_pte_range(pmd, address, end - address, pages, shared, dirty, total);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
}

static void statm_pgd_range(pgd_t * pgd, unsigned long address, unsigned long end,
	int * pages, int * shared, int * dirty, int * total)
{
	while (address < end) {
		statm_pmd_range(pgd, address, end - address, pages, shared, dirty, total);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		pgd++;
	}
}

int proc_pid_statm(struct task_struct *task, char * buffer)
{
	struct mm_struct *mm;
	int size=0, resident=0, share=0, trs=0, lrs=0, drs=0, dt=0;

	task_lock(task);
	mm = task->mm;
	if(mm)
		atomic_inc(&mm->mm_users);
	task_unlock(task);
	if (mm) {
		struct vm_area_struct * vma;
		down(&mm->mmap_sem);
		vma = mm->mmap;
		while (vma) {
			pgd_t *pgd = pgd_offset(mm, vma->vm_start);
			int pages = 0, shared = 0, dirty = 0, total = 0;

			statm_pgd_range(pgd, vma->vm_start, vma->vm_end, &pages, &shared, &dirty, &total);
			resident += pages;
			share += shared;
			dt += dirty;
			size += total;
			if (vma->vm_flags & VM_EXECUTABLE)
				trs += pages;	/* text */
			else if (vma->vm_flags & VM_GROWSDOWN)
				drs += pages;	/* stack */
			else if (vma->vm_end > 0x60000000)
				lrs += pages;	/* library */
			else
				drs += pages;
			vma = vma->vm_next;
		}
		up(&mm->mmap_sem);
		mmput(mm);
	}
	return sprintf(buffer,"%d %d %d %d %d %d %d\n",
		       size, resident, share, trs, lrs, drs, dt);
}

/*
 * The way we support synthetic files > 4K
 * - without storing their contents in some buffer and
 * - without walking through the entire synthetic file until we reach the
 *   position of the requested data
 * is to cleverly encode the current position in the file's f_pos field.
 * There is no requirement that a read() call which returns `count' bytes
 * of data increases f_pos by exactly `count'.
 *
 * This idea is Linus' one. Bruno implemented it.
 */

/*
 * For the /proc/<pid>/maps file, we use fixed length records, each containing
 * a single line.
 */
#define MAPS_LINE_LENGTH	4096
#define MAPS_LINE_SHIFT		12
/*
 * f_pos = (number of the vma in the task->mm->mmap list) * MAPS_LINE_LENGTH
 *         + (index into the line)
 */
/* for systems with sizeof(void*) == 4: */
#define MAPS_LINE_FORMAT4	  "%08lx-%08lx %s %08lx %s %lu"
#define MAPS_LINE_MAX4	49 /* sum of 8  1  8  1 4 1 8 1 5 1 10 1 */

/* for systems with sizeof(void*) == 8: */
#define MAPS_LINE_FORMAT8	  "%016lx-%016lx %s %016lx %s %lu"
#define MAPS_LINE_MAX8	73 /* sum of 16  1  16  1 4 1 16 1 5 1 10 1 */

#define MAPS_LINE_MAX	MAPS_LINE_MAX8


ssize_t proc_pid_read_maps (struct task_struct *task, struct file * file, char * buf,
			  size_t count, loff_t *ppos)
{
	struct mm_struct *mm;
	struct vm_area_struct * map, * next;
	char * destptr = buf, * buffer;
	loff_t lineno;
	ssize_t column, i;
	int volatile_task;
	long retval;

	/*
	 * We might sleep getting the page, so get it first.
	 */
	retval = -ENOMEM;
	buffer = (char*)__get_free_page(GFP_KERNEL);
	if (!buffer)
		goto out;

	if (count == 0)
		goto getlen_out;
	task_lock(task);
	mm = task->mm;
	if (mm)
		atomic_inc(&mm->mm_users);
	task_unlock(task);
	if (!mm)
		goto getlen_out;

	/* Check whether the mmaps could change if we sleep */
	volatile_task = (task != current || atomic_read(&mm->mm_users) > 2);

	/* decode f_pos */
	lineno = *ppos >> MAPS_LINE_SHIFT;
	column = *ppos & (MAPS_LINE_LENGTH-1);

	/* quickly go to line lineno */
	down(&mm->mmap_sem);
	for (map = mm->mmap, i = 0; map && (i < lineno); map = map->vm_next, i++)
		continue;

	for ( ; map ; map = next ) {
		/* produce the next line */
		char *line;
		char str[5], *cp = str;
		int flags;
		kdev_t dev;
		unsigned long ino;
		int maxlen = (sizeof(void*) == 4) ?
			MAPS_LINE_MAX4 :  MAPS_LINE_MAX8;
		int len;

		/*
		 * Get the next vma now (but it won't be used if we sleep).
		 */
		next = map->vm_next;
		flags = map->vm_flags;

		*cp++ = flags & VM_READ ? 'r' : '-';
		*cp++ = flags & VM_WRITE ? 'w' : '-';
		*cp++ = flags & VM_EXEC ? 'x' : '-';
		*cp++ = flags & VM_MAYSHARE ? 's' : 'p';
		*cp++ = 0;

		dev = 0;
		ino = 0;
		if (map->vm_file != NULL) {
			dev = map->vm_file->f_dentry->d_inode->i_dev;
			ino = map->vm_file->f_dentry->d_inode->i_ino;
			line = d_path(map->vm_file->f_dentry,
				      map->vm_file->f_vfsmnt,
				      buffer, PAGE_SIZE);
			buffer[PAGE_SIZE-1] = '\n';
			line -= maxlen;
			if(line < buffer)
				line = buffer;
		} else
			line = buffer;

		len = sprintf(line,
			      sizeof(void*) == 4 ? MAPS_LINE_FORMAT4 : MAPS_LINE_FORMAT8,
			      map->vm_start, map->vm_end, str, map->vm_pgoff << PAGE_SHIFT,
			      kdevname(dev), ino);

		if(map->vm_file) {
			for(i = len; i < maxlen; i++)
				line[i] = ' ';
			len = buffer + PAGE_SIZE - line;
		} else
			line[len++] = '\n';
		if (column >= len) {
			column = 0; /* continue with next line at column 0 */
			lineno++;
			continue; /* we haven't slept */
		}

		i = len-column;
		if (i > count)
			i = count;
		up(&mm->mmap_sem);
		copy_to_user(destptr, line+column, i); /* may have slept */
		down(&mm->mmap_sem);
		destptr += i;
		count   -= i;
		column  += i;
		if (column >= len) {
			column = 0; /* next time: next line at column 0 */
			lineno++;
		}

		/* done? */
		if (count == 0)
			break;

		/* By writing to user space, we might have slept.
		 * Stop the loop, to avoid a race condition.
		 */
		if (volatile_task)
			break;
	}
	up(&mm->mmap_sem);

	/* encode f_pos */
	*ppos = (lineno << MAPS_LINE_SHIFT) + column;
	mmput(mm);

getlen_out:
	retval = destptr - buf;
	free_page((unsigned long)buffer);
out:
	return retval;
}

#ifdef CONFIG_SMP
int proc_pid_cpu(struct task_struct *task, char * buffer)
{
	int i, len;

	len = sprintf(buffer,
		"cpu  %lu %lu\n",
		task->times.tms_utime,
		task->times.tms_stime);
		
	for (i = 0 ; i < smp_num_cpus; i++)
		len += sprintf(buffer + len, "cpu%d %lu %lu\n",
			i,
			task->per_cpu_utime[cpu_logical_map(i)],
			task->per_cpu_stime[cpu_logical_map(i)]);

	return len;
}
#endif
