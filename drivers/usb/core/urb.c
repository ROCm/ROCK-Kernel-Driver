#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/init.h>

#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>
#include "hcd.h"

/**
 * usb_alloc_urb - creates a new urb for a USB driver to use
 * @iso_packets: number of iso packets for this urb
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list of
 *	valid options for this.
 *
 * Creates an urb for the USB driver to use, initializes a few internal
 * structures, incrementes the usage counter, and returns a pointer to it.
 *
 * If no memory is available, NULL is returned.
 *
 * If the driver want to use this urb for interrupt, control, or bulk
 * endpoints, pass '0' as the number of iso packets.
 *
 * The driver must call usb_free_urb() when it is finished with the urb.
 */
struct urb *usb_alloc_urb(int iso_packets, int mem_flags)
{
	struct urb *urb;

	urb = (struct urb *)kmalloc(sizeof(struct urb) + 
		iso_packets * sizeof(struct usb_iso_packet_descriptor),
		mem_flags);
	if (!urb) {
		err("alloc_urb: kmalloc failed");
		return NULL;
	}

	memset(urb, 0, sizeof(*urb));
	urb->count = (atomic_t)ATOMIC_INIT(1);
	spin_lock_init(&urb->lock);

	return urb;
}

/**
 * usb_free_urb - frees the memory used by a urb when all users of it are finished
 * @urb: pointer to the urb to free
 *
 * Must be called when a user of a urb is finished with it.  When the last user
 * of the urb calls this function, the memory of the urb is freed.
 *
 * Note: The transfer buffer associated with the urb is not freed, that must be
 * done elsewhere.
 */
void usb_free_urb(struct urb *urb)
{
	if (urb)
		if (atomic_dec_and_test(&urb->count))
			kfree(urb);
}

/**
 * usb_get_urb - increments the reference count of the urb
 * @urb: pointer to the urb to modify
 *
 * This must be  called whenever a urb is transfered from a device driver to a
 * host controller driver.  This allows proper reference counting to happen
 * for urbs.
 *
 * A pointer to the urb with the incremented reference counter is returned.
 */
struct urb * usb_get_urb(struct urb *urb)
{
	if (urb) {
		atomic_inc(&urb->count);
		return urb;
	} else
		return NULL;
}
		
		
/*-------------------------------------------------------------------*/

