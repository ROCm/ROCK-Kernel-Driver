/*
 * IBM Dynamic Probes
 * Copyright (c) International Business Machines Corp., 2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

#include <linux/dprobes.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/notifier.h>
#ifdef CONFIG_MAGIC_SYSRQ
#include <linux/sysrq.h>
#endif

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/errno.h>
#ifdef CONFIG_DEBUGREG
#include <asm/debugreg.h>
#endif

#ifdef CONFIG_SMP
struct dprobes_struct dprobes_set[NR_CPUS];
#else
struct dprobes_struct dprobes;
#endif

struct dp_module_struct *dp_module_list = NULL;
DECLARE_RWSEM(dp_modlist_sem);

byte_t *dp_heap = NULL;
unsigned long dp_num_heap = 0;
rwlock_t dp_heap_lock = RW_LOCK_UNLOCKED;

unsigned long *dp_gv = NULL;
unsigned long dp_num_gv = 0;
rwlock_t dp_gv_lock = RW_LOCK_UNLOCKED;


#ifdef CONFIG_MAGIC_SYSRQ
static struct work_struct dprobes_work;
static struct workqueue_struct *dprobes_wq;
static void dprobes_sysrq( int , struct pt_regs *, struct tty_struct *);
static struct sysrq_key_op key_op = {
	.handler = &dprobes_sysrq,
	.help_msg = "remoVe_dynamic_probes",
	.action_msg = "Deactivating all probepoints",
};
unsigned long emergency_remove = 0; /*Flag to disable active dprobes*/
#endif


void dprobes_code_start(void) { }

static void init_trace_hdr(struct dp_module_struct *m)
{
	unsigned long log_flags = m->pgm.flags & DP_LOG_MASK;
	unsigned long mask = 0;
	unsigned short len = sizeof(m->hdr);

	mask |= DP_HDR_MAJOR | DP_HDR_MINOR;
#ifdef CONFIG_SMP
	mask |= DP_HDR_CPU;
#ifdef CONFIG_IA64
	len += sizeof(long);
#else
	len += sizeof(int);
#endif
#endif	
	if (log_flags & DP_LOG_PID) {
		mask |= DP_HDR_PID;
		len += sizeof(pid_t);
	}
	if (log_flags & DP_LOG_UID) {
		mask |= DP_HDR_UID;
		len += sizeof(uid_t);
	}
	if (log_flags & DP_LOG_CS_EIP) {
		mask |= DP_HDR_CS | DP_HDR_EIP;
		len += (sizeof(unsigned short) + sizeof(unsigned long));
	}
	if (log_flags & DP_LOG_SS_ESP) {
		mask |= DP_HDR_SS | DP_HDR_ESP;
		len += (sizeof(unsigned short) + sizeof(unsigned long));
	}
	if (log_flags & DP_LOG_TSC) {
		mask |= DP_HDR_TSC;
		len += sizeof(struct timeval);
	}
	if (log_flags & DP_LOG_PROCNAME) {
		mask |= DP_HDR_PROCNAME;
		len += 16; /* size of task_struct.comm */
	}
	m->hdr.facility_id = DP_TRACE_HDR_ID;
	m->hdr.mask = mask;
	m->hdr.len = len;
	/* Adjust the logmax to take care of log header bytes */
	m->pgm.logmax += len;
	return;
}

#if defined(CONFIG_TRACE) || defined(CONFIG_TRACE_MODULE)
#include <linux/trace.h>
static inline void get_trace_id(struct dp_module_struct *m)
{
	m->trace_id = trace_create_event(m->pgm.name, NULL, CUSTOM_EVENT_FORMAT_TYPE_HEX, NULL);
}	
static inline void free_trace_id(struct dp_module_struct *m)
{
	trace_destroy_event(m->trace_id);
}
#else
#define get_trace_id(m)
#define free_trace_id(m)
#endif

static inline struct dp_module_struct * find_mod_by_inode(struct inode *inode)
{
	struct dp_module_struct *m;
	for (m = dp_module_list; m; m = m->next) {
		if (m->inode == inode)
			break;
	}
	return m;
}

static inline struct dp_module_struct * find_mod_by_name(const char * name)
{
	struct dp_module_struct *m;
	for (m = dp_module_list; m ; m = m->next) {
		if (!strcmp(m->pgm.name, name))
			break;
	}
	return m;
}

static inline void link_module(struct dp_module_struct *m)
{
	m->next = dp_module_list;
	dp_module_list = m;
	return;
}

static inline void unlink_module(struct dp_module_struct *m)
{
	struct dp_module_struct *prev;

	if (m == dp_module_list) {
		dp_module_list = m->next;
		return;
	}
	prev = dp_module_list;
	while (prev->next && prev->next != m)
		prev = prev->next;
	if (prev->next)
		prev->next = prev->next->next;
	return;
}

/*
 * copied from kernel/module.c find_module().
 */
static struct module * find_kmodule(const char *name)
{
	struct module *kmod = NULL;
#if 0
	extern struct list_head *modules;
	list_for_each_entry(kmod, modules, list) {
		if (!strcmp(kmod->name, name))
			break;
	}
#endif
	return kmod;
}

/*
 * Returns the index of the last dp_record that lies in the same page as the
 * dp_record at start index.
 */
static inline unsigned short
find_last(struct dp_module_struct *m, unsigned short start)
{
	int i;
	struct dp_record_struct * rec = &m->rec[start];
	unsigned short num_recs = m->pgm.num_points;
	unsigned long end_offset = (rec->point.offset & PAGE_MASK) + PAGE_SIZE;

	for (i = start; i < num_recs && rec->point.offset < end_offset; i++, rec++)
		;

	return i;
}

/*
 * Unlinks the given module from dp_module_list and frees all the memory
 * taken by it.
 */
static int free_dp_module(struct dp_module_struct *m)
{
	/* unlink mod */
	unlink_module(m);

	if (m->flags & DP_MOD_FLAGS_LARGE_REC)
		vfree(m->rec);
	else
		kfree(m->rec);
	kfree(m->lv);
	kfree(m->pgm.rpn_code);
	putname(m->pgm.name);
	kfree(m);
	return 0;
}

