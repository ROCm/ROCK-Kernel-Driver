/* Support for kernel probes.
   (C) 2002 Vamsi Krishna S <vamsi_krishna@in.ibm.com>.
   Support for user space probes.
   (C) 2003 Prasanna S Panchamukhi <prasanna@in.ibm.com>.
*/
#include <linux/kprobes.h>
#include <linux/sysrq.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <asm/cacheflush.h>
#include <asm/errno.h>

#define KPROBE_HASH_BITS 6
#define KPROBE_TABLE_SIZE (1 << KPROBE_HASH_BITS)

static struct list_head kprobe_table[KPROBE_TABLE_SIZE];

unsigned int kprobe_cpu = NR_CPUS;
static spinlock_t kprobe_lock = SPIN_LOCK_UNLOCKED;

void kprobes_code_start(void){}

/* Locks kprobe: irqs must be disabled */
void lock_kprobes(void)
{
	spin_lock(&kprobe_lock);
	kprobe_cpu = smp_processor_id();
}

void unlock_kprobes(void)
{
	kprobe_cpu = NR_CPUS;
	spin_unlock(&kprobe_lock);
}

static struct kprobe *get_uprobe_at(struct inode *inode, unsigned long offset)
{
	struct list_head *head;
	struct kprobe *p;
	
	head = &kprobe_table[hash_long((unsigned long)inode*offset, 
					KPROBE_HASH_BITS)];
	list_for_each_entry(p, head, list) {
		if (p->user->inode == inode && p->user->offset == offset)
			return p;
	}
	return NULL;
}

/*
 * This leaves with page, and kmap held. They will be released in 
 * put_kprobe_user. Not sure if holding page_table_lock is also 
 * needed, it is a very small, probably can't happen, race where 
 * vma could be gone by the time we complete the single-step.
 */
static void get_kprobe_user(struct uprobe *u)
{
	kprobe_opcode_t *addr;
	u->page = find_get_page(u->inode->i_mapping, u->offset >> PAGE_CACHE_SHIFT);
	addr = (kprobe_opcode_t *)kmap_atomic(u->page, KM_USER0);
	u->addr = (kprobe_opcode_t *) ((unsigned long) addr + (unsigned long) ( u->offset & ~PAGE_MASK));

}

static void put_kprobe_user(struct uprobe *u)
{
	kunmap_atomic(u->addr, KM_USER0);	
	page_cache_release(u->page);
}

/*
 * We need to look up the inode and offset from the vma. We can't depend on
 * the page->(mapping, index) as that would be incorrect if we ever swap this
 * page out (possible for pages which are dirtied by GDB breakpoints etc)
 * 
 * We acquire page_table_lock here to ensure that:
 *	- current page doesn't go away from under us (kswapd)
 *	- mm->mmap consistancy (vma are always added under this lock)
 *
 * We will never deadlock on page_table_lock, we always come here due to a
 * probe in user space, no kernel code could have executed to take the
 * page_table_lock.
 */
static struct kprobe *get_uprobe(void *addr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct inode *inode;
	unsigned long offset;
	struct kprobe *p;

	spin_lock(&mm->page_table_lock); 
	vma = find_vma(mm, (unsigned long)addr);
	offset = (unsigned long)addr - vma->vm_start + (vma->vm_pgoff << PAGE_SHIFT);
	if (!vma->vm_file) {
		spin_unlock(&mm->page_table_lock);
		return NULL;
	}
	inode = vma->vm_file->f_dentry->d_inode;
	spin_unlock(&mm->page_table_lock);

	p = get_uprobe_at(inode, offset);
	if (p) {
		get_kprobe_user(p->user);
		p->addr = addr;
		p->user->vma = vma;
	}
	return p;
}

/* You have to be holding the kprobe_lock */
struct kprobe *get_kprobe(void *addr)
{
	struct list_head *head, *tmp;

	if ((unsigned long)addr < PAGE_OFFSET)
		return get_uprobe(addr);

	head = &kprobe_table[hash_ptr(addr, KPROBE_HASH_BITS)];
	list_for_each(tmp, head) {
		struct kprobe *p = list_entry(tmp, struct kprobe, list);
		if (p->addr == addr)
			return p;
	}
	return NULL;
}

