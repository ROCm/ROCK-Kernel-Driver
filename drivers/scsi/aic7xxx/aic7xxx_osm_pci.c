/*
 * Linux driver attachment glue for PCI based controllers.
 *
 * Copyright (c) 2000-2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id$
 */

#include "aic7xxx_osm.h"

/*
 * Include aiclib_pci.c as part of our
 * "module dependencies are hard" work around.
 */
#include "aiclib_pci.c"

static int	ahc_pci_module_registered;

static int	ahc_linux_pci_dev_probe(struct pci_dev *pdev,
					const struct pci_device_id *ent);
static int	ahc_linux_pci_reserve_io_region(struct ahc_softc *ahc,
						u_long *base);
static int	ahc_linux_pci_reserve_mem_region(struct ahc_softc *ahc,
						 u_long *bus_addr,
						 uint8_t **maddr);
static void	ahc_linux_pci_dev_remove(struct pci_dev *pdev);

/* We do our own ID filtering.  So, grab all SCSI storage class devices. */
static struct pci_device_id ahc_linux_pci_id_table[] = {
	{
		0x9004, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_STORAGE_SCSI << 8, 0xFFFF00, 0
	},
	{
		0x9005, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_STORAGE_SCSI << 8, 0xFFFF00, 0
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, ahc_linux_pci_id_table);

struct pci_driver aic7xxx_pci_driver = {
	.name		= "aic7xxx",
	.probe		= ahc_linux_pci_dev_probe,
	.remove		= ahc_linux_pci_dev_remove,
	.id_table	= ahc_linux_pci_id_table
};

static void
ahc_linux_pci_dev_remove(struct pci_dev *pdev)
{
	struct ahc_softc *ahc;
	u_long l;

	/*
	 * We should be able to just perform
	 * the free directly, but check our
	 * list for extra sanity.
	 */
	ahc_list_lock(&l);
	ahc = ahc_find_softc((struct ahc_softc *)pci_get_drvdata(pdev));
	if (ahc != NULL) {
		u_long s;

		TAILQ_REMOVE(&ahc_tailq, ahc, links);
		ahc_list_unlock(&l);
		ahc_lock(ahc, &s);
		ahc_intr_enable(ahc, FALSE);
		ahc_unlock(ahc, &s);
		ahc_free(ahc);
	} else
		ahc_list_unlock(&l);
}

static int
ahc_linux_pci_dev_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	char		 buf[80];
	bus_addr_t	 mask_39bit;
	struct		 ahc_softc *ahc;
	aic_dev_softc_t	 dev;
	struct		 ahc_pci_identity *entry;
	char		*name;
	int		 error;

	/*
	 * Some BIOSen report the same device multiple times.
	 */
	TAILQ_FOREACH(ahc, &ahc_tailq, links) {
		struct pci_dev *probed_pdev;

		probed_pdev = aic_dev_to_pci_dev(ahc->dev_softc);
		if (probed_pdev->bus->number == pdev->bus->number
		 && probed_pdev->devfn == pdev->devfn)
			break;
	}
	if (ahc != NULL) {
		/* Skip duplicate. */
		return (-ENODEV);
	}

	dev = aic_pci_dev_to_dev(pdev);
	entry = ahc_find_pci_device(dev);
	if (entry == NULL)
		return (-ENODEV);

	/*
	 * Allocate a softc for this card and
	 * set it up for attachment by our
	 * common detect routine.
	 */
	sprintf(buf, "ahc_pci:%d:%d:%d",
		aic_get_pci_bus(dev),
		aic_get_pci_slot(dev),
		aic_get_pci_function(dev));
	name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		return (-ENOMEM);
	strcpy(name, buf);
	ahc = ahc_alloc(NULL, name);
	if (ahc == NULL)
		return (-ENOMEM);
	ahc->dev_softc = dev;

	if (pci_enable_device(pdev)) {
		ahc_free(ahc);
		return (-ENODEV);
	}
	pci_set_master(pdev);

	if (aic_set_consistent_dma_mask(ahc, 0xFFFFFFFF) != 0) {
		printk(KERN_WARNING "aic7xxx: Unable to set"
		       "coherent DMA mask.\n");
		ahc_free(ahc);
		return (-ENOMEM);
	}

	mask_39bit = (bus_addr_t)0x7FFFFFFFFFULL;
	if (sizeof(bus_addr_t) > 4
	 && ahc_linux_get_memsize() > 0x80000000
	 && aic_set_dma_mask(ahc, mask_39bit) == 0) {
		ahc->flags |= AHC_39BIT_ADDRESSING;
		ahc->platform_data->hw_dma_mask = mask_39bit;
	} else {
		if (aic_set_dma_mask(ahc, 0xFFFFFFFF) != 0) {
			printk(KERN_WARNING "aic7xxx: Unable to set data "
			       "DMA mask.\n");
			ahc_free(ahc);
			return (-ENOMEM);
		}
		ahc->platform_data->hw_dma_mask = 0xFFFFFFFF;
	}
	error = ahc_pci_config(ahc, entry);
	if (error != 0) {
		ahc_free(ahc);
		return (-error);
	}
	pci_set_drvdata(pdev, ahc);
	if (aic7xxx_detect_complete) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
		ahc_linux_register_host(ahc, &aic7xxx_driver_template);
#else
		printf("aic7xxx: ignoring PCI device found after "
		       "initialization\n");
		return (-ENODEV);
#endif
	}
	return (0);
}

