/*
 * Implementation of Utility functions for PCI controller types.
 *
 * Copyright (c) 2000-2003 Adaptec Inc.
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
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic79xx_osm_pci.c#25 $
 */

void
aic_power_state_change(struct aic_softc *aic, aic_power_state new_state)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	pci_set_power_state(aic_dev_to_pci_dev(aic->dev_softc), new_state);
#else
	uint32_t cap;
	u_int cap_offset;

	/*
	 * Traverse the capability list looking for
	 * the power management capability.
	 */
	cap = 0;
	cap_offset = aic_pci_read_config(aic->dev_softc,
					 PCIR_CAP_PTR, /*bytes*/1);
	while (cap_offset != 0) {

		cap = aic_pci_read_config(aic->dev_softc,
					  cap_offset, /*bytes*/4);
		if ((cap & 0xFF) == 1
		 && ((cap >> 16) & 0x3) > 0) {
			uint32_t pm_control;

			pm_control = aic_pci_read_config(aic->dev_softc,
							 cap_offset + 4,
							 /*bytes*/4);
			pm_control &= ~0x3;
			pm_control |= new_state;
			aic_pci_write_config(aic->dev_softc,
					     cap_offset + 4,
					     pm_control, /*bytes*/2);
			break;
		}
		cap_offset = (cap >> 8) & 0xFF;
	}
#endif 
}