/**
 * usb_submit_urb - asynchronously issue a transfer request for an endpoint
 * @urb: pointer to the urb describing the request
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list
 *	of valid options for this.
 *
 * This submits a transfer request, and transfers control of the URB
 * describing that request to the USB subsystem.  Request completion will
 * indicated later, asynchronously, by calling the completion handler.
 * This call may be issued in interrupt context.
 *
 * The caller must have correctly initialized the URB before submitting
 * it.  Functions such as usb_fill_bulk_urb() and usb_fill_control_urb() are
 * available to ensure that most fields are correctly initialized, for
 * the particular kind of transfer, although they will not initialize
 * any transfer flags.
 *
 * Successful submissions return 0; otherwise this routine returns a
 * negative error number.  If the submission is successful, the complete
 * fuction of the urb will be called when the USB host driver is
 * finished with the urb (either a successful transmission, or some
 * error case.)
 *
 * Unreserved Bandwidth Transfers:
 *
 * Bulk or control requests complete only once.  When the completion
 * function is called, control of the URB is returned to the device
 * driver which issued the request.  The completion handler may then
 * immediately free or reuse that URB.
 *
 * Bulk URBs will be queued if the USB_QUEUE_BULK transfer flag is set
 * in the URB.  This can be used to maximize bandwidth utilization by
 * letting the USB controller start work on the next URB without any
 * delay to report completion (scheduling and processing an interrupt)
 * and then submit that next request.
 *
 * For control endpoints, the synchronous usb_control_msg() call is
 * often used (in non-interrupt context) instead of this call.
 *
 * Reserved Bandwidth Transfers:
 *
 * Periodic URBs (interrupt or isochronous) are completed repeatedly,
 * until the original request is aborted.  When the completion callback
 * indicates the URB has been unlinked (with a special status code),
 * control of that URB returns to the device driver.  Otherwise, the
 * completion handler does not control the URB, and should not change
 * any of its fields.
 *
 * Note that isochronous URBs should be submitted in a "ring" data
 * structure (using urb->next) to ensure that they are resubmitted
 * appropriately.
 *
 * If the USB subsystem can't reserve sufficient bandwidth to perform
 * the periodic request, and bandwidth reservation is being done for
 * this controller, submitting such a periodic request will fail.
 *
 * Memory Flags:
 *
 * General rules for how to decide which mem_flags to use:
 * 
 * Basically the rules are the same as for kmalloc.  There are four
 * different possible values; GFP_KERNEL, GFP_NOFS, GFP_NOIO and
 * GFP_ATOMIC.
 *
 * GFP_NOFS is not ever used, as it has not been implemented yet.
 *
 * There are three situations you must use GFP_ATOMIC.
 *    a) you are inside a completion handler, an interrupt, bottom half,
 *       tasklet or timer.
 *    b) you are holding a spinlock or rwlock (does not apply to
 *       semaphores)
 *    c) current->state != TASK_RUNNING, this is the case only after
 *       you've changed it.
 * 
 * GFP_NOIO is used in the block io path and error handling of storage
 * devices.
 *
 * All other situations use GFP_KERNEL.
 *
 * Specfic rules for how to decide which mem_flags to use:
 *
 *    - start_xmit, timeout, and receive methods of network drivers must
 *      use GFP_ATOMIC (spinlock)
 *    - queuecommand methods of scsi drivers must use GFP_ATOMIC (spinlock)
 *    - If you use a kernel thread with a network driver you must use
 *      GFP_NOIO, unless b) or c) apply
 *    - After you have done a down() you use GFP_KERNEL, unless b) or c)
 *      apply or your are in a storage driver's block io path
 *    - probe and disconnect use GFP_KERNEL unless b) or c) apply
 *    - Changing firmware on a running storage or net device uses
 *      GFP_NOIO, unless b) or c) apply
 *
 */
int usb_submit_urb(struct urb *urb, int mem_flags)
{

	if (urb && urb->dev && urb->dev->bus && urb->dev->bus->op) {
		if (usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe)) <= 0) {
			err("%s: pipe %x has invalid size (<= 0)", __FUNCTION__, urb->pipe);
			return -EMSGSIZE;
		}
		return urb->dev->bus->op->submit_urb(urb, mem_flags);
	}
	return -ENODEV;
}

/*-------------------------------------------------------------------*/

/**
 * usb_unlink_urb - abort/cancel a transfer request for an endpoint
 * @urb: pointer to urb describing a previously submitted request
 *
 * This routine cancels an in-progress request.  The requests's
 * completion handler will be called with a status code indicating
 * that the request has been canceled, and that control of the URB
 * has been returned to that device driver.  This is the only way
 * to stop an interrupt transfer, so long as the device is connected.
 *
 * When the USB_ASYNC_UNLINK transfer flag for the URB is clear, this
 * request is synchronous.  Success is indicated by returning zero,
 * at which time the urb will have been unlinked,
 * and the completion function will see status -ENOENT.  Failure is
 * indicated by any other return value.  This mode may not be used
 * when unlinking an urb from an interrupt context, such as a bottom
 * half or a completion handler,
 *
 * When the USB_ASYNC_UNLINK transfer flag for the URB is set, this
 * request is asynchronous.  Success is indicated by returning -EINPROGRESS,
 * at which time the urb will normally not have been unlinked,
 * and the completion function will see status -ECONNRESET.  Failure is
 * indicated by any other return value.
 */
int usb_unlink_urb(struct urb *urb)
{
	if (urb && urb->dev && urb->dev->bus && urb->dev->bus->op)
		return urb->dev->bus->op->unlink_urb(urb);
	else
		return -ENODEV;
}

// asynchronous request completion model
EXPORT_SYMBOL(usb_alloc_urb);
EXPORT_SYMBOL(usb_free_urb);
EXPORT_SYMBOL(usb_get_urb);
EXPORT_SYMBOL(usb_submit_urb);
EXPORT_SYMBOL(usb_unlink_urb);