/*
 * Allocates dp_record_structs, copies dp_point_structs from user space into
 * the dp_record_structs in the kernel space.
 */
static int
copy_point_from_user (struct dp_module_struct *m, struct dp_pgm_struct *pgm)
{
	unsigned short count = m->pgm.num_points, i;
	struct dp_record_struct *rec;
	unsigned long rec_size;

	if (!count)
		return 0;

	rec_size = count * sizeof(*rec);
	if (rec_size > PAGE_SIZE) {
		rec = vmalloc(rec_size);
		m->flags |= DP_MOD_FLAGS_LARGE_REC;
	} else {
		rec = kmalloc(rec_size, GFP_KERNEL);
	}
	if (!rec)
		return -ENOMEM;
	memset(rec, 0, rec_size);
	m->rec = rec;

	for (i = 0; i < count; i++, rec++) {
		if (copy_from_user (&rec->point, &pgm->point[i],
				sizeof (struct dp_point_struct))) {
			if (m->flags & DP_MOD_FLAGS_LARGE_REC)
				vfree(m->rec);
			else
				kfree(m->rec);
			m->rec = NULL;
			return -EFAULT;
		}
		rec->status = DP_REC_STATUS_COMPILED;
		rec->lock = SPIN_LOCK_UNLOCKED;
		rec->mod = m;
		rec->count = -rec->point.passcount;
		if (!rec->point.maxhits) rec->point.maxhits =DEFAULT_MAXHITS;
	}
	return 0;
}

/* 
 * We may have to realloc global variables area.
 */
static int check_gv_space(unsigned short num_gv)
{
	unsigned long *tmp, *tmp1;	
	unsigned long eflags;

	if (num_gv <= dp_num_gv)
		return 0;

	tmp = kmalloc(num_gv * sizeof(*tmp), GFP_KERNEL);
	if (!tmp) 
		return -ENOMEM;
	memset(tmp, 0, num_gv * sizeof(*tmp));

	write_lock_irqsave(&dp_gv_lock, eflags);
	if (dp_num_gv) {
		memcpy((void *)tmp, (void *)dp_gv, 
			sizeof(unsigned long)*dp_num_gv);
	}
	tmp1 = dp_gv;
	dp_gv = tmp;
	dp_num_gv = num_gv;
	write_unlock_irqrestore(&dp_gv_lock, eflags);

	kfree(tmp1);
	return 0;
} 

static int check_heap_space(unsigned short num_heap)
{
	byte_t *tmp, *tmp1;	
	unsigned long eflags;

	if (num_heap * num_online_cpus() <= dp_num_heap)
		return 0;

	tmp = kmalloc(num_heap * sizeof(*tmp) * num_online_cpus(), GFP_KERNEL);
	if (!tmp) 
		return -ENOMEM;

	write_lock_irqsave(&dp_heap_lock, eflags);
	tmp1 = dp_heap;
	dp_heap = tmp;
	dp_num_heap = num_heap * num_online_cpus();
	write_unlock_irqrestore(&dp_heap_lock, eflags);
	kfree(tmp1);

	return 0;
} 

/*
 * Allocates a dp_module_struct and copies the dp_pgm_struct from user space
 * into it, in kernel space.
 */
static int
copy_pgm_from_user(struct dp_module_struct **mod, struct dp_pgm_struct *pgm)
{
	int  retval = 0;
	struct dp_module_struct *m;

	pgm->name = getname(pgm->name);
	if (IS_ERR(pgm->name)) {
		retval = PTR_ERR(pgm->name);
		goto error;
	}

	m = find_mod_by_name(pgm->name);
	if (m) {
		retval = -EEXIST;
		goto err1;
	}

	m = kmalloc(sizeof(*m), GFP_KERNEL);
	if (!m) {
		retval = -ENOMEM;
		goto err1;
	}
	memset(m, 0, sizeof(*m));

	m->pgm = *pgm;

	if (m->pgm.logmax > LOG_SIZE) m->pgm.logmax = DEFAULT_LOGMAX;
	if (m->pgm.jmpmax > JMP_MAX) m->pgm.jmpmax = DEFAULT_JMPMAX;
	if (m->pgm.ex_logmax > EX_LOG_SIZE) m->pgm.ex_logmax = DEFAULT_EX_LOG_MAX;
	if (m->pgm.heapmax > HEAPMAX)
		m->pgm.heapmax = DEFAULT_HEAPMAX;

	if (pgm->num_lv != 0) {
		m->lv = kmalloc(pgm->num_lv * sizeof(*m->lv), GFP_KERNEL);
		if (!m->lv) {
			retval = -ENOMEM;
			goto err2;
		}
		memset(m->lv, 0, pgm->num_lv * sizeof(*m->lv));
	}

	if (pgm->num_gv != 0) {
		retval = check_gv_space(pgm->num_gv);
		if (retval < 0)
			goto err3;
	}

	if (pgm->heapmax != 0) {
		retval = check_heap_space(pgm->heapmax);
		if (retval < 0)
			goto err3;
	}

	m->pgm.rpn_code = kmalloc(pgm->rpn_length, GFP_KERNEL);
	if (!m->pgm.rpn_code) {
		retval = -ENOMEM;
		goto err3;
	}
	if (copy_from_user(m->pgm.rpn_code, pgm->rpn_code, pgm->rpn_length)) {
		retval = -EFAULT;
		goto err4;
	}

	retval = copy_point_from_user(m, pgm);
	if (retval)
		goto err4;
	*mod = m;
	return 0;

err4: 	kfree(m->pgm.rpn_code);
err3: 	kfree(m->lv);
err2: 	kfree(m);
err1:	putname(pgm->name);
error:	return retval;

}

/*
 * Fills up the base and end fields of the given dp_module_struct with the
 * starting and end addresses of the given kernel module, accounting for
 * correct alignment of the code segment.
 */
static inline int get_kmod_bounds(struct dp_module_struct *m, struct module *kmod)
{
	m->base = (unsigned long)kmod->module_core;
	m->end = kmod->core_size;
	return 1;
}

/*
 * Fills up the base and end fields of the dp_module_struct with the details
 * of where the corresponding kernel module is loaded.
 */
