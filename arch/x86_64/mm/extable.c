/*
 * linux/arch/x86_64/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/init.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];


void __init exception_table_check(void)
{ 
	const struct exception_table_entry *e;
	unsigned long prev;

	prev = 0;
	for (e = __start___ex_table; e < __stop___ex_table; e++) { 		
		if (e->insn < prev) {
			panic("unordered exception table at %016lx:%016lx and %016lx:%016lx\n",
				   prev, e[-1].fixup, 
			       e->insn, e->fixup);
		}		   	
		prev = e->insn;
	} 
} 

static unsigned long
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
	unsigned long ret = 0;
	unsigned long flags;

#ifndef CONFIG_MODULES
	/* There is only the kernel to search.  */
	return search_one_table(__start___ex_table, __stop___ex_table-1, addr);
#else
	/* The kernel is the last "module" -- no need to treat it special.  */
	struct list_head *i;

	spin_lock_irqsave(&modlist_lock, flags);
	list_for_each(i, &extables) { 
		struct exception_table *ex = 
			list_entry(i,struct exception_table, list); 
		if (ex->num_entries == 0) 
			continue;
		ret = search_one_table(ex->entry,
				       ex->entry + ex->num_entries - 1, addr);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&modlist_lock, flags);
	return ret;
#endif
}
