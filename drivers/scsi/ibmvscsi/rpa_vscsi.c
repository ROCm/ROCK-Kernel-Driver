/* ------------------------------------------------------------
 * rpa_vscsi.c
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
 * RPA-specific functions of the SCSI host adapter for Virtual I/O devices
 *
 * This driver allows the Linux SCSI peripheral drivers to directly
 * access devices in the hosting partition, either on an iSeries
 * hypervisor system or a converged hypervisor system.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <asm/vio.h>
#include <asm/pci_dma.h>
#include <asm/hvcall.h>
#include "ibmvscsi.h"

/* data structures */
struct vio_driver;         /* VIO interface driver data */

/* global variables */
irqreturn_t ibmvscsi_handle_event(int irq, void *dev_instance, struct pt_regs *regs);
static struct vio_driver ibmvscsi_driver;


/* ------------------------------------------------------------
 * Routines for managing the command/response queue
 */
/* zero on success, non-zero on failure */
/**
 * initialize_crq_queue: - Initializes and registers CRQ with hypervisor
 * @queue:	crq_queue to initialize and register
 * @host_data:	ibmvscsi_host_data of host
 *
 * Allocates a page for messages, maps it for dma, and registers
 * the crq with the hypervisor.
 * Returns zero on success.
*/
int initialize_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata)
{
	int rc;

	queue->msgs = (struct VIOSRP_CRQ *)get_zeroed_page(GFP_KERNEL);

	if(!queue->msgs)
		goto malloc_failed;
	queue->size = PAGE_SIZE / sizeof(*queue->msgs);

	if((queue->msg_token = dma_map_single(hostdata->dmadev, queue->msgs, queue->size * sizeof(*queue->msgs), PCI_DMA_BIDIRECTIONAL)) == NO_TCE)
		goto map_failed;

	rc = plpar_hcall_norets(H_REG_CRQ, hostdata->dmadev->unit_address, queue->msg_token, PAGE_SIZE);
	if (rc != 0) {
		printk(KERN_WARNING "ibmvscsi: couldn't register crq--rc 0x%x\n", rc);
		goto reg_crq_failed;
	}

	//if(request_irq(hostdata->dmadev->irq, &ibmvscsi_handle_event, SA_INTERRUPT, "ibmvscsi", (void *)hostdata) != 0) {
	if(request_irq(hostdata->dmadev->irq, &ibmvscsi_handle_event, 0, "ibmvscsi", (void *)hostdata) != 0) {
		printk(KERN_ERR "ibmvscsi: couldn't register irq 0x%x\n", hostdata->dmadev->irq);
		goto req_irq_failed;
	}
	
	rc = ibmvscsi_enable_interrupts(hostdata->dmadev);
	if (rc != 0) {
		printk(KERN_ERR "ibmvscsi:  Error %d enabling interrupts!!!\n",rc);
		goto req_irq_failed;
	}


	queue->cur = 0;
	queue->lock = SPIN_LOCK_UNLOCKED;
	return 0;

req_irq_failed:
	plpar_hcall_norets(H_FREE_CRQ, hostdata->dmadev->unit_address);
reg_crq_failed:
	dma_unmap_single(hostdata->dmadev, queue->msg_token, queue->size * sizeof(*queue->msgs), PCI_DMA_BIDIRECTIONAL);
map_failed:
	free_page((unsigned long)queue->msgs);
malloc_failed:
	return -1;
}

/**
 * release_crq_queue: - Deallocates data and unregisters CRQ
 * @queue:	crq_queue to initialize and register
 * @host_data:	ibmvscsi_host_data of host
 *
 * Frees irq, deallocates a page for messages, unmaps dma, and unregisters
 * the crq with the hypervisor.
*/
void release_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata)
{
	free_irq(hostdata->dmadev->irq, (void *)hostdata);
	plpar_hcall_norets(H_FREE_CRQ, hostdata->dmadev->unit_address);
	dma_unmap_single(hostdata->dmadev, queue->msg_token, queue->size * sizeof(*queue->msgs), PCI_DMA_BIDIRECTIONAL);
	free_page((unsigned long)queue->msgs);
}

/**
 * crq_queue_next_crq: - Returns the next entry in message queue
 * @queue:	crq_queue to use
 *
 * Returns pointer to next entry in queue, or NULL if there are no new 
 * entried in the CRQ.
*/
struct VIOSRP_CRQ *crq_queue_next_crq(struct crq_queue *queue)
{
	struct VIOSRP_CRQ *crq;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	crq = &queue->msgs[queue->cur];
	if(crq->valid & 0x80) {
		if(++queue->cur == queue->size)
			queue->cur = 0;
	}
	else
		crq = NULL;
	spin_unlock_irqrestore(&queue->lock, flags);