void put_kprobe(struct kprobe *p)
{
	if (p->user)
		put_kprobe_user(p->user);
}

static void set_opcode_user(struct kprobe *p, kprobe_opcode_t opcode)
{
	*p->user->addr = opcode;
	flush_icache_user_range(p->user->vma, p->user->page, addr, sizeof(kprobe_opcode_t));
}

static void set_opcode_k(struct kprobe *p, kprobe_opcode_t opcode)
{
	kprobe_opcode_t *addr = p->addr;
	*addr = opcode;
	flush_icache_range(addr, addr + sizeof(kprobe_opcode_t));
}

void set_opcode(struct kprobe *p, kprobe_opcode_t opcode)
{
	if ((unsigned long)p->addr > PAGE_OFFSET)
		set_opcode_k(p, opcode);
	else
		set_opcode_user(p, opcode);
}

int register_kprobe(struct kprobe *p)
{
	int ret = 0;
	kprobe_opcode_t *addr = p->addr;

	spin_lock_irq(&kprobe_lock);
	if (get_kprobe(addr)) {
		ret = -EEXIST;
		goto out;
	}
	list_add(&p->list, &kprobe_table[hash_ptr(addr, 
					 KPROBE_HASH_BITS)]);
	arch_prepare_kprobe(p);
	p->opcode = *addr;
	set_opcode_k(p, BREAKPOINT_INSTRUCTION);
 out:
	spin_unlock_irq(&kprobe_lock);
	return ret;
}

void unregister_kprobe(struct kprobe *p)
{
	spin_lock_irq(&kprobe_lock);
	set_opcode_k(p, p->opcode);
	list_del(&p->list);
	spin_unlock_irq(&kprobe_lock);
}

/* 
 * User space probes defines four simple and lightweight interfaces :
 *
 * register_kprobe_user :
 * 	Registration of user space probes is defined for a pair of inode and
 * 	offset within the executable where the probes need to be inserted.
 *	Similar to kernel space probes registration, the user space 
 *	registration provides three callback functions. 
 * 	User space registration requires a kprobes structure as an argument 
 *	with the following fields initialized:
 *
 * pre_handler - pointer to the function that will be called when the 
 *	probed instruction is about to execute.
 * post_handler - pointer to the function that will be called after the 
 *	successful execution of the instruction.
 * fault_handler - pointer to the function that will be called if a 
 * 	software exception occurs, while executing inside the probe_handler
 *	or while single stepping.
 * user - pointer to the struct uprobe. The following fields within this 
 * 	structure must be initialized:
 * 
 *	inode -  pointer to the inode of the executable.
 * 	offset - offset within the executable, where probes are 
 *		inserted/removed. Each unique pair of inode and offset is 
 *		used as a hashing function.
 *
 * User is required to retain this structure until the probes are unregistered.
 * This routine should be called only once for a given pair of inode and 
 * offset. Only after the user space probes are registered, the user
 * can insert and remove probes at the given address.
 */

int register_kprobe_user(struct kprobe *p)
{
	int ret = 0;
	spin_lock_irq(&kprobe_lock);
	if (get_uprobe_at(p->user->inode, p->user->offset)) {
		ret = -EEXIST;
		goto out;
	} 
	list_add(&p->list, &kprobe_table[hash_long(
		(unsigned long)p->user->inode * p->user->offset,
		KPROBE_HASH_BITS)]);
	p->opcode = BREAKPOINT_INSTRUCTION;
 out :
	spin_unlock_irq(&kprobe_lock);
	return ret;

}

/* 
 * unregister_kprobe_user:
 * 	Every registered user space probe must be unregistered.
 * 	For unregistering, kprobes structure must be passed with
 * 	the following fields initialized:
 *
 * user->inode -  pointer to the inode of the executable.
 * user->offset - offset within the executable, where the probe was registered.
 *
 * This routine must be called after all the probes for a given 
 * pair of inode and offset are removed.
 */

void unregister_kprobe_user(struct kprobe *p)
{
	spin_lock_irq(&kprobe_lock);
	if (get_uprobe_at(p->user->inode, p->user->offset)) 
		list_del(&p->list);
	spin_unlock_irq(&kprobe_lock);
}

