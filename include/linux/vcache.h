/*
 * virtual => physical mapping cache support.
 */
#ifndef _LINUX_VCACHE_H
#define _LINUX_VCACHE_H

typedef struct vcache_s {
	unsigned long address;
	struct mm_struct *mm;
	struct list_head hash_entry;
	void (*callback)(struct vcache_s *data, struct page *new_page);
} vcache_t;

extern spinlock_t vcache_lock;

extern void __attach_vcache(vcache_t *vcache,
		unsigned long address,
		struct mm_struct *mm,
		void (*callback)(struct vcache_s *data, struct page *new_page));

extern void __detach_vcache(vcache_t *vcache);

extern void invalidate_vcache(unsigned long address, struct mm_struct *mm,
				struct page *new_page);

#endif
