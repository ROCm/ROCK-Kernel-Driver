/*
 * hvconsole.c
 * Copyright (C) 2004 Hollis Blanchard, IBM Corporation
 *
 * LPAR console support.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/hvcall.h>
#include <asm/prom.h>
#include <asm/hvconsole.h>

int hvc_get_chars(int index, char *buf, int count)
{
	unsigned long got;

	if (plpar_hcall(H_GET_TERM_CHAR, index, 0, 0, 0, &got,
		(unsigned long *)buf, (unsigned long *)buf+1) == H_Success) {
		/*
		 * Work around a HV bug where it gives us a null
		 * after every \r.  -- paulus
		 */
		if (got > 0) {
			int i;
			for (i = 1; i < got; ++i) {
				if (buf[i] == 0 && buf[i-1] == '\r') {
					--got;
					if (i < got)
						memmove(&buf[i], &buf[i+1],
							got - i);
				}
			}
		}
		return got;
	}
	return 0;
}

EXPORT_SYMBOL(hvc_get_chars);

int hvc_put_chars(int index, const char *buf, int count)
{
	unsigned long *lbuf = (unsigned long *) buf;
	long ret;

	ret = plpar_hcall_norets(H_PUT_TERM_CHAR, index, count, lbuf[0],
				 lbuf[1]);
	if (ret == H_Success)
		return count;
	if (ret == H_Busy)
		return 0;
	return -1;
}

EXPORT_SYMBOL(hvc_put_chars);

/* return the number of client vterms present */
/* XXX this requires an interface change to handle multiple discontiguous
 * vterms */
int hvc_count(int *start_termno)
{
	struct device_node *vty;
	int num_found = 0;

	/* consider only the first vty node.
	 * we should _always_ be able to find one. */
	vty = of_find_node_by_name(NULL, "vty");
	if (vty && device_is_compatible(vty, "hvterm1")) {
		u32 *termno = (u32 *)get_property(vty, "reg", NULL);

		if (termno && start_termno)
			*start_termno = *termno;
		num_found = 1;
		of_node_put(vty);
	}

	return num_found;
}
