/*
 * Copyright (c) 2008, NEC Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include "pci.h"


#define	REASSIGNDEV_PARAM_MAX	(2048)
#define	TOKEN_MAX	(12)	/* "SSSS:BB:DD.F" length is 12 */

static char param_reassigndev[REASSIGNDEV_PARAM_MAX] = {0};

static int __init reassigndev_setup(char *str)
{
	strncpy(param_reassigndev, str, REASSIGNDEV_PARAM_MAX);
	param_reassigndev[REASSIGNDEV_PARAM_MAX - 1] = '\0';
	return 1;
}
__setup("reassigndev=", reassigndev_setup);

int is_reassigndev(struct pci_dev *dev)
{
	char dev_str[TOKEN_MAX+1];
	int seg, bus, slot, func;
	int len;
	char *p, *next_str;

	p = param_reassigndev;
	for (; p; p = next_str + 1) {
		next_str = strpbrk(p, ",");
		if (next_str) {
			len = next_str - p;
		} else {
			len = strlen(p);
		}
		if (len > 0 && len <= TOKEN_MAX) {
			strncpy(dev_str, p, len);
			*(dev_str + len) = '\0';

			if (sscanf(dev_str, "%x:%x:%x.%x",
				&seg, &bus, &slot, &func) != 4) {
				if (sscanf(dev_str, "%x:%x.%x",
					&bus, &slot, &func) == 3) {
					seg = 0;
				} else {
					/* failed to scan strings */
					seg = -1;
					bus = -1;
				}
			}
			if (seg == pci_domain_nr(dev->bus) &&
			    bus == dev->bus->number &&
			    slot == PCI_SLOT(dev->devfn) &&
			    func == PCI_FUNC(dev->devfn)) {
				/* It's a target device */
				return 1;
			}
		}
		if (!next_str)
			break;
	}

	return 0;
}
