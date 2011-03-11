
#include <linux/device.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/blkdev.h>

#include "blktap.h"

int blktap_ring_major;

 /* 
  * BLKTAP - immediately before the mmap area,
  * we have a bunch of pages reserved for shared memory rings.
  */
#define RING_PAGES 1

static void
blktap_ring_read_response(struct blktap *tap,
		     const struct blkif_response *rsp)
{
	struct blktap_ring *ring = &tap->ring;
	struct blktap_request *request;
	int usr_idx, err;

	request = NULL;

	usr_idx = rsp->id;
	if (usr_idx < 0 || usr_idx >= MAX_PENDING_REQS) {
		err = -ERANGE;
		goto invalid;
	}

	request = ring->pending[usr_idx];

	if (!request) {
		err = -ESRCH;
		goto invalid;
	}

	if (rsp->operation != request->operation) {
		err = -EINVAL;
		goto invalid;
	}

	dev_dbg(ring->dev,
		"request %d [%p] response: %d\n",
		request->usr_idx, request, rsp->status);

	err = rsp->status == BLKIF_RSP_OKAY ? 0 : -EIO;
end_request:
	blktap_device_end_request(tap, request, err);
	return;

invalid:
	dev_warn(ring->dev,
		 "invalid response, idx:%d status:%d op:%d/%d: err %d\n",
		 usr_idx, rsp->status,
		 rsp->operation, request->operation,
		 err);
	if (request)
		goto end_request;
}

static void
blktap_read_ring(struct blktap *tap)
{
	struct blktap_ring *ring = &tap->ring;
	struct blkif_response rsp;
	RING_IDX rc, rp;

	down_read(&current->mm->mmap_sem);
	if (!ring->vma) {
		up_read(&current->mm->mmap_sem);
		return;
	}

	/* for each outstanding message on the ring  */
	rp = ring->ring.sring->rsp_prod;
	rmb();

	for (rc = ring->ring.rsp_cons; rc != rp; rc++) {
		memcpy(&rsp, RING_GET_RESPONSE(&ring->ring, rc), sizeof(rsp));
		blktap_ring_read_response(tap, &rsp);
	}

	ring->ring.rsp_cons = rc;

	up_read(&current->mm->mmap_sem);
}

static int blktap_ring_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

static void
blktap_ring_fail_pending(struct blktap *tap)
{
	struct blktap_ring *ring = &tap->ring;
	struct blktap_request *request;
	int usr_idx;

	for (usr_idx = 0; usr_idx < MAX_PENDING_REQS; usr_idx++) {
		request = ring->pending[usr_idx];
		if (!request)
			continue;

		blktap_device_end_request(tap, request, -EIO);
	}
}

static void
blktap_ring_vm_close(struct vm_area_struct *vma)
{
	struct blktap *tap = vma->vm_private_data;
	struct blktap_ring *ring = &tap->ring;
	struct page *page = virt_to_page(ring->ring.sring);

	blktap_ring_fail_pending(tap);

	zap_page_range(vma, vma->vm_start, PAGE_SIZE, NULL);
	ClearPageReserved(page);
	__free_page(page);

	ring->vma = NULL;

	if (test_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse))
		blktap_control_destroy_tap(tap);
}

static struct vm_operations_struct blktap_ring_vm_operations = {
	.close    = blktap_ring_vm_close,
	.fault    = blktap_ring_fault,
};

int
blktap_ring_map_segment(struct blktap *tap,
			struct blktap_request *request,
			int seg)
{
	struct blktap_ring *ring = &tap->ring;
	unsigned long uaddr;

	uaddr = MMAP_VADDR(ring->user_vstart, request->usr_idx, seg);
	return vm_insert_page(ring->vma, uaddr, request->pages[seg]);
}

int
blktap_ring_map_request(struct blktap *tap,
			struct blktap_request *request)
{
	int seg, err = 0;
	int write;

	write = request->operation != BLKIF_OP_READ;

	for (seg = 0; seg < request->nr_pages; seg++) {
		if (write)
			blktap_request_bounce(tap, request, seg, 1);

		err = blktap_ring_map_segment(tap, request, seg);
		if (err)
			break;
	}

	if (err)
		blktap_ring_unmap_request(tap, request);

	return err;
}

void
blktap_ring_unmap_request(struct blktap *tap,
			  struct blktap_request *request)
{
	struct blktap_ring *ring = &tap->ring;
	unsigned long uaddr;
	unsigned size;
	int seg, read;

	uaddr = MMAP_VADDR(ring->user_vstart, request->usr_idx, 0);
	size  = request->nr_pages << PAGE_SHIFT;
	read  = request->operation != BLKIF_OP_WRITE;

