/* ------------------------------------------------------------
 * iSeries_vscsi.c
 * (C) Copyright IBM Corporation 1994, 2003
 * Authors: Colin DeVilbiss (devilbis@us.ibm.com)
 *          Santiago Leon (santil@us.ibm.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 * ------------------------------------------------------------
 * iSeries-specific functions of the SCSI host adapter for Virtual I/O devices
 *
 * This driver allows the Linux SCSI peripheral drivers to directly
 * access devices in the hosting partition, either on an iSeries
 * hypervisor system or a converged hypervisor system.
 */

#include <asm/iSeries/vio.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpConfig.h>
#include "ibmvscsi.h"


/* global variables */
extern struct dma_dev *iSeries_pci_dev;
static void ibmvscsi_handle_event(struct HvLpEvent *lpevt);
static int open_event_path(void);
struct ibmvscsi_host_data *single_host_data = NULL; 

/* ------------------------------------------------------------
 * Routines for managing the command/response queue
 */
/* these routines should all be no-ops under iSeries; just succeed and end */

int initialize_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata)
{
	return 0;
}

void release_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata)
{
}

struct VIOSRP_CRQ *crq_queue_next_crq(struct crq_queue *queue)
{
	return NULL;
}

/* ------------------------------------------------------------
 * Routines for direct interpartition interaction
 */
int ibmvscsi_send_crq(struct ibmvscsi_host_data *hostdata, u64 word1, u64 word2)
{
	single_host_data = hostdata;
	return HvCallEvent_signalLpEventFast(
		viopath_hostLp,
		HvLpEvent_Type_VirtualIo,
		viomajorsubtype_scsi,
		HvLpEvent_AckInd_NoAck,
		HvLpEvent_AckType_ImmediateAck,
		viopath_sourceinst(viopath_hostLp),
		viopath_targetinst(viopath_hostLp),
		0,
		VIOVERSION << 16, word1, word2, 0, 0);
}

struct VIOSRPLpEvent {
	struct HvLpEvent lpevt;	// 0x00-0x17
	u32 reserved1;		// 0x18-0x1B; unused
	u16 version;		// 0x1C-0x1D; unused
	u16 subtype_rc;		// 0x1E-0x1F; unused
	struct VIOSRP_CRQ crq;	// 0x20-0x3F
};
static void ibmvscsi_handle_event(struct HvLpEvent *lpevt)
{
	struct VIOSRPLpEvent *evt = (struct VIOSRPLpEvent *)lpevt;

	if(!evt) {
		printk(KERN_ERR "ibmvscsi: received null event\n");
		return;
	}

	if (single_host_data == NULL) {
		printk(KERN_ERR "ibmvscsi: received event, no adapter present\n");
		return;
	}
	
	ibmvscsi_handle_crq(&evt->crq, single_host_data);
}

/* ------------------------------------------------------------
 * Routines for driver initialization
 */
static int open_event_path(void)
{
	int rc;
//	viopath_capability_mask mask = viopath_get_capabilities();

	rc = viopath_open(viopath_hostLp, viomajorsubtype_scsi, 0);
	if (rc < 0) {
		printk("viopath_open failed with rc %d in open_event_path\n", rc);
		goto viopath_open_failed;
	}

	rc = vio_setHandler(viomajorsubtype_scsi, ibmvscsi_handle_event);
	if (rc < 0) {
		printk("vio_setHandler failed with rc %d in open_event_path\n", rc);
		goto vio_setHandler_failed;
	}
	return 1;

vio_setHandler_failed:
	viopath_close(viopath_hostLp, viomajorsubtype_scsi,
		IBMVSCSI_MAX_REQUESTS);
viopath_open_failed:
	return 0;
}

int ibmvscsi_detect(Scsi_Host_Template * host_template)
{
	struct dma_dev *dma_dev;

	if(!open_event_path()) {
		printk("ibmvscsi: couldn't open vio event path\n");
		return 0;
	}
	dma_dev = iSeries_pci_dev;
	if(!dma_dev) {
		printk("ibmvscsi: couldn't find a device to open\n");
		vio_clearHandler(viomajorsubtype_scsi);
		return 0;
	}

	single_host_data = ibmvscsi_probe_generic(dma_dev, NULL);

	return 1;
}

int ibmvscsi_release(struct Scsi_Host *host)
{
	struct ibmvscsi_host_data *hostdata = *(struct ibmvscsi_host_data **)&host->hostdata;
	/* send an SRP_I_LOGOUT */
	printk("ibmvscsi: release called\n");

	ibmvscsi_remove_generic(hostdata);
	single_host_data = NULL;

	vio_clearHandler(viomajorsubtype_scsi);
	viopath_close(viopath_hostLp, viomajorsubtype_scsi,
		IBMVSCSI_MAX_REQUESTS);
	return 0;
}
/* ------------------------------------------------------------
 * Routines to complete Linux SCSI Host support
 */

int ibmvscsi_enable_interrupts(struct dma_dev *dev)
{
	/*we're not disabling interrupt in iSeries*/
	return 0;
}

int ibmvscsi_disable_interrupts(struct dma_dev *dev)
{
	/*we're not disabling interrupt in iSeries*/
	return 0;
}

/**
 * iSeries_vscsi_init: - Init function for module
 *
*/
int __init ibmvscsi_module_init(void)
{
	printk(KERN_DEBUG "Loading iSeries_vscsi module\n");
	inter_module_register("vscsi_ref", THIS_MODULE, NULL);
	return 0;
}

/**
 * iSeries_vscsi_exit: - Exit function for module
 *
*/
void __exit ibmvscsi_module_exit(void)
{
	printk(KERN_DEBUG "Unloading iSeries_vscsi module\n");
	inter_module_unregister("vscsi_ref");
}

