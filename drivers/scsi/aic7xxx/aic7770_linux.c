/*
 * Linux driver attachment glue for aic7770 based controllers.
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
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic7770_linux.c#9 $
 */

#include "aic7xxx_osm.h"

#define MINSLOT			1
#define NUMSLOTS		16
#define IDOFFSET		0x80

int
aic7770_linux_probe(Scsi_Host_Template *template)
{
#if defined(__i386__) || defined(__alpha__)
	struct aic7770_identity *entry;
	struct ahc_softc *ahc;
	int i, slot;
	int eisaBase;
	int found;

	if (aic7xxx_no_probe)
		return (0);

	eisaBase = 0x1000 + AHC_EISA_SLOT_OFFSET;
	found = 0;
	for (slot = 1; slot < NUMSLOTS; eisaBase+=0x1000, slot++) {
		uint32_t eisa_id;
		size_t	 id_size;

		if (check_region(eisaBase, AHC_EISA_IOSIZE) != 0)
			continue;

		eisa_id = 0;
		id_size = sizeof(eisa_id);
		for (i = 0; i < 4; i++) {
			/* VLcards require priming*/
			outb(0x80 + i, eisaBase + IDOFFSET);
			eisa_id |= inb(eisaBase + IDOFFSET + i)
				   << ((id_size-i-1) * 8);
		}
		if (eisa_id & 0x80000000)
			continue;  /* no EISA card in slot */

		entry = aic7770_find_device(eisa_id);
		if (entry != NULL) {
			char	 buf[80];
			char	*name;
			int	 error;

			/*
			 * Allocate a softc for this card and
			 * set it up for attachment by our
			 * common detect routine.
			 */
			sprintf(buf, "ahc_eisa:%d", slot);
			name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
			if (name == NULL)
				break;
			strcpy(name, buf);
			ahc = ahc_alloc(template, name);
			if (ahc == NULL) {
				/*
				 * If we can't allocate this one,
				 * chances are we won't be able to
				 * allocate future card structures.
				 */
				break;
			}
			ahc->tag = BUS_SPACE_PIO;
			ahc->bsh.ioport = eisaBase;
			error = aic7770_config(ahc, entry);
			if (error != 0) {
				ahc_free(ahc);
				continue;
			}
			found++;
		}
	}
	return (found);
#else
	return (0);
#endif
}

int
aic7770_map_registers(struct ahc_softc *ahc)
{
	/*
	 * Lock out other contenders for our i/o space.
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
	request_region(ahc->bsh.ioport, AHC_EISA_IOSIZE, "aic7xxx");
#else
	if (request_region(ahc->bsh.ioport, AHC_EISA_IOSIZE, "aic7xxx") == 0)
		return (ENOMEM);
#endif

	return (0);
}

int
aic7770_map_int(struct ahc_softc *ahc, u_int irq)
{
	int error;
	int shared;

	shared = 0;
	if ((ahc->flags & AHC_EDGE_INTERRUPT) == 0)
		shared = SA_SHIRQ;

	ahc->platform_data->irq = irq;
	error = request_irq(ahc->platform_data->irq, ahc_linux_isr,
			    shared, "aic7xxx", ahc);
	
	return (-error);
}
