/*
 * linux/arch/m68k/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

/* Simple binary search */
const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long value)
{
        while (first <= last) {
		const struct exception_table_entry *mid;
		long diff;

		mid = (last - first) / 2 + first;
		diff = value - mid->insn;
		if (diff >= 0 && diff <= 2)
			return mid;
		else if (diff > 0)
			first = mid+1;
		else
			last = mid-1;
	}
	return NULL;
}

