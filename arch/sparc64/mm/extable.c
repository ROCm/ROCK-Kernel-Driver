/*
 * linux/arch/sparc64/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

static unsigned long
search_one_table(const struct exception_table_entry *start,
		 const struct exception_table_entry *last,
		 unsigned long value, unsigned long *g2)
{
	const struct exception_table_entry *first = start;
	const struct exception_table_entry *mid;
	long diff = 0;
        while (first <= last) {
		mid = (last - first) / 2 + first;
		diff = mid->insn - value;
                if (diff == 0) {
                	if (!mid->fixup) {
                		*g2 = 0;
                		return (mid + 1)->fixup;
                	} else
	                        return mid->fixup;
                } else if (diff < 0)
                        first = mid+1;
                else
                        last = mid-1;
        }
        if (last->insn < value && !last->fixup && last[1].insn > value) {
        	*g2 = (value - last->insn)/4;
        	return last[1].fixup;
        }
        if (first > start && first[-1].insn < value
	    && !first[-1].fixup && first->insn < value) {
        	*g2 = (value - first[-1].insn)/4;
        	return first->fixup;
        }
        return 0;
}

unsigned long
search_exception_table(unsigned long addr, unsigned long *g2)
{
	unsigned long ret;

#ifndef CONFIG_MODULES
	/* There is only the kernel to search.  */
	ret = search_one_table(__start___ex_table,
			       __stop___ex_table-1, addr, g2);
	if (ret) return ret;
#else
	/* The kernel is the last "module" -- no need to treat it special.  */
	struct module *mp;
	for (mp = module_list; mp != NULL; mp = mp->next) {
		if (mp->ex_table_start == NULL)
			continue;
		ret = search_one_table(mp->ex_table_start,
				       mp->ex_table_end-1, addr, g2);
		if (ret) return ret;
	}
#endif

	return 0;
}
