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
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#else
#include "scsi.h"
#include "hosts.h"
#endif
#include "scsi.h"
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
#define dma_device_id        void
#define SG_TABLESIZE		SG_ALL
#else /* CONFIG_PPC_ISERIES */
#define dma_dev              vio_dev
#define dma_map_single       vio_map_single
#define dma_unmap_single     vio_unmap_single
#define dma_alloc_consistent vio_alloc_consistent
#define dma_free_consistent  vio_free_consistent
#define dma_map_sg           vio_map_sg
#define dma_device_id        struct vio_device_id 
#define SG_TABLESIZE		MAX_INDIRECT_BUFS	
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)

#include "sd.h"
#define irqreturn_t			void

static inline struct Scsi_Host *scsi_host_alloc(Scsi_Host_Template *t, size_t s)
{
	return scsi_register(t, s);
}
static inline void scsi_host_put(struct Scsi_Host *h)
{
	scsi_unregister(h);
}

#define INIT_BOTTOM_HALF(x,y,z) tasklet_init(x, y, (unsigned long)z)
#define SCHEDULE_BOTTOM_HALF(x) tasklet_schedule(x)
#define SCHEDULE_BOTTOM_HALF_QUEUE(q,x) tasklet_schedule(x)
#define KILL_BOTTOM_HALF(x) tasklet_kill(x)
#else
#define INIT_BOTTOM_HALF(x,y,z) INIT_WORK(x, y, (void*)z)
#define SCHEDULE_BOTTOM_HALF(x) schedule_work(x)
#define SCHEDULE_BOTTOM_HALF_QUEUE(q,x) queue_work(q,x)
#define KILL_BOTTOM_HALF(x) cancel_delayed_work(x); flush_scheduled_work()
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

/* routines for managing a command/response queue */
int initialize_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata);
void release_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata);
struct VIOSRP_CRQ *crq_queue_next_crq(struct crq_queue *queue);
int ibmvscsi_detect(Scsi_Host_Template * host_template);
int ibmvscsi_release(struct Scsi_Host *host);

/* routines for direct interaction with the hosting partition */
struct ibmvscsi_host_data *ibmvscsi_probe_generic(struct dma_dev *dev, const dma_device_id *id);
int ibmvscsi_remove_generic(struct ibmvscsi_host_data *hostdata);
int ibmvscsi_send_crq(struct ibmvscsi_host_data *hostdata, u64 word1, u64 word2);
void ibmvscsi_handle_crq(struct VIOSRP_CRQ *crq, struct ibmvscsi_host_data *hostdata);
int ibmvscsi_enable_interrupts(struct dma_dev *dev);
int ibmvscsi_disable_interrupts(struct dma_dev *dev);
int ibmvscsi_module_init(void);
void ibmvscsi_module_exit(void);

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

/* a unit of work for the hosting partition */
struct srp_event_struct {
	union VIOSRP_IU *evt;		/* the actual SRP IU to send */
	Scsi_Cmnd  *cmnd;		/* data to use for callback */
	struct list_head list;		/* queued or sent list for active events*/
	void (*done)(struct srp_event_struct *);	/* run done(this) when it comes back */
	struct VIOSRP_CRQ crq;		/* points to *evt for DMA */
	struct ibmvscsi_host_data *hostdata;
	char in_use;
	/* for the queue case only: */
	struct SRP_CMD cmd;
	void (*cmnd_done)(Scsi_Cmnd*);	/* special _done_ passed with scsi cmd */
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
	atomic_t request_limit;
	struct semaphore waitsem;
	struct dma_dev *dmadev;
	struct event_pool pool;
	struct crq_queue queue;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	struct tasklet_struct srp_task;
	struct tasklet_struct scan_task;
#else
	struct workqueue_struct *srp_workqueue;
	struct work_struct srp_task;
	struct work_struct scan_task;
#endif
	spinlock_t lock; /* lock for queues */
	struct list_head queued;
	struct list_head sent;
	struct Scsi_Host *host;
};


