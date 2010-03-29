#ifndef PCI_IOMULTI_H
#define PCI_IOMULTI_H
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

struct pci_iomul_setup {
	uint16_t	segment;
	uint8_t		bus;
	uint8_t		dev;
	uint8_t		func;
};

struct pci_iomul_in {
	uint8_t		bar;
	uint64_t	offset;

	uint8_t		size;
	uint32_t	value;
};

struct pci_iomul_out {
	uint8_t		bar;
	uint64_t	offset;

	uint8_t		size;
	uint32_t	value;
};

#define PCI_IOMUL_SETUP		_IOW ('P', 0, struct pci_iomul_setup)
#define PCI_IOMUL_DISABLE_IO	_IO  ('P', 1)
#define PCI_IOMUL_IN		_IOWR('P', 2, struct pci_iomul_in)
#define PCI_IOMUL_OUT		_IOW ('P', 3, struct pci_iomul_out)

#endif /* PCI_IOMULTI_H */
