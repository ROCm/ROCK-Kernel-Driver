/*
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
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (c) 2009 Isaku Yamahata
 *                    VA Linux Systems Japan K.K.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>

#include <asm/setup.h>

static char pci_reserve_param[COMMAND_LINE_SIZE];

/* pci_reserve=	[PCI]
 * Format: [<sbdf>[+IO<size>][+MEM<size>]][,<sbdf>...]
 * Format of sbdf: [<segment>:]<bus>:<dev>.<func>
 */
static int pci_reserve_parse_size(const char *str,
				  unsigned long *io_size,
				  unsigned long *mem_size)
{
	if (sscanf(str, "io%lx", io_size) == 1 ||
	    sscanf(str, "IO%lx", io_size) == 1)
		return 0;

	if (sscanf(str, "mem%lx", mem_size) == 1 ||
	    sscanf(str, "MEM%lx", mem_size) == 1)
		return 0;

	return -EINVAL;
}

static int pci_reserve_parse_one(const char *str,
				 int *seg, int *bus, int *dev, int *func,
				 unsigned long *io_size,
				 unsigned long *mem_size)
{
	char *p;

	*io_size = 0;
	*mem_size = 0;

	if (sscanf(str, "%x:%x:%x.%x", seg, bus, dev, func) != 4) {
		*seg = 0;
		if (sscanf(str, "%x:%x.%x", bus, dev, func) != 3) {
			return -EINVAL;
		}
	}

	p = strchr(str, '+');
	if (p == NULL)
		return -EINVAL;
	if (pci_reserve_parse_size(++p, io_size, mem_size))
		return -EINVAL;

	p = strchr(p, '+');
	return p ? pci_reserve_parse_size(p + 1, io_size, mem_size) : 0;
}

static unsigned long pci_reserve_size(struct pci_bus *pbus, int flags)
{
	char *sp;
	char *ep;

	int seg;
	int bus;
	int dev;
	int func;

	unsigned long io_size;
	unsigned long mem_size;

	sp = pci_reserve_param;

	do {
		ep = strchr(sp, ',');
		if (ep)
			*ep = '\0';	/* chomp */

		if (pci_reserve_parse_one(sp, &seg, &bus, &dev, &func,
					  &io_size, &mem_size) == 0) {
			if (pci_domain_nr(pbus) == seg &&
			    pbus->number == bus &&
			    PCI_SLOT(pbus->self->devfn) == dev &&
			    PCI_FUNC(pbus->self->devfn) == func) {
				switch (flags) {
				case IORESOURCE_IO:
					return io_size;
				case IORESOURCE_MEM:
					return mem_size;
				default:
					break;
				}
			}
		}

		if (ep) {
			*ep = ',';	/* restore chomp'ed ',' for later */
			ep++;
		}
		sp = ep;
	} while (ep);

	return 0;
}

unsigned long pci_reserve_size_io(struct pci_bus *pbus)
{
	return pci_reserve_size(pbus, IORESOURCE_IO);
}

unsigned long pci_reserve_size_mem(struct pci_bus *pbus)
{
	return pci_reserve_size(pbus, IORESOURCE_MEM);
}

static int __init pci_reserve_setup(char *str)
{
	if (strlen(str) >= sizeof(pci_reserve_param))
		return 0;
	strlcpy(pci_reserve_param, str, sizeof(pci_reserve_param));
	return 1;
}
__setup("pci_reserve=", pci_reserve_setup);
