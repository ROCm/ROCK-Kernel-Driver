/*
 * Linux driver attachment glue for PCI based controllers.
 *
 * Copyright (c) 2000 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: //depot/src/linux/drivers/scsi/aic7xxx/aic7xxx_linux_pci.c#15 $
 */

#include "aic7xxx_osm.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
struct pci_device_id
{
};
#endif

static int	ahc_linux_pci_dev_probe(struct pci_dev *pdev,
					const struct pci_device_id *ent);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
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

static struct pci_driver aic7xxx_pci_driver = {
	name:		"aic7xxx",
	probe:		ahc_linux_pci_dev_probe,
	remove:		ahc_linux_pci_dev_remove,
	id_table:	ahc_linux_pci_id_table
};

static void
ahc_linux_pci_dev_remove(struct pci_dev *pdev)
{
	struct ahc_softc *ahc;
	struct ahc_softc *list_ahc;

	/*
	 * We should be able to just perform
	 * the free directly, but check our
	 * list for extra sanity.
	 */
	ahc = (struct ahc_softc *)pdev->driver_data;
	TAILQ_FOREACH(list_ahc, &ahc_tailq, links) {
		if (list_ahc == ahc) {
			ahc_free(ahc);
			break;
		}
	}
}
#endif /* !LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0) */

static int
ahc_linux_pci_dev_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	char	 buf[80];
	struct	 ahc_softc *ahc;
	ahc_dev_softc_t pci;
	struct	 ahc_pci_identity *entry;
	char	*name;
	int	 error;

	pci = pdev;
	entry = ahc_find_pci_device(pci);
	if (entry == NULL)
		return (-ENODEV);

	/*
	 * Allocate a softc for this card and
	 * set it up for attachment by our
	 * common detect routine.
	 */
	sprintf(buf, "ahc_pci:%d:%d:%d",
		ahc_get_pci_bus(pci),
		ahc_get_pci_slot(pci),
		ahc_get_pci_function(pci));
	name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		return (-ENOMEM);
	strcpy(name, buf);
	ahc = ahc_alloc(NULL, name);
	if (ahc == NULL)
		return (-ENOMEM);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	if (pci_enable_device(pdev)) {
		ahc_free(ahc);
		return (-ENODEV);
	}
	pci_set_master(pdev);
#endif
	ahc->dev_softc = pci;
	ahc->platform_data->irq = pdev->irq;
	error = ahc_pci_config(ahc, entry);
	if (error != 0) {
		ahc_free(ahc);
		return (-error);
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	pdev->driver_data = ahc;
	if (aic7xxx_detect_complete)
		aic7xxx_register_host(ahc, aic7xxx_driver_template);
#endif
	return (0);
}

int
ahc_linux_pci_probe(Scsi_Host_Template *template)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	return (pci_module_init(&aic7xxx_pci_driver));
#else
	struct pci_dev *pdev;
	u_int class;
	int found;

	/* If we don't have a PCI bus, we can't find any adapters. */
	if (pci_present() == 0)
		return (0);

	found = 0;
	pdev = NULL;
	class = PCI_CLASS_STORAGE_SCSI << 8;
	while ((pdev = pci_find_class(class, pdev)) != NULL) {
		struct ahc_softc *ahc;
		ahc_dev_softc_t pci;
		int error;

		pci = pdev;

		/*
		 * Some BIOSen report the same device multiple times.
		 */
		TAILQ_FOREACH(ahc, &ahc_tailq, links) {
			struct pci_dev *probed_pdev;

			probed_pdev = ahc->dev_softc;
			if (probed_pdev->bus->number == pdev->bus->number
			 && probed_pdev->devfn == pdev->devfn)
				break;
		}
		if (ahc != NULL) {
			/* Skip duplicate. */
			continue;
		}

		error = ahc_linux_pci_dev_probe(pdev, /*pci_devid*/NULL);
		if (error == 0)
			found++;
	}
	return (found);
#endif
}