static inline int get_mod_load_addr(struct dp_module_struct *m)
{
	struct module * kmod;
	kmod = find_kmodule(m->pgm.name);
	if (!kmod)
		return 0; // module not yet loaded.
	return get_kmod_bounds(m, kmod);
}

/*
 * Fills up the base and end fields of dp_module_struct if this module is
 * for a kernel or a kernel module.
 */
static int get_load_addr(struct dp_module_struct *m)
{
	if (m->pgm.flags & DP_MODTYPE_KMOD)
		return get_mod_load_addr(m);
	m->base = PAGE_OFFSET;
#ifdef CONFIG_IA64
	m->end =  ((unsigned long) &_etext) - PAGE_OFFSET;
#else
	m->end = init_mm.end_code - PAGE_OFFSET;
#endif
	return 1;
}

/* last thing we do should be to remove the breakpoint setting. */
static int insert_probe(byte_t *addr, struct dp_record_struct *rec, 
	struct dp_module_struct *m, struct page * page, 
	struct vm_area_struct *vma)
{
	/* 
	 * remove residual status bits, applicable only to probes in kernel
	 * modules when they are being reinserted. 
	 */
	rec->status &= ~(DP_REC_STATUS_REMOVED | DP_REC_STATUS_DISABLED);
	if(!(m->pgm.flags & DP_MODTYPE_USER))
		return __insert_probe(addr, rec, m, page);
	else
		return insert_probe_userspace(addr, rec, page, vma);
}

/* first thing we do should be to remove the breakpoint setting. */
static int remove_probe(byte_t * addr, struct dp_record_struct *rec,
	struct dp_module_struct *m, struct page * page, 
	struct vm_area_struct *vma)
{
	unsigned long eflags;
	spin_lock_irqsave(&rec->lock, eflags);
	if(!(m->pgm.flags & DP_MODTYPE_USER))
		__remove_probe(addr, rec);
	else 
		remove_probe_userspace(addr, rec, page, vma);
	rec->status &= ~(DP_REC_STATUS_ACTIVE | DP_REC_STATUS_MISMATCH);
	rec->status |= DP_REC_STATUS_REMOVED;
	spin_unlock_irqrestore(&rec->lock, eflags);
	return 0;
}

/*
 * Creates hash table links and inserts all records in kernel / kmodule.
 */
static int insert_all_recs_k(struct dp_module_struct *m)
{
	struct dp_record_struct * rec;
	byte_t * addr;
	int i;
	unsigned short num_recs = m->pgm.num_points;

	for (i = 0, rec = m->rec; i < num_recs; i++, rec++) {
		if (!(rec->point.address_flag & DP_ADDRESS_ABSOLUTE)) { 
			rec->point.offset += m->base;
		}
		addr = (byte_t *)((unsigned long)rec->point.offset);
		insert_probe(addr, rec, m, NULL, NULL);
	}
	return 0;
}				

/*
 * Removes probes in kernel / kmodules: all _will_ be in memory.
 * Note that rec->point.offset is now the linear address itself.
 */
static int remove_all_recs_k(struct dp_module_struct *m)
{
	struct dp_record_struct * rec;
	int i;
	byte_t * addr;
	unsigned short num_recs = m->pgm.num_points;

	for (i = 0, rec = m->rec; i < num_recs; i++, rec++) {
		addr = (byte_t *) ((unsigned long)rec->point.offset);
		if (rec->status & DP_REC_STATUS_ACTIVE) {
			remove_probe(addr, rec, m, NULL, NULL);
		}
		if (!(rec->point.address_flag & DP_ADDRESS_ABSOLUTE))
			rec->point.offset -= m->base;

	}
	m->base = m->end = 0;
	return 0;
}

/*
 * mod->pgm.name is already in kernel space.
 */
static int insert_k (struct dp_module_struct * m)
{
	if (!get_load_addr(m))
		return 0;
	return insert_all_recs_k(m);
}

static int remove_k(struct dp_module_struct *m)
{
	return remove_all_recs_k(m);
}

/*
 * Inserts user probes into the page. Insert probes that lie in the same page 
 * after waiting for the page to be unlocked, if it was locked.
 */
static unsigned short insert_recs_in_page (struct dp_module_struct *m,
	struct page * page, unsigned short start, unsigned short end, 
		struct vm_area_struct *vma)
{
	unsigned long alias;
	byte_t * addr;
	unsigned short i;
	struct dp_record_struct * rec = &m->rec[start];

	if (!page) {
		return 1;
	}
	wait_on_page_locked(page);
	if (!PageUptodate(page)) { /* we could probably retry readpage here. */
		return 1;
	}

	alias = (unsigned long)kmap(page);
	for (i = start; i < end; i++, rec++) {
		addr = (byte_t *) (alias +
			(unsigned long)(rec->point.offset & ~PAGE_MASK));
		insert_probe(addr, rec, m, page, vma);
	}
	kunmap(page);
	return 1;
}

/*
 * Removes user probes that lie in the same page after waiting for the page to
 * be unlocked, if it was locked. Make sure the page does not get swapped out.
 */
static unsigned short remove_recs_from_page (struct dp_module_struct *m,
	struct page * page, unsigned short start, unsigned short end, 
		struct vm_area_struct *vma)
{
	struct dp_record_struct * rec = &m->rec[start];
	unsigned long alias;
	byte_t * addr;
	unsigned short i;

	if (!page)
		return 1;
	wait_on_page_locked(page);
	lock_page(page);
	alias = (unsigned long)kmap(page); 
	for (i = start; i < end; i++, rec++) {
		addr = (byte_t *) (alias +
			(unsigned long)(rec->point.offset & ~PAGE_MASK)); 
		remove_probe(addr, rec, m, page, vma);
	}
	kunmap(page);
	unlock_page(page);

	return 1;
}

typedef unsigned short
(*process_recs_func_t)(struct dp_module_struct *m, struct page *page,
		unsigned short start, unsigned short end, struct vm_area_struct *);

static inline pte_t *get_one_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		goto end;
	if (pgd_bad(*pgd)) {
		pgd_ERROR(*pgd);
		pgd_clear(pgd);
		goto end;
	}

	pmd = pmd_offset(pgd, addr);
	if (pmd_none(*pmd))
		goto end;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		goto end;
	}

	pte = pte_offset_kernel(pmd, addr);
	if (pte_none(*pte))
		pte = NULL;
