/*
 * Kernel exception handling table support.  Derived from arch/i386/mm/extable.c.
 *
 * Copyright (C) 2000 Hewlett-Packard Co
 * Copyright (C) 2000 John Marvin (jsm@fc.hp.com)
 */

#include <asm/uaccess.h>

const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long addr)
{
	/* Abort early if the search value is out of range.  */

	if ((addr < first->addr) || (addr > last->addr))
		return 0;

        while (first <= last) {
		const struct exception_table_entry *mid;
		long diff;

		mid = first + ((last - first)/2);
		diff = mid->addr - addr;

                if (diff == 0)
                        return mid;
                else if (diff < 0)
                        first = mid+1;
                else
                        last = mid-1;
        }

        return 0;
}

