/*
 * linux/arch/cris/mm/extable.c
 *
 * $Log: extable.c,v $
 * Revision 1.4  2003/01/09 14:42:52  starvik
 * Merge of Linux 2.5.55
 *
 * Revision 1.3  2002/11/21 07:24:54  starvik
 * Made search_exception_table similar to implementation for other archs
 * (now compiles with CONFIG_MODULES)
 *
 * Revision 1.2  2002/11/18 07:36:55  starvik
 * Removed warning
 *
 * Revision 1.1  2001/12/17 13:59:27  bjornw
 * Initial revision
 *
 * Revision 1.3  2001/09/27 13:52:40  bjornw
 * Harmonize underscore-ness with other parts
 *
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

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
		diff = mid->insn - value;
                if (diff == 0)
                        return mid;
                else if (diff < 0)
                        first = mid+1;
                else
                        last = mid-1;
        }
        return NULL;
}