/* 
 * insert_kprobe_userspace:
 *	To insert a user space probe within a page at a given address,
 *	this routine must be called with the following fields initialized:
 *
 * user->inode -  pointer to the inode of the executable.
 * user->offset - offset within the executable, where we need to insert a probe.
 * user->addr - address in the user address space where probes need to inserted.
 * user->page - pointer to the page containing the address.
 * user->vma - pointer to the vma containing the address.
 *
 * The user space probes can be inserted into the pages existing in the memory or 
 * pages read via readpage routine of the inode's address space operations or 
 * swapping in of the process private page. 
 * User has to ensure that the page containing the specified address 
 * (user->addr) must be present in the memory before calling.
 * User has to first register user space probes for a given pair of 
 * of inode and offset before calling this routine to insert probes.
 * 
 */

int insert_kprobe_user(struct kprobe *p)
{
	int ret = 0;
	spin_lock_irq(&kprobe_lock);
	if (!get_uprobe_at(p->user->inode, p->user->offset)) {
		ret = -ENODATA;
		goto out;
	}

	if (*p->user->addr == BREAKPOINT_INSTRUCTION) {
		ret = -EEXIST;
		goto out;
	}
		
	p->opcode = *p->user->addr;
	set_opcode_user(p, BREAKPOINT_INSTRUCTION);
 out :
	spin_unlock_irq(&kprobe_lock);
	return ret;
}

/*
 * remove_kprobe_user:
 * For removing user space probe from a page at the given address, this routine must be
 * called with the following fields initialized:
 *
 * user->inode -  pointer to the inode of the executable.
 * user->offset - offset within the executable,where we need to remove a probe.
 * user->addr - address in user address space where probes need to removed.
 * user->page - pointer to the page containing the address.
 * user->vma - pointer to the vma containing the address.
 * 
 * User has to ensure that the page containing the specified address 
 * (user->addr) must be present in the memory before calling this routine.
 * Before unregistering, all the probes inserted at a given inode and offset
 * must be removed. 
 */

int remove_kprobe_user(struct kprobe *p)
{
	int ret = 0;
	spin_lock_irq(&kprobe_lock);
	if (!get_uprobe_at(p->user->inode, p->user->offset)) {
		ret = -ENODATA;
		goto out;
	}
	set_opcode_user(p, p->opcode);
 out:
	spin_unlock_irq(&kprobe_lock);
	return ret;
}

static void show_kprobes(int key, struct pt_regs *pt_regs, struct tty_struct *tty)
{
	int i;
	struct kprobe *p;
	
	/* unsafe: kprobe_lock ought to be taken here */
	for(i = 0; i < KPROBE_TABLE_SIZE; i++) {
		if (!list_empty(&kprobe_table[i])) {
			list_for_each_entry(p, &kprobe_table[i], list) {
				printk("[<%p>] ", p->addr);
				print_symbol("%s\t", (unsigned long)p->addr);
				print_symbol("%s\n", (unsigned long)p->pre_handler);
			}
		}
	}
}

static struct sysrq_key_op sysrq_show_kprobes = {
	.handler        = show_kprobes,
	.help_msg       = "shoWkprobes",
	.action_msg     = "Show kprobes\n"
};

static int __init init_kprobes(void)
{
	int i;

	register_sysrq_key('w', &sysrq_show_kprobes);
	/* FIXME allocate the probe table, currently defined statically */
	/* initialize all list heads */
	for (i = 0; i < KPROBE_TABLE_SIZE; i++)
		INIT_LIST_HEAD(&kprobe_table[i]);
	return 0;
}

void kprobes_code_end(void) {}
__initcall(init_kprobes);

EXPORT_SYMBOL_GPL(register_kprobe);
EXPORT_SYMBOL_GPL(unregister_kprobe);
EXPORT_SYMBOL_GPL(register_kprobe_user);
EXPORT_SYMBOL_GPL(unregister_kprobe_user);
EXPORT_SYMBOL_GPL(insert_kprobe_user);
EXPORT_SYMBOL_GPL(remove_kprobe_user);
EXPORT_SYMBOL_GPL(kprobes_code_start);
EXPORT_SYMBOL_GPL(kprobes_code_end);
EXPORT_SYMBOL_GPL(kprobes_asm_code_start);
EXPORT_SYMBOL_GPL(kprobes_asm_code_end);
