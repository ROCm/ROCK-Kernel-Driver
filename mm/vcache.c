/*
 *  linux/mm/vcache.c
 *
 *  virtual => physical page mapping cache. Users of this mechanism
 *  register callbacks for a given (virt,mm,phys) page mapping, and
 *  the kernel guarantees to call back when this mapping is invalidated.
 *  (ie. upon COW or unmap.)
 *
 *  Started by Ingo Molnar, Copyright (C) 2002
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/vcache.h>

#define VCACHE_HASHBITS 8
#define VCACHE_HASHSIZE (1 << VCACHE_HASHBITS)

spinlock_t vcache_lock = SPIN_LOCK_UNLOCKED;

static struct list_head hash[VCACHE_HASHSIZE];

static struct list_head *hash_vcache(unsigned long address,
					struct mm_struct *mm)
{
        return &hash[hash_long(address + (unsigned long)mm, VCACHE_HASHBITS)];
}

void __attach_vcache(vcache_t *vcache,
		unsigned long address,
		struct mm_struct *mm,
		void (*callback)(struct vcache_s *data, struct page *new))
{
	struct list_head *hash_head;

	address &= PAGE_MASK;
	vcache->address = address;
	vcache->mm = mm;
	vcache->callback = callback;

	hash_head = hash_vcache(address, mm);

	list_add_tail(&vcache->hash_entry, hash_head);
}

void __detach_vcache(vcache_t *vcache)
{
	list_del_init(&vcache->hash_entry);
}

void invalidate_vcache(unsigned long address, struct mm_struct *mm,
				struct page *new_page)
{
	struct list_head *l, *hash_head;
	vcache_t *vcache;

	address &= PAGE_MASK;

	hash_head = hash_vcache(address, mm);
	/*
	 * This is safe, because this path is called with the pagetable
	 * lock held. So while other mm's might add new entries in
	 * parallel, *this* mm is locked out, so if the list is empty
	 * now then we do not have to take the vcache lock to see it's
	 * really empty.
	 */
	if (likely(list_empty(hash_head)))
		return;

	spin_lock(&vcache_lock);
	list_for_each(l, hash_head) {
		vcache = list_entry(l, vcache_t, hash_entry);
		if (vcache->address != address || vcache->mm != mm)
			continue;
		vcache->callback(vcache, new_page);
	}
	spin_unlock(&vcache_lock);
}

static int __init vcache_init(void)
{
        unsigned int i;

	for (i = 0; i < VCACHE_HASHSIZE; i++)
		INIT_LIST_HEAD(hash + i);
	return 0;
}
__initcall(vcache_init);