	if (read)
		for (seg = 0; seg < request->nr_pages; seg++)
			blktap_request_bounce(tap, request, seg, 0);

	zap_page_range(ring->vma, uaddr, size, NULL);
}

void
blktap_ring_free_request(struct blktap *tap,
			 struct blktap_request *request)
{
	struct blktap_ring *ring = &tap->ring;

	ring->pending[request->usr_idx] = NULL;
	ring->n_pending--;

	blktap_request_free(tap, request);
}

struct blktap_request*
blktap_ring_make_request(struct blktap *tap)
{
	struct blktap_ring *ring = &tap->ring;
	struct blktap_request *request;
	int usr_idx;

	if (RING_FULL(&ring->ring))
		return ERR_PTR(-ENOSPC);

	request = blktap_request_alloc(tap);
	if (!request)
		return ERR_PTR(-ENOMEM);

	for (usr_idx = 0; usr_idx < BLK_RING_SIZE; usr_idx++)
		if (!ring->pending[usr_idx])
			break;

	BUG_ON(usr_idx >= BLK_RING_SIZE);

	request->tap     = tap;
	request->usr_idx = usr_idx;

	ring->pending[usr_idx] = request;
	ring->n_pending++;

	return request;
}

void
blktap_ring_submit_request(struct blktap *tap,
			   struct blktap_request *request)
{
	struct blktap_ring *ring = &tap->ring;
	struct blkif_request *breq;
	struct scatterlist *sg;
	int i, nsecs = 0;

	dev_dbg(ring->dev,
		"request %d [%p] submit\n", request->usr_idx, request);

	breq = RING_GET_REQUEST(&ring->ring, ring->ring.req_prod_pvt);

	breq->id            = request->usr_idx;
	breq->sector_number = blk_rq_pos(request->rq);
	breq->handle        = 0;
	breq->operation     = request->operation;
	breq->nr_segments   = request->nr_pages;

	blktap_for_each_sg(sg, request, i) {
		struct blkif_request_segment *seg = &breq->seg[i];
		int first, count;

		count = sg->length >> 9;
		first = sg->offset >> 9;

		seg->first_sect = first;
		seg->last_sect  = first + count - 1;

		nsecs += count;
	}

	ring->ring.req_prod_pvt++;

	do_gettimeofday(&request->time);


	switch (request->operation) {
	case BLKIF_OP_WRITE:
		tap->stats.st_wr_sect += nsecs;
		tap->stats.st_wr_req++;
		break;

	case BLKIF_OP_READ:
		tap->stats.st_rd_sect += nsecs;
		tap->stats.st_rd_req++;
		break;

	case BLKIF_OP_PACKET:
		tap->stats.st_pk_req++;
		break;
	}
}

static int
blktap_ring_open(struct inode *inode, struct file *filp)
{
	struct blktap *tap = NULL;
	int minor;

	minor = iminor(inode);

	if (minor < blktap_max_minor)
		tap = blktaps[minor];

	if (!tap)
		return -ENXIO;

	if (test_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse))
		return -ENXIO;

	if (tap->ring.task)
		return -EBUSY;

	filp->private_data = tap;
	tap->ring.task = current;

	return 0;
}

static int
blktap_ring_release(struct inode *inode, struct file *filp)
{
	struct blktap *tap = filp->private_data;

	blktap_device_destroy_sync(tap);

	tap->ring.task = NULL;

	if (test_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse))
		blktap_control_destroy_tap(tap);

	return 0;
}

static int
blktap_ring_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct blktap *tap = filp->private_data;
	struct blktap_ring *ring = &tap->ring;
	struct blkif_sring *sring;
	struct page *page = NULL;
	int err;

	if (ring->vma)
		return -EBUSY;

	page = alloc_page(GFP_KERNEL|__GFP_ZERO);
	if (!page)
		return -ENOMEM;

	SetPageReserved(page);

	err = vm_insert_page(vma, vma->vm_start, page);
	if (err)
		goto fail;

	sring = page_address(page);
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&ring->ring, sring, PAGE_SIZE);

	ring->ring_vstart = vma->vm_start;
	ring->user_vstart = ring->ring_vstart + PAGE_SIZE;

	vma->vm_private_data = tap;

	vma->vm_flags |= VM_DONTCOPY;
	vma->vm_flags |= VM_RESERVED;

	vma->vm_ops = &blktap_ring_vm_operations;

	ring->vma = vma;
	return 0;

fail:
	if (page) {
		zap_page_range(vma, vma->vm_start, PAGE_SIZE, NULL);
		ClearPageReserved(page);
		__free_page(page);
	}

	return err;
}