int
ahc_linux_pci_init(void)
{
	int error;
	
	error = pci_module_init(&aic7xxx_pci_driver);
	if (error == 0)
		ahc_pci_module_registered = 1;
	return (error);
}

void
ahc_linux_pci_exit(void)
{
	if (ahc_pci_module_registered != 0)
		pci_unregister_driver(&aic7xxx_pci_driver);
}

static int
ahc_linux_pci_reserve_io_region(struct ahc_softc *ahc, u_long *base)
{
	if (aic7xxx_allow_memio == 0)
		return (ENOMEM);

	*base = pci_resource_start(aic_pci_dev(ahc), 0);
	if (*base == 0)
		return (ENOMEM);
	if (request_region(*base, 256, "aic7xxx") == 0)
		return (ENOMEM);
	return (0);
}

static int
ahc_linux_pci_reserve_mem_region(struct ahc_softc *ahc,
				 u_long *bus_addr,
				 uint8_t **maddr)
{
	u_long	start;
	u_long	base_page;
	u_long	base_offset;
	int	error;

	error = 0;
	start = pci_resource_start(aic_pci_dev(ahc), 1);
	base_page = start & PAGE_MASK;
	base_offset = start - base_page;
	if (start != 0) {
		*bus_addr = start;
		if (request_mem_region(start, 0x1000, "aic7xxx") == 0)
			error = ENOMEM;
		if (error == 0) {
			*maddr = ioremap_nocache(base_page, base_offset + 256);
			if (*maddr == NULL) {
				error = ENOMEM;
				release_mem_region(start, 0x1000);
			} else
				*maddr += base_offset;
		}
	} else
		error = ENOMEM;
	return (error);
}

int
ahc_pci_map_registers(struct ahc_softc *ahc)
{
	uint32_t command;
	u_long	 base;
	uint8_t	*maddr;
	int	 error;

	/*
	 * If its allowed, we prefer memory mapped access.
	 */
	command = aic_pci_read_config(ahc->dev_softc, PCIR_COMMAND, 4);
	command &= ~(PCIM_CMD_PORTEN|PCIM_CMD_MEMEN);
	base = 0;
	maddr = NULL;
	error = ahc_linux_pci_reserve_mem_region(ahc, &base, &maddr);
	if (error == 0) {
		ahc->platform_data->mem_busaddr = base;
		ahc->tag = BUS_SPACE_MEMIO;
		ahc->bsh.maddr = maddr;
		aic_pci_write_config(ahc->dev_softc, PCIR_COMMAND,
				     command | PCIM_CMD_MEMEN, 4);

		/*
		 * Do a quick test to see if memory mapped
		 * I/O is functioning correctly.
		 */
		if (ahc_pci_test_register_access(ahc) != 0) {

			printf("aic7xxx: PCI Device %d:%d:%d "
			       "failed memory mapped test.  Using PIO.\n",
			       aic_get_pci_bus(ahc->dev_softc),
			       aic_get_pci_slot(ahc->dev_softc),
			       aic_get_pci_function(ahc->dev_softc));
			iounmap((void *)((u_long)maddr & PAGE_MASK));
			release_mem_region(ahc->platform_data->mem_busaddr,
					   0x1000);
			ahc->bsh.maddr = NULL;
			maddr = NULL;
		} else
			command |= PCIM_CMD_MEMEN;
	} else {
		printf("aic7xxx: PCI%d:%d:%d MEM region 0x%lx "
		       "unavailable. Cannot memory map device.\n",
		       aic_get_pci_bus(ahc->dev_softc),
		       aic_get_pci_slot(ahc->dev_softc),
		       aic_get_pci_function(ahc->dev_softc),
		       base);
	}

	/*
	 * We always prefer memory mapped access.
	 */
	if (maddr == NULL) {

		error = ahc_linux_pci_reserve_io_region(ahc, &base);
		if (error == 0 && ahc_pci_test_register_access(ahc) == 0) {
			ahc->tag = BUS_SPACE_PIO;
			ahc->bsh.ioport = base;
			command |= PCIM_CMD_PORTEN;
		} else {
			printf("aic7xxx: PCI%d:%d:%d IO region 0x%lx[0..255] "
			       "unavailable. Cannot map device.\n",
			       aic_get_pci_bus(ahc->dev_softc),
			       aic_get_pci_slot(ahc->dev_softc),
			       aic_get_pci_function(ahc->dev_softc),
			       base);
		}
	}
	aic_pci_write_config(ahc->dev_softc, PCIR_COMMAND, command, 4);
	return (error);
}

int
ahc_pci_map_int(struct ahc_softc *ahc)
{
	int error;

	error = request_irq(aic_pci_dev(ahc)->irq, ahc_linux_isr,
			    SA_SHIRQ, "aic7xxx", ahc);
	if (error == 0)
		ahc->platform_data->irq = aic_pci_dev(ahc)->irq;
	
	return (-error);
}