end:
	return pte;
}

struct dp_page_locator_struct {
	struct list_head list;
	struct mm_struct *mm;
	unsigned long addr;
};

/*
 * Get the page corresponding to a given virtual addr, marking it as needing
 * to be brought in later and checked, if it has been swapped out. 
 * It doesn't bring in discarded pages as a_ops->readpage
 * takes care of that.
 */
struct page * dp_vaddr_to_page(struct vm_area_struct *vma, unsigned long addr,
struct list_head  *swapped_list_head)
{
	struct mm_struct *mm = vma->vm_mm;
	pte_t *pte = get_one_pte(mm, addr);
	struct page *page;
	struct dp_page_locator_struct *pageloc;

	if (!pte) {
		return NULL;
	}
	/*
	 * If the page has been swapped out, we want to bring it in.
	 * We can't do it right away since we are holding spin locks.
	 * So just queue it up on the swapped pages list.
	 */
	if (!pte_present(*pte)) {
		if (swapped_list_head == NULL) return NULL;	
		pageloc = kmalloc(sizeof(*pageloc), GFP_ATOMIC);
		if (!pageloc) return NULL;
		pageloc->mm = mm;
		pageloc->addr = addr;
		atomic_inc(&mm->mm_users); /* so that mm won't go away */
		list_add(&pageloc->list, swapped_list_head);
		return NULL;
	}

	page = pte_page(*pte);
	page_cache_get(page); /* TODO: Check if we need the page_cache_lock */

	/*
	 * If this is a COW page, then we want to make sure that any changes
	 * made to the page get written back when swapped out. We need to do
	 * the following explicitly because dprobes will write via an alias.
	 */
	if (IS_COW_PAGE(page, vma->vm_file->f_dentry->d_inode)) {
		if (PageSwapCache(page))
			delete_from_swap_cache(page);
		set_pte(pte, pte_mkdirty(*pte));
	}

	return page;
}

/*
 * Tracks all COW pages for the page containing the specified probe point
 * and inserts/removes all the probe points for that page.
 * Avoid the inserts/removes if the page's inode matches the module's inode.
 * since that means that it is the original page and not a copied page.
 */
static int process_recs_in_cow_pages (struct dp_module_struct *m,
		unsigned short start, unsigned short end,
		process_recs_func_t process_recs_in_page)
{
	unsigned long offset = m->rec[start].point.offset;
	struct vm_area_struct *vma;
	struct page *page;
	unsigned long addr;
	struct mm_struct *mm;
	struct prio_tree_iter iter;
	struct address_space *mapping = m->inode->i_mapping;
	struct prio_tree_root *head = &mapping->i_mmap;
	struct list_head swapped_pages_list, *ptr;
	struct dp_page_locator_struct *pageloc;

	INIT_LIST_HEAD(&swapped_pages_list);
	down(&mapping->i_shared_sem);

	vma = __vma_prio_tree_first(head, &iter, start, end);
	while (vma) {
		mm = vma->vm_mm;
		spin_lock(&mm->page_table_lock);	
							
		/* Locate the page in this vma */
		addr = vma->vm_start - (vma->vm_pgoff << PAGE_SHIFT) + offset;
		/*
		 * The following routine would also bring in the page if it is
		 * swapped out
		 */
		page = dp_vaddr_to_page (vma, addr, &swapped_pages_list);
		spin_unlock(&mm->page_table_lock);
		if (!page)
			continue;
		if (IS_COW_PAGE(page, m->inode)) {	
			(*process_recs_in_page)(m, page, start, end, vma);
		}
		page_cache_release(page); /* TODO: Do we need a lock ? */
		vma = __vma_prio_tree_next(vma, head, &iter, start, end);
	}
	up(&mapping->i_shared_sem);

	while (!list_empty(&swapped_pages_list)) { 
		ptr = swapped_pages_list.next;
		pageloc = list_entry(ptr, struct dp_page_locator_struct, list);
		mm = pageloc->mm;      
		addr = pageloc->addr;      
		down_read(&mm->mmap_sem); 
		/* first make sure that the vma didn't go away  */
		vma = find_vma(mm, addr);
		/* and that this is the same one we had */
		if (vma && vma->vm_file && 
		(vma->vm_file->f_dentry->d_inode == m->inode) &&
		(vma->vm_start - (vma->vm_pgoff << PAGE_SHIFT) + offset 
		== addr)) {
			if (handle_mm_fault(mm, vma, addr, 0) <= 0) {
				goto next;
			}
			page = dp_vaddr_to_page(vma, addr, NULL); 
			if (!page) /* could this ever happen ? */ {
				goto next;
			}
			if (IS_COW_PAGE(page, m->inode)) {
				(*process_recs_in_page)(m, page, start, end, vma);
			}
			page_cache_release(page); /*TODO: Do we need a lock ?*/
		}
next:		
		up_read(&mm->mmap_sem); 
		mmput(mm);
		list_del(ptr);
		kfree(pageloc);
	}
	return 1;
}
/* 
 * find_get_vma walks through the list of process private mappings and
 * returns the pointer to vma containing the offset if found.
 */
static struct vm_area_struct * find_get_vma(unsigned long offset, 
	struct address_space *mapping)
{
	struct vm_area_struct *vma = NULL;
	struct prio_tree_iter iter;
	struct prio_tree_root *head = &mapping->i_mmap;
	struct mm_struct *mm;
	unsigned long start, end;

	down(&mapping->i_shared_sem);
	vma = __vma_prio_tree_first(head, &iter, offset, offset);
	while (vma) {
		mm = vma->vm_mm;
		spin_lock(&mm->page_table_lock);
		start = vma->vm_start - (vma->vm_pgoff << PAGE_SHIFT);
		end = vma->vm_end - (vma->vm_pgoff << PAGE_SHIFT);
		spin_unlock(&mm->page_table_lock);
		if ((start + offset) < end) {
			up(&mapping->i_shared_sem);
			return vma;
		}
		vma = __vma_prio_tree_next(vma, head, &iter, offset, offset);
	}
	up(&mapping->i_shared_sem);
	return NULL;
}
/*
 * physical insertion/removal of probes in the actual pages of the module.
 * Register user space probes before actually instering probes in the page for 
 * a given pair of inode and offset.
 * 
 */
