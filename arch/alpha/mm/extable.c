/*
 * linux/arch/alpha/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

static inline unsigned
search_one_table(const struct exception_table_entry *first,
		 const struct exception_table_entry *last,
		 unsigned long value)
{
        while (first <= last) {
		const struct exception_table_entry *mid;
		unsigned long mid_value;

		mid = (last - first) / 2 + first;
		mid_value = (unsigned long)&mid->insn + mid->insn;
                if (mid_value == value)
                        return mid->fixup.unit;
                else if (mid_value < value)
                        first = mid+1;
                else
                        last = mid-1;
        }
        return 0;
}

unsigned
search_exception_table(unsigned long addr)
{
	unsigned ret;

#ifndef CONFIG_MODULES
	ret = search_one_table(__start___ex_table, __stop___ex_table-1, addr);
#else
	unsigned long flags;
	struct list_head *i;

	ret = 0;
	spin_lock_irqsave(&modlist_lock, flags);
	list_for_each(i, &extables) {
		struct exception_table *ex
			= list_entry(i, struct exception_table, list);
		if (ex->num_entries == 0)
			continue;
		ret = search_one_table(ex->entry,
				       ex->entry + ex->num_entries - 1, addr);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&modlist_lock, flags);
#endif

	return ret;
}
