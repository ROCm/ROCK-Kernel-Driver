/*
 *  arch/s390/mm/extable.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/extable.c"
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

static inline unsigned long
search_one_table(const struct exception_table_entry *first,
		 const struct exception_table_entry *last,
		 unsigned long value)
{
        while (first <= last) {
		const struct exception_table_entry *mid;
		long diff;

		mid = (last - first) / 2 + first;
		diff = mid->insn - value;
                if (diff == 0)
                        return mid->fixup;
                else if (diff < 0)
                        first = mid+1;
                else
                        last = mid-1;
        }
        return 0;
}

extern spinlock_t modlist_lock;

unsigned long
search_exception_table(unsigned long addr)
{
	struct list_head *i;
	unsigned long ret = 0;

#ifndef CONFIG_MODULES
        addr &= 0x7fffffff;  /* remove amode bit from address */
	/* There is only the kernel to search.  */
	ret = search_one_table(__start___ex_table, __stop___ex_table-1, addr);
	if (ret) ret = ret | PSW_ADDR_AMODE31;
	return ret;
#else
	unsigned long flags;
        addr &= 0x7fffffff;  /* remove amode bit from address */

	/* The kernel is the last "module" -- no need to treat it special. */
	spin_lock_irqsave(&modlist_lock, flags);
	list_for_each(i, &extables) {
		struct exception_table *ex
			= list_entry(i, struct exception_table, list);
		if (ex->num_entries == 0)
			continue;
		ret = search_one_table(ex->entry,
				       ex->entry + ex->num_entries - 1, addr);
		if (ret) {
			ret = ret | PSW_ADDR_AMODE31;
			break;
		}
	}
	spin_unlock_irqrestore(&modlist_lock, flags);
	return ret;
#endif
}