static int process_recs(struct dp_module_struct *m,
		process_recs_func_t process_recs_in_page)
{
	struct address_space * mapping = m->inode->i_mapping;
	struct page *page;
	unsigned short i = 0, j;
	unsigned short num_recs = m->pgm.num_points;
	unsigned short start, end;
	struct vm_area_struct *vma = NULL;

	while (i < num_recs) {
		start = i;
		i = end = find_last(m, start);
		vma = find_get_vma(m->rec[start].point.offset, mapping);
		page = find_get_page(mapping, 
				m->rec[start].point.offset >> PAGE_CACHE_SHIFT);
		
		for (j = start; j < end; j++) {
			if (process_recs_in_page == insert_recs_in_page)
				register_userspace_probes(&m->rec[j]);
		}
				
		(*process_recs_in_page)(m, page, start, end, vma);
		if (page) 
			page_cache_release(page); /* TODO: Lock needed ? */
		process_recs_in_cow_pages(m, start, end, process_recs_in_page);

		for (j = start; j < end; j++) {
			if (process_recs_in_page == remove_recs_from_page)
				unregister_userspace_probes(&m->rec[j]);
		}
	}
	return 1;
}

static inline int insert_recs(struct dp_module_struct *m)
{
	return process_recs(m, insert_recs_in_page);
}

static inline int remove_recs(struct dp_module_struct *m)
{
	return process_recs(m, remove_recs_from_page);
}

/*
 * Gets exclusive write access to the given inode to ensure that the file
 * on which probes are currently applied does not change. Use the function,
 * deny_write_access_to_inode() we added in fs/namei.c.
 */
extern int deny_write_access_to_inode(struct inode * inode);
static inline int ex_write_lock(struct inode * inode)
{
	return deny_write_access_to_inode(inode);
}

/*
 * Called when removing user space probes to release the write lock on the
 * inode.
 */
static inline int ex_write_unlock(struct inode * inode)
{
	atomic_inc(&inode->i_writecount);
	return 0;
}

/*
 * Inserts user space probes.
 *
 * mod->pgm.name is already in user space. This function leaves with the
 * dentry held and taking with the inode writelock held to ensure that the
 * file on which probes are currently active does not change from under us.
 */
static int insert_u (struct dp_module_struct * m)
{
	struct address_space *as;
	int error;

	error = user_path_walk(m->pgm.name, &m->nd);
	if (error)
		return error;

	m->inode = m->nd.dentry->d_inode;
	as = m->inode->i_mapping;

	error = ex_write_lock(m->inode);
	if (error) {
		path_release(&m->nd);
		return error;
	}

	/* switch i_op to hook into readpage */
	m->ori_a_ops = as->a_ops;
	m->dp_a_ops = *as->a_ops;
	m->dp_a_ops.readpage = dp_readpage;
	as->a_ops = &m->dp_a_ops;

	/* physically insert the probes in existing pages */
	insert_recs(m);

	return 0;
}

/*
 * Removes user space probes.
 */
static int remove_u(struct dp_module_struct *m)
{
	m->inode->i_mapping->a_ops = m->ori_a_ops; /* unhook readpage */
	remove_recs(m); /* remove the probes */
	ex_write_unlock(m->inode);
	path_release(&m->nd); /* release path */
	return 0;
}

/*
 * Entry routine for the insert command. It checks for existing probes in this
 * module to avoid any duplications, creates the necessary data structures
 * calls insert_k() or insert_u() depending on whether it is a kernel space or
 * user space probe.
 */
static int do_insert(struct dp_pgm_struct *upgm)
{
	struct dp_module_struct *m;
	struct dp_pgm_struct *pgm;
	char *uname, *kname;
	int retval;

	if (!upgm)
		return -EINVAL;

	pgm = kmalloc (sizeof(*pgm), GFP_KERNEL);
	if (!pgm)
		return -ENOMEM;

	if (copy_from_user(pgm, upgm, sizeof(*pgm))) {
		retval = -EFAULT;
		goto done;
	}

	if (!pgm->name || !pgm->num_points) {
		retval = -EINVAL;
		goto done;
	}
	uname = pgm->name;
	retval = copy_pgm_from_user(&m, pgm);
	if (retval)
		goto done;

	try_module_get(THIS_MODULE);
	down_write(&dp_modlist_sem);
	link_module(m);
	if (pgm->flags & DP_MODTYPE_USER) {
		/* kludge: needed to use user_path_walk. */
		kname = m->pgm.name;
		m->pgm.name = uname;
		retval = insert_u(m);
		m->pgm.name = kname;
	} else {
		retval = insert_k(m);
	}
	
	if (retval) {
		free_dp_module(m);
		module_put(THIS_MODULE);
	} else {
		init_trace_hdr(m);
		get_trace_id(m);
	}
	up_write(&dp_modlist_sem);

done:	kfree(pgm);
	return retval;
}

static int remove_module(struct dp_module_struct *m)
{
	/* remove probes */
	if (m->pgm.flags & DP_MODTYPE_USER) {
		remove_u(m);
	} else {
		remove_k(m);
	}
	free_trace_id(m);

	/* free module and all its contents */
	free_dp_module(m);
	return 0;
}

/*
 * Entry routine for removing probes.
 */
static int do_remove (unsigned long cmd, char * uname)
{
	struct dp_module_struct *m, *tmp;
	char *name;

	if (cmd & DP_ALL) {
		down_write(&dp_modlist_sem);
		m = dp_module_list;
		while (m) {
			tmp = m->next;
			remove_module (m);
			m = tmp;
			module_put(THIS_MODULE);
		}
		up_write(&dp_modlist_sem);
	} else {
		if (!uname)
			return -EINVAL;
		name = getname(uname);
		if (IS_ERR(name))
			return (PTR_ERR(name));

		down_write(&dp_modlist_sem);
		m = find_mod_by_name (name);
		putname(name);
		if (!m) {
			up_write(&dp_modlist_sem);
			return -EINVAL;
		}
		remove_module (m);
		module_put(THIS_MODULE);
		up_write(&dp_modlist_sem);
	}
	return 0;
}