int
ahc_pci_map_registers(struct ahc_softc *ahc)
{
	uint32_t command;
	u_long	 base;
	u_long	 start;
	u_long	 base_page;
	u_long	 base_offset;
	uint8_t *maddr;

	command = ahc_pci_read_config(ahc->dev_softc, PCIR_COMMAND, 4);
	base = 0;
	maddr = NULL;
#ifdef MMAPIO
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
	start = pci_resource_start(ahc->dev_softc, 1);
	base_page = start & PAGE_MASK;
	base_offset = start - base_page;
#else
	start = ahc_pci_read_config(ahc->dev_softc, PCIR_MAPS+4, 4);
	base_offset = start & PCI_BASE_ADDRESS_MEM_MASK;
	base_page = base_offset & PAGE_MASK;
	base_offset -= base_page;
#endif
	if (start != 0) {
		ahc->platform_data->mem_busaddr = start;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
		if (request_mem_region(start, 0x1000, "aic7xxx") == 0) {
			printf("aic7xxx: PCI%d:%d:%d MEM region 0x%lx "
			       "in use. Cannot map device.\n",
			       ahc_get_pci_bus(ahc->dev_softc),
			       ahc_get_pci_slot(ahc->dev_softc),
			       ahc_get_pci_function(ahc->dev_softc),
			       start);
		} else
#endif
			maddr = ioremap_nocache(base_page, base_offset + 256);
		if (maddr != NULL) {
			ahc->tag = BUS_SPACE_MEMIO;
			ahc->bsh.maddr = maddr + base_offset;
			command |= PCIM_CMD_MEMEN;
			ahc_pci_write_config(ahc->dev_softc, PCIR_COMMAND,
					     command, 4);

			/*
			 * Do a quick test to see if memory mapped
			 * I/O is functioning correctly.
			 */
			if (ahc_inb(ahc, HCNTRL) == 0xFF) {
				printf("aic7xxx: PCI Device %d:%d:%d "
				       "failed memory mapped test\n",
				       ahc_get_pci_bus(ahc->dev_softc),
				       ahc_get_pci_slot(ahc->dev_softc),
				       ahc_get_pci_function(ahc->dev_softc));
				iounmap((void *)base_page);
				maddr = NULL;
			} else {
				command &= ~PCIM_CMD_PORTEN;
				ahc_pci_write_config(ahc->dev_softc,
						    PCIR_COMMAND, command, 4);
			}
		}
	}
#endif

	/*
	 * We always prefer memory mapped access.  Only
	 * complain about our ioport conflicting with
	 * another device if we are going to use it.
	 */
	if (maddr == NULL) {
		ahc->tag = BUS_SPACE_PIO;
		command &= ~(PCIM_CMD_MEMEN|PCIM_CMD_PORTEN);
		ahc_pci_write_config(ahc->dev_softc, PCIR_COMMAND, command, 4);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
		base = pci_resource_start(ahc->dev_softc, 0);
#else
		base = ahc_pci_read_config(ahc->dev_softc, PCIR_MAPS, 4);
		base &= PCI_BASE_ADDRESS_IO_MASK;
#endif
		if (base == 0) {
			printf("aic7xxx: PCI%d:%d:%d No mapping available. "
			       "Cannot map device.\n",
			       ahc_get_pci_bus(ahc->dev_softc),
			       ahc_get_pci_slot(ahc->dev_softc),
			       ahc_get_pci_function(ahc->dev_softc));
			return (ENXIO);
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
		if (check_region(base, 256) != 0) {
#else
		if (request_region(base, 256, "aic7xxx") == 0) {
#endif
			printf("aic7xxx: PCI%d:%d:%d IO region 0x%lx[0..255] "
			       "in use. Cannot map device.\n",
			       ahc_get_pci_bus(ahc->dev_softc),
			       ahc_get_pci_slot(ahc->dev_softc),
			       ahc_get_pci_function(ahc->dev_softc),
			       base);
			base = 0;
			return (EBUSY);
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
		request_region(base, 256, "aic7xxx");
#endif
		ahc->bsh.ioport = base;
		command |= PCIM_CMD_PORTEN;
		ahc_pci_write_config(ahc->dev_softc, PCIR_COMMAND, command, 4);
	}
	return (0);
}

int
ahc_pci_map_int(struct ahc_softc *ahc)
{
	int error;

	ahc->platform_data->irq = ahc->dev_softc->irq;
	error = request_irq(ahc->platform_data->irq, aic7xxx_isr,
			    SA_INTERRUPT|SA_SHIRQ, "aic7xxx", ahc);
	if (error < 0)
		error = request_irq(ahc->platform_data->irq, aic7xxx_isr,
				    SA_SHIRQ, "aic7xxx", ahc);
	
	return (-error);
}
