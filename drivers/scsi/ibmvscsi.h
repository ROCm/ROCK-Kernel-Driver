/* ------------------------------------------------------------
 * ibmvscsi.h
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
 * Emulation of a SCSI host adapter for Virtual I/O devices
 *
 * This driver allows the Linux SCSI peripheral drivers to directly
 * access devices in the hosting partition, either on an iSeries
 * hypervisor system or a converged hypervisor system.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/version.h>   // LINUX_VERSION_CODE and KERNEL_VERSION
#include <linux/string.h>    // memcpy, memset, printk
#include <linux/errno.h>     // -EINVAL, etc
#include <linux/init.h>
#include <linux/module.h>    // module abilities
#include <linux/blkdev.h>       // struct request
#include <linux/interrupt.h> // request_irq and free_irq
#include <../drivers/scsi/scsi.h>
#include <../drivers/scsi/hosts.h>
#include "viosrp.h"

/* ------------------------------------------------------------
 * Platform-specific includes and definitions
 */
#ifdef CONFIG_PPC_ISERIES
#define dma_dev              pci_dev
#define dma_map_single       pci_map_single
#define dma_unmap_single     pci_unmap_single
#define dma_alloc_consistent pci_alloc_consistent
#define dma_free_consistent  pci_free_consistent
#define dma_map_sg           pci_map_sg
#define SG_TABLESIZE		SG_ALL
#else /* CONFIG_PPC_ISERIES */
#define dma_dev              vio_dev
#define dma_map_single       vio_map_single
#define dma_unmap_single     vio_unmap_single
#define dma_alloc_consistent vio_alloc_consistent
#define dma_free_consistent  vio_free_consistent
#define dma_map_sg           vio_map_sg
#define SG_TABLESIZE		MAX_INDIRECT_BUFS	
#endif


/* ------------------------------------------------------------
 * Forward Declarations
 */
/* important constants */
static const struct SRP_CMD *fake_srp_cmd = NULL;
enum {
	IBMVSCSI_MAX_REQUESTS = 50,
	MAX_INDIRECT_BUFS = (sizeof(fake_srp_cmd->additional_data) - sizeof(struct indirect_descriptor)) / sizeof(struct memory_descriptor)
};

/* data structures */
struct crq_queue;          /* an RPA command/response transport queue */
struct ibmvscsi_host_data; /* all driver data associated with a host adapter */
struct event_pool;         /* a pool of event structs for use */

/* global variables */
extern int ibmvscsi_host_count;
extern struct ibmvscsi_host_data *single_host_data;

/* Generic setup routine */
int ibmvscsi_detect(Scsi_Host_Template * host_template);

/* routines for managing a command/response queue */
int initialize_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata);
void release_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata);
struct VIOSRP_CRQ *crq_queue_next_crq(struct crq_queue *queue);


/* routines for direct interaction with the hosting partition */
int ibmvscsi_send_crq(struct ibmvscsi_host_data *hostdata, u64 word1, u64 word2);
void ibmvscsi_handle_crq(struct VIOSRP_CRQ *crq, struct ibmvscsi_host_data *hostdata);
void ibmvscsi_task(unsigned long data);
int ibmvscsi_enable_interrupts(void* dma_dev);
int ibmvscsi_disable_interrupts(void* dma_dev);

/* the rest of the routines for SCSI support */
int add_host(struct dma_dev *dma_dev, Scsi_Host_Template *template); 
void close_path(void);

/* ------------------------------------------------------------
 * Data Structures
 */
/* an RPA command/response transport queue */
struct crq_queue {
	struct VIOSRP_CRQ *msgs;
	int size, cur;
	dma_addr_t msg_token;
	spinlock_t lock;
};

/* a pool of event structs for use */
struct event_pool {
	struct srp_event_struct *events;
	u32 size;
	spinlock_t lock;
	union VIOSRP_IU *iu_storage;
	dma_addr_t iu_token;
};

/* all driver data associated with a host adapter */
struct ibmvscsi_host_data {
	u32 request_limit;
	struct dma_dev *dma_dev;
	struct event_pool pool;
	struct crq_queue queue;
	struct tasklet_struct tasklet;
};