static int 
dp_get_globalvars(unsigned long cmd, struct dp_getvars_in_struct * req, 
		byte_t * result)
{
	unsigned long num_vars;
	unsigned long *tmp_gv = NULL;
	unsigned long eflags;
	unsigned short orig_to = req->to;
	int retval = 0;

	read_lock_irqsave(&dp_gv_lock, eflags);
	if (!(cmd & DP_GETVARS_INDEX)) {
		req->from = 0;
		req->to = dp_num_gv - 1;
		num_vars = dp_num_gv;
     	} else if (req->from >= dp_num_gv) {
		num_vars = 0;
	} else if (req->to >= dp_num_gv) {
		req->to = dp_num_gv - 1;
		num_vars = req->to - req->from + 1;
	} else {
		num_vars = req->to - req->from + 1;
	}

	if (cmd & DP_GETVARS_RESET) {
		memset(dp_gv + req->from, 0, num_vars*sizeof(unsigned long));
		read_unlock_irqrestore(&dp_gv_lock, eflags);
		goto done;
	}

	read_unlock_irqrestore(&dp_gv_lock, eflags);

	if (num_vars) {
		tmp_gv = kmalloc(num_vars * sizeof(*tmp_gv), GFP_KERNEL);
		if (!tmp_gv) {
			retval = -ENOMEM;
			goto done;
		}

		read_lock_irqsave(&dp_gv_lock, eflags);

		/*
		 * dp_num_gv can change(only increase) during kmalloc. In such
		 * a case, only copy the previously determined number
		 * of global vars.
		 */
		memcpy((void *)tmp_gv, (void *)(dp_gv + req->from), 
			sizeof(unsigned long) * num_vars);
		read_unlock_irqrestore(&dp_gv_lock, eflags);
	}
	
	req->returned +=  (num_vars + 1) * sizeof(unsigned long); 
       	if (req->returned < req->allocated) {
		if (copy_to_user(result, &num_vars, sizeof(unsigned long)))
			retval = -EFAULT;
		else if (num_vars) {
			if (copy_to_user(result + sizeof(unsigned long), 
				tmp_gv, sizeof(unsigned long) * num_vars))
			retval = -EFAULT;
			kfree(tmp_gv);
		}
	}
	else
		retval = -ENOMEM;
done:
	req->to = orig_to;
	return retval;
}

static int
cp_lvars_for_mod(struct dp_module_struct *m, struct dp_getvars_in_struct * req,
		byte_t * result, unsigned long var_bytes)
{
	unsigned long len = 0;
	unsigned long offset = 0;
	unsigned long num_vars = req->to - req->from + 1;

       	len = sizeof(struct dp_getvars_local_out_struct) +
			var_bytes + strlen(m->pgm.name) + 1;
	req->returned += len;

       	if (req->returned < req->allocated) {
		if (copy_to_user(result, &len, sizeof(unsigned long)))
			return -EFAULT;
		else
			offset = sizeof(unsigned long);

		if (copy_to_user(result+offset, &num_vars,
				sizeof(unsigned long)))
			return -EFAULT;
		else
			offset += sizeof(unsigned long);

		if (copy_to_user(result+offset, m->lv + req->from, var_bytes))
			return -EFAULT;
		else
			offset += var_bytes;

		if (copy_to_user(result+offset, m->pgm.name,
				strlen(m->pgm.name)+1))
			return -EFAULT;
	}
	else
		return -ENOMEM;

	return 0;
}

static int
getvars_m(struct dp_module_struct *m, unsigned long cmd,
		struct dp_getvars_in_struct *req, byte_t *result)
{
	int retval = 0;
	unsigned long var_bytes;
	unsigned short orig_to = req->to;

	if (cmd & DP_GETVARS_INDEX) {
		if (req->from >= m->pgm.num_lv)
			return 0;
		if(req->to >= m->pgm.num_lv)
			req->to = m->pgm.num_lv - 1;
     	}
	else {
		req->from = 0;
		req->to = m->pgm.num_lv - 1;
	}
	var_bytes = (req->to - req->from + 1) * sizeof(unsigned long);

	if (cmd & DP_GETVARS_RESET)
		memset(m->lv + req->from, 0, var_bytes);
	else
		retval = cp_lvars_for_mod(m, req, result, var_bytes);
	req->to = orig_to;
	return retval;
}

static int
do_getvars(unsigned long cmd, struct dp_getvars_in_struct * ureq,
		byte_t * result)
{
	struct dp_module_struct *m;
	struct dp_getvars_in_struct req;
	int retval = 0;

	if (!ureq)
		return -EINVAL;

	if (copy_from_user(&req, ureq, sizeof(req))) {
		retval = -EFAULT;
		goto failed;
	}
	if (req.name) {
		req.name = getname(req.name);
		if (IS_ERR(req.name)) {
			retval = PTR_ERR(req.name);
			goto failed;
		}
	}
	else
		req.name = NULL;

       	req.returned = 0;
	if (cmd & DP_GETVARS_GLOBAL) {
		retval = dp_get_globalvars(cmd, &req, result);
		if (retval) 
			goto done;
	}

	if (cmd & DP_GETVARS_LOCAL) {
		down_read(&dp_modlist_sem);
		if (cmd & DP_ALL) {
			for (m = dp_module_list; m; m = m->next) {
				if (!m->lv) 
					continue;
				retval = getvars_m(m, cmd, &req, result + req.returned);
				if (retval && (retval != -ENOMEM))
					break;
			}
		} else if (req.name) {
			m = find_mod_by_name(req.name);
			if (m && m->lv)
				retval = getvars_m(m, cmd, &req, result + req.returned);
			else 
				retval = -EINVAL;
		}
		up_read(&dp_modlist_sem);
	}
done:
	if (copy_to_user(&ureq->returned, &req.returned,
			sizeof(unsigned long)))
		retval = -EFAULT;

	if (req.name)
		putname(req.name);
failed:
	return retval;
}