static long
blktap_ring_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct blktap *tap = filp->private_data;
	struct blktap_ring *ring = &tap->ring;

	BTDBG("%d: cmd: %u, arg: %lu\n", tap->minor, cmd, arg);

	if (!ring->vma || ring->vma->vm_mm != current->mm)
		return -EACCES;

	switch(cmd) {
	case BLKTAP2_IOCTL_KICK_FE:

		blktap_read_ring(tap);
		return 0;

	case BLKTAP2_IOCTL_CREATE_DEVICE: {
		struct blktap_params params;
		void __user *ptr = (void *)arg;

		if (!arg)
			return -EINVAL;

		if (copy_from_user(&params, ptr, sizeof(params)))
			return -EFAULT;

		return blktap_device_create(tap, &params);
	}

	case BLKTAP2_IOCTL_REMOVE_DEVICE:

		return blktap_device_destroy(tap);
	}

	return -ENOIOCTLCMD;
}

static unsigned int blktap_ring_poll(struct file *filp, poll_table *wait)
{
	struct blktap *tap = filp->private_data;
	struct blktap_ring *ring = &tap->ring;
	int work;

	poll_wait(filp, &tap->pool->wait, wait);
	poll_wait(filp, &ring->poll_wait, wait);

	down_read(&current->mm->mmap_sem);
	if (ring->vma && tap->device.gd)
		blktap_device_run_queue(tap);
	up_read(&current->mm->mmap_sem);

	work = ring->ring.req_prod_pvt - ring->ring.sring->req_prod;
	RING_PUSH_REQUESTS(&ring->ring);

	if (work ||
	    ring->ring.sring->private.tapif_user.msg ||
	    test_and_clear_bit(BLKTAP_DEVICE_CLOSED, &tap->dev_inuse))
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations blktap_ring_file_operations = {
	.owner    = THIS_MODULE,
	.open     = blktap_ring_open,
	.release  = blktap_ring_release,
	.unlocked_ioctl = blktap_ring_ioctl,
	.mmap     = blktap_ring_mmap,
	.poll     = blktap_ring_poll,
};

void
blktap_ring_kick_user(struct blktap *tap)
{
	wake_up(&tap->ring.poll_wait);
}

int
blktap_ring_destroy(struct blktap *tap)
{
	struct blktap_ring *ring = &tap->ring;

	if (ring->task || ring->vma)
		return -EBUSY;

	return 0;
}

int
blktap_ring_create(struct blktap *tap)
{
	struct blktap_ring *ring = &tap->ring;

	init_waitqueue_head(&ring->poll_wait);
	ring->devno = MKDEV(blktap_ring_major, tap->minor);

	return 0;
}

size_t
blktap_ring_debug(struct blktap *tap, char *buf, size_t size)
{
	struct blktap_ring *ring = &tap->ring;
	char *s = buf, *end = buf + size;
	int usr_idx;

	s += snprintf(s, end - s,
		      "begin pending:%d\n", ring->n_pending);

	for (usr_idx = 0; usr_idx < MAX_PENDING_REQS; usr_idx++) {
		struct blktap_request *request;
		struct timeval *time;
		char op = '?';

		request = ring->pending[usr_idx];
		if (!request)
			continue;

		switch (request->operation) {
		case BLKIF_OP_WRITE:  op = 'W'; break;
		case BLKIF_OP_READ:   op = 'R'; break;
		case BLKIF_OP_PACKET: op = 'P'; break;
		}
		time  = &request->time;

		s += snprintf(s, end - s,
			      "%02d: usr_idx:%02d "
			      "op:%c nr_pages:%02d time:%lu.%09lu\n",
			      usr_idx, request->usr_idx,
			      op, request->nr_pages,
			      time->tv_sec, time->tv_usec);
	}

	s += snprintf(s, end - s, "end pending\n");

	return s - buf;
}


int __init
blktap_ring_init(void)
{
	int err;

	err = __register_chrdev(0, 0, MAX_BLKTAP_DEVICE, "blktap2",
				&blktap_ring_file_operations);
	if (err < 0) {
		BTERR("error registering ring devices: %d\n", err);
		return err;
	}

	blktap_ring_major = err;
	BTINFO("blktap ring major: %d\n", blktap_ring_major);

	return 0;
}

void
blktap_ring_exit(void)
{
	if (!blktap_ring_major)
		return;

	__unregister_chrdev(blktap_ring_major, 0, MAX_BLKTAP_DEVICE,
			    "blktap2");

	blktap_ring_major = 0;
}