	return crq;
}


/* ------------------------------------------------------------
 * Routines for direct interpartition interaction
 */
/**
 * ibmvscsi_send_crq: - Sends message in crq to hypervisor
 * @hostdata:	ibmvscsi_host_data of host to send
 * @word1:	First u64 parameter
 * @word2:	Second u64 parameter
 *
 * Returns zero on success, or error returned by plpar_hcall
*/
int ibmvscsi_send_crq(struct ibmvscsi_host_data *hostdata, u64 word1, u64 word2)
{
	return plpar_hcall_norets(H_SEND_CRQ, hostdata->dmadev->unit_address, word1, word2);
}

/**
 * ibmvscsi_handle_event: - Interrupt handler for crq events
 * @irq:	number of irq to handle, not used
 * @dev_instance: ibmvscsi_host_data of host that received interrupt
 * @regs:	pt_regs with registers
 *
 * Disables interrupts and schedules srp_task
 * Always returns IRQ_HANDLED
*/
irqreturn_t ibmvscsi_handle_event(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibmvscsi_host_data *hostdata = (struct ibmvscsi_host_data *)dev_instance;
	ibmvscsi_disable_interrupts(hostdata->dmadev);
	SCHEDULE_BOTTOM_HALF_QUEUE(hostdata->srp_workqueue,&hostdata->srp_task);
	return IRQ_HANDLED;
}


/* ------------------------------------------------------------
 * Routines for driver initialization
 */
/**
 * ibmvscsi_handle_event: - Detects the number of hosts in device
 * @host_template:	Scsi_Host_Template for the driver
 *
 * Registers the driver in the vio infrastructure.
 * Returns number of hosts found.
*/
static atomic_t ibmvscsi_host_count;
int ibmvscsi_detect(Scsi_Host_Template * host_template)
{
	int host_count;
	ibmvscsi_driver.driver_data = (unsigned long)host_template;
	host_count = vio_register_driver(&ibmvscsi_driver);
	atomic_set(&ibmvscsi_host_count, host_count);
	
	return host_count;
}

/* All we do on release (called by the older SCSI infrastructure) is
 * decrement a counter.  When the counter goes to zero, we call
 * vio_unregister_driver, which will actually drive the remove of all
 * the adapters
 */
int ibmvscsi_release(struct Scsi_Host *host)
{
	if (atomic_dec_return(&ibmvscsi_host_count) == 0) {
		vio_unregister_driver(&ibmvscsi_driver);
	}


	return 0;
}

int ibmvscsi_enable_interrupts(struct dma_dev *dev)
{
	return vio_enable_interrupts(dev);
}

int ibmvscsi_disable_interrupts(struct dma_dev *dev)
{
	return vio_disable_interrupts(dev);
}

/* ------------------------------------------------------------
 * Routines to complete Linux SCSI Host support
 */
/* ------------------------------------------------------------
 * VIO interface support
 */
static struct vio_device_id ibmvscsi_device_table[] __devinitdata = {
    { "scsi-3", "IBM,v-scsi" }, /* Note: This entry can go away when all the firmware is up to date */ 
    { "vscsi",  "IBM,v-scsi" },
    { 0,}
};
                                                                                
static int ibmvscsi_probe(struct dma_dev *dev, const dma_device_id *id) 
{
	struct ibmvscsi_host_data *hostdata;
	hostdata = ibmvscsi_probe_generic(dev,id);
	if (hostdata) {
		dev->driver_data = hostdata;
		return 0;
	} else {
		return -1;
	}
}

static int ibmvscsi_remove(struct dma_dev *dev)
{
	struct ibmvscsi_host_data *hostdata = (struct ibmvscsi_host_data *)dev->driver_data;
	return ibmvscsi_remove_generic(hostdata);
	
}
MODULE_DEVICE_TABLE(vio, ibmvscsi_device_table);
    
char ibmvscsi_driver_name[] = "ibmvscsi";                                                                            
static struct vio_driver ibmvscsi_driver = {
	.name		= ibmvscsi_driver_name,
	.id_table	= ibmvscsi_device_table,
	.probe		= ibmvscsi_probe,
	.remove		= ibmvscsi_remove
};

int __init ibmvscsi_module_init(void)
{
	return vio_register_driver(&ibmvscsi_driver);
}

void __exit ibmvscsi_module_exit(void)
{
	vio_unregister_driver(&ibmvscsi_driver);
}	