#define dp_copy_to_user(destination, source, length) \
do { \
	if (copy_to_user(destination, source, length)) \
		return -EFAULT; \
	destination += length; \
} while(0)

static int query_r(struct dp_module_struct *m, byte_t **result)
{
 	struct dp_outrec_struct outrec;
 	struct dp_record_struct * rec;
 	unsigned short i;
 	unsigned short num_recs = m->pgm.num_points;
 	byte_t * offset = *result;
 	unsigned long eflags;

 	for (i = 0, rec = m->rec; i < num_recs; i++, rec++) {
 		spin_lock_irqsave(&rec->lock, eflags);
		outrec.status = rec->status;
		outrec.count = rec->count;
		outrec.dbregno = rec->dbregno;
		outrec.point = rec->point;
 		spin_unlock_irqrestore(&rec->lock, eflags);
 		dp_copy_to_user(offset, &outrec, sizeof(outrec));
 	}
 	*result = offset;
	return 0;
}

static int
query_m(struct dp_module_struct *m, struct dp_query_in_struct *req,
		byte_t * result)
{
	unsigned long len = 0;
	byte_t * offset = result;
	struct dp_outmod_struct outmod;
	int retval;
	unsigned long rec_bytes = m->pgm.num_points *
				  sizeof(struct dp_outrec_struct);
	unsigned long var_bytes = m->pgm.num_lv * sizeof(m->lv);
	unsigned long rpn_bytes = m->pgm.rpn_length;
	unsigned long mod_name_len = strlen(m->pgm.name) + 1;

	len = sizeof(struct dp_outmod_struct) + var_bytes + rec_bytes
		+ rpn_bytes + mod_name_len;

	req->returned += len;
	if (req->returned <= req->allocated) {
		outmod.pgm = m->pgm;
		outmod.flags = m->flags;
		outmod.base = m->base;
		outmod.end = m->end;

		offset += sizeof(outmod);

		outmod.rec = (struct dp_outrec_struct *) offset ;
		retval = query_r(m, &offset);
		if (retval)
			return retval;

		if (var_bytes) {
			outmod.lv = (unsigned long *) offset;
			dp_copy_to_user(offset, m->lv, var_bytes);
		} else
			outmod.lv = NULL;

		outmod.pgm.rpn_code = offset;
		dp_copy_to_user(offset, m->pgm.rpn_code, rpn_bytes);

		outmod.pgm.name = (char *) offset;
		dp_copy_to_user(offset, m->pgm.name, mod_name_len);
		if (m->next && !(req->name))
			outmod.next = (struct dp_outmod_struct *) offset;
		else
			outmod.next = NULL;
		offset = result;
		dp_copy_to_user(offset, &outmod, sizeof(outmod));
	}
	else
		return -ENOMEM;
	return 0;
}

static int
do_query(unsigned long cmd, struct dp_query_in_struct * ureq, byte_t * result)
{
	struct dp_query_in_struct req;
	struct dp_module_struct *m;
	int retval = 0;

	if (!ureq)
		return -EINVAL;
	if (copy_from_user(&req, ureq, sizeof(req))) {
		retval = -EFAULT;
		goto failed;
	}
	if (req.name) {
		req.name = getname(req.name);
		if (IS_ERR(req.name)) {
			retval = PTR_ERR(req.name);
			goto failed;
		}
	}
	else
		req.name = NULL;
	req.returned = 0;

	down_read(&dp_modlist_sem);
	if (cmd & DP_ALL) {
		for (m = dp_module_list; m; m = m->next) {
			retval = query_m(m, &req, result + req.returned);
			if (retval && (retval != -ENOMEM))
				break;
		}
	}
	else if (req.name) {
		m = find_mod_by_name(req.name);
		if (m) 
			retval = query_m(m, &req, result + req.returned);
		else 
			retval = -EINVAL;
	}
	up_read(&dp_modlist_sem);

	if (copy_to_user(&ureq->returned, &req.returned,
			sizeof(unsigned long)))
		retval = -EFAULT;
	if (req.name)
		putname(req.name);
failed:
	return retval;
}

/*
 * Returns the index of the first probe record that lies in the page
 * starting with given offset.
 */
static inline int
find_first_in_page(struct dp_module_struct * m, unsigned long offset)
{
	int i;
	unsigned short num_recs = m->pgm.num_points;
	struct dp_record_struct * rec;
	unsigned long end = offset + PAGE_SIZE;

	for (i = 0, rec = m->rec; i < num_recs; i++, rec++) {
		if (rec->point.offset >=offset && rec->point.offset < end)
			return i;
	}
	return -1;
}

/*
 * This routine does the post processing for all pages that are brought
 * in from a user module which has active probes in it.
 */
static int fixup_page (struct dp_module_struct *m, struct page *page)
{
	int start;
	unsigned short end;
	struct vm_area_struct *vma = NULL;

	/* find and insert all probes in this page */
	start = find_first_in_page(m, (page->index << PAGE_CACHE_SHIFT));
	if (start < 0)
		return -1;
	end = find_last(m, start);

	return insert_recs_in_page (m, page, start, end, vma);
}

/*
 * This function handles the readpage of all modules that have active probes
 * in them. Here, we first call the original readpage() of this
 * inode / address_space to actually read the page into memory. Then, we will
 * insert all the probes that are specified in this page before returning.
 */
int dp_readpage(struct file *file, struct page *page)
{
	int retval = 0;
	struct dp_module_struct *m;

	down_read(&dp_modlist_sem);
	m = find_mod_by_inode(file->f_dentry->d_inode);
	if (!m) {
		printk("dp_readpage: major problem. we don't have mod for this !!!\n");
		up_read(&dp_modlist_sem);
		return -EINVAL;
	}

	/* call original readpage() */
	retval = m->ori_a_ops->readpage(file, page);
	if (retval >= 0)
		fixup_page (m, page);

	up_read(&dp_modlist_sem);
	return retval;
}

/*
 * This is called from kernel/module.c,sys_init_module() after a kernel module
 * is loaded before it is initialized to give us a chance to insert any probes
 * in it.
 */
