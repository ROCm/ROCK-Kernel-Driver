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
#include <asm/hvcall.h>
#include <asm/prom.h>
#include <asm/hvconsole.h>

struct vtty_struct {
	u32 vtermno;
	int (*get_chars)(struct vtty_struct *vtty, char *buf, int count);
	int (*put_chars)(struct vtty_struct *vtty, const char *buf, int count);
	int (*ioctl)(struct vtty_struct *vtty, unsigned int cmd, unsigned long val);
};
static struct vtty_struct vttys[MAX_NR_HVC_CONSOLES];

int hvterm_get_chars(struct vtty_struct *vtty, char *buf, int count)
{
	unsigned long got;

	if (plpar_hcall(H_GET_TERM_CHAR, vtty->vtermno, 0, 0, 0, &got,
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

int hvterm_put_chars(struct vtty_struct *vtty, const char *buf, int count)
{
	unsigned long *lbuf = (unsigned long *) buf;
	long ret;

	ret = plpar_hcall_norets(H_PUT_TERM_CHAR, vtty->vtermno, count, lbuf[0],
				 lbuf[1]);
	if (ret == H_Success)
		return count;
	if (ret == H_Busy)
		return 0;
	return -1;
}

int hvc_get_chars(int index, char *buf, int count)
{
	struct vtty_struct *vtty = &vttys[index];

	if (index >= MAX_NR_HVC_CONSOLES)
		return -1;

	return vtty->get_chars(vtty, buf, count);
}

int hvc_put_chars(int index, const char *buf, int count)
{
	struct vtty_struct *vtty = &vttys[index];

	if (index >= MAX_NR_HVC_CONSOLES)
		return -1;

	return vtty->put_chars(vtty, buf, count);
}

int hvc_find_vterms(void)
{
	struct device_node *vty;
	int count = 0;

	for (vty = of_find_node_by_name(NULL, "vty"); vty != NULL;
			vty = of_find_node_by_name(vty, "vty")) {
		struct vtty_struct *vtty;
		u32 *vtermno;

		vtermno = (u32 *)get_property(vty, "reg", NULL);
		if (!vtermno)
			continue;

		if (count >= MAX_NR_HVC_CONSOLES)
			break;

		vtty = &vttys[count];
		if (device_is_compatible(vty, "hvterm1")) {
			vtty->vtermno = *vtermno;
			vtty->get_chars = hvterm_get_chars;
			vtty->put_chars = hvterm_put_chars;
			vtty->ioctl = NULL;
			hvc_instantiate();
			count++;
		}
	}

	return count;
}