int dp_insmod(struct module *kmod)
{
	struct dp_module_struct *m;
	int retval;
	char kmod_name[MODULE_NAME_LEN];

	down_read(&dp_modlist_sem);
	m = find_mod_by_name(kmod->name);
	if (!m) {
		strcpy(kmod_name, kmod->name);
		strcat(kmod_name, ".ko");
		m = find_mod_by_name(kmod_name);
	}

	if (!m) {
		retval = -1;
	} else {
		get_kmod_bounds(m, kmod);
		retval = insert_all_recs_k(m);
	}
	up_read(&dp_modlist_sem);
	return retval;
}

/*
 * This is called from kernel/module.c, free_module() before freeing a kernel
 * module and from sys_init_module() when a kernel module initialization fails
 * to give us a chance to remove any probes in it.
 */
int dp_remmod(struct module *kmod)
{
	struct dp_module_struct *m;
	int retval;
	char kmod_name[MODULE_NAME_LEN];

	down_read(&dp_modlist_sem);
	m = find_mod_by_name(kmod->name);
	if (!m) {
		strcpy(kmod_name, kmod->name);
		strcat(kmod_name, ".ko");
		m = find_mod_by_name(kmod_name);
	}
	if (!m)
		retval = -1;
	else
		retval = remove_all_recs_k(m);
	up_read(&dp_modlist_sem);
	return retval;
}

/* 
 * ioctl function for dprobes driver.
 */
static int dp_ioctl (struct inode *in, struct file *fp, unsigned int ioctl_cmd,
	       unsigned long arg)
{
	struct dp_ioctl_arg dp_arg;
	int retval = 0;

	if (current->euid) {
		return -EACCES;
	}

	if (copy_from_user((void *)&dp_arg, (void *)arg, sizeof(dp_arg))) {
		module_put(THIS_MODULE);
		return -EFAULT;
	}

	switch (dp_arg.cmd & DP_CMD_MASK) {
	case DP_INSERT:
		retval = do_insert(dp_arg.input); 
		break;
	case DP_REMOVE:
		retval = do_remove(dp_arg.cmd, dp_arg.input); 
		break; 
	case DP_GETVARS:
		retval = do_getvars(dp_arg.cmd, dp_arg.input, dp_arg.output); 
		break;
	case DP_QUERY:
		retval = do_query(dp_arg.cmd, dp_arg.input, dp_arg.output); 
		break;
	default:
		retval = -EINVAL;
		break;
	}
	return retval;	
}

static struct file_operations dprobes_ops = {
	owner:	THIS_MODULE,
	ioctl:	dp_ioctl
};

static int dprobes_major;

#ifdef CONFIG_MAGIC_SYSRQ

/* SysRq handler - emergency removal of all probes via Alt-SysRq-V */

static void do_remove_sysrq(struct dprobes_struct *dp) 
{
	int retval;
	if ((retval = do_remove(DP_ALL, NULL)))
		printk("<1>Probe removal failed rc %i\n", retval);
	else
		printk("<1> All probes removed\n");
	test_and_clear_bit(0, &emergency_remove);
	return;
}

static void dprobes_sysrq( int key, struct pt_regs *ptr, struct tty_struct *tty) 
{

	int retval;
	retval = test_and_set_bit(0,&emergency_remove);
	if (!retval) 
		queue_work(dprobes_wq, &dprobes_work);
	return;

}
#endif

static int dp_module_load_notify(struct notifier_block * self, unsigned long val, void * data)
{
	struct module *kmod =(struct module *) data;
	dp_insmod(kmod);
	return 0;
}

static struct notifier_block dp_module_load_nb = {
	.notifier_call = dp_module_load_notify,
};

static int dp_rmmodule_load_notify(struct notifier_block * self, unsigned long val, void * data)
{
	struct module *kmod =(struct module *) data;
	dp_remmod(kmod);
	return 0;
}

static struct notifier_block dp_rmmodule_load_nb = {
	.notifier_call = dp_rmmodule_load_notify,
};

static int __init dprobes_init_module(void)
{
	int retval = 0;

	dprobes_major = register_chrdev(0, "dprobes", &dprobes_ops);
	if (dprobes_major < 0) {
		printk("Failed to register dprobes device\n");
		retval = dprobes_major;
		goto err;
	}

	printk("IBM Dynamic Probes v%d.%d.%d loaded.\n", DP_MAJOR_VER, 
			DP_MINOR_VER, DP_PATCH_VER);

	retval = register_module_notifier(&dp_module_load_nb);
	if (retval)
		goto err;
	register_rmmodule_notifier(&dp_rmmodule_load_nb);
	if (retval)
		goto err1;

#ifdef CONFIG_MAGIC_SYSRQ
	if (!(dprobes_wq = create_workqueue("dprobes_wq"))) {
		printk("dprobes create_workqueue failed\n");
		retval = -ENOMEM;
		goto err2;
	}
	INIT_WORK(&dprobes_work, (void *)(void *)do_remove_sysrq, NULL);
	if ((retval = register_sysrq_key('v', &key_op)))
		printk("<1>register_sysrq_key returned %i\n", retval);
#endif
	return retval;
err2:	unregister_rmmodule_notifier(&dp_module_load_nb);
	printk("called unreg\n");
err1:	unregister_module_notifier(&dp_rmmodule_load_nb);
	printk("called unreg\n");
err:	return retval;

}

static void __exit dprobes_cleanup_module(void)
{
	int retval;
	unregister_chrdev(dprobes_major, "dprobes");
	
	printk("IBM Dynamic Probes v%d.%d.%d unloaded.\n", DP_MAJOR_VER, 
			DP_MINOR_VER, DP_PATCH_VER);

	unregister_rmmodule_notifier(&dp_module_load_nb);
	unregister_module_notifier(&dp_rmmodule_load_nb);
#ifdef CONFIG_MAGIC_SYSRQ        
	destroy_workqueue(dprobes_wq);
	if ((retval = unregister_sysrq_key('v', &key_op)))
		printk("<1>unregister_sysrq_key returned %i\n", retval);
#endif   
	return;
}

module_init(dprobes_init_module);
module_exit(dprobes_cleanup_module);

MODULE_AUTHOR("IBM");
MODULE_DESCRIPTION("Dynamic Probes");
MODULE_LICENSE("GPL");

void dprobes_code_end(void) { }
