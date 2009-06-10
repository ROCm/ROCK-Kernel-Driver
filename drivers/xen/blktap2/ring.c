#include <linux/module.h>
#include <linux/signal.h>

#include "blktap.h"

static int blktap_ring_major;

static inline struct blktap *
vma_to_blktap(struct vm_area_struct *vma)
{
	struct vm_foreign_map *m = vma->vm_private_data;
	struct blktap_ring *r = container_of(m, struct blktap_ring, foreign_map);
	return container_of(r, struct blktap, ring);
}

 /* 
  * BLKTAP - immediately before the mmap area,
  * we have a bunch of pages reserved for shared memory rings.
  */
#define RING_PAGES 1

static int
blktap_read_ring(struct blktap *tap)
{
	/* This is called to read responses from the ring. */
	int usr_idx;
	RING_IDX rc, rp;
	blkif_response_t res;
	struct blktap_ring *ring;
	struct blktap_request *request;

	down_read(&tap->tap_sem);

	ring = &tap->ring;
	if (!ring->vma) {
		up_read(&tap->tap_sem);
		return 0;
	}

	/* for each outstanding message on the ring  */
	rp = ring->ring.sring->rsp_prod;
	rmb();

	for (rc = ring->ring.rsp_cons; rc != rp; rc++) {
		memcpy(&res, RING_GET_RESPONSE(&ring->ring, rc), sizeof(res));
		mb(); /* rsp_cons read by RING_FULL() in do_block_io_op(). */
		++ring->ring.rsp_cons;

		usr_idx = (int)res.id;
		if (usr_idx >= MAX_PENDING_REQS ||
		    !tap->pending_requests[usr_idx]) {
			BTWARN("Request %d/%d invalid [%x], tapdisk %d%p\n",
			       rc, rp, usr_idx, tap->pid, ring->vma);
			continue;
		}

		request = tap->pending_requests[usr_idx];
		BTDBG("request %p response #%d id %x\n", request, rc, usr_idx);
		blktap_device_finish_request(tap, &res, request);
	}

	up_read(&tap->tap_sem);

	blktap_run_deferred();

	return 0;
}

static int
blktap_ring_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	/*
	 * if the page has not been mapped in by the driver then return
	 * VM_FAULT_SIGBUS to the domain.
	 */

	return VM_FAULT_SIGBUS;
}

static pte_t
blktap_ring_clear_pte(struct vm_area_struct *vma,
		      unsigned long uvaddr,
		      pte_t *ptep, int is_fullmm)
{
	pte_t copy;
	struct blktap *tap;
	unsigned long kvaddr;
	struct page **map, *page;
	struct blktap_ring *ring;
	struct blktap_request *request;
	struct grant_handle_pair *khandle;
	struct gnttab_unmap_grant_ref unmap[2];
	int offset, seg, usr_idx, count = 0;

	tap  = vma_to_blktap(vma);
	ring = &tap->ring;
	map  = ring->foreign_map.map;
	BUG_ON(!map);	/* TODO Should this be changed to if statement? */

	/*
	 * Zap entry if the address is before the start of the grant
	 * mapped region.
	 */
	if (uvaddr < ring->user_vstart)
		return xen_ptep_get_and_clear_full(vma, uvaddr,
						   ptep, is_fullmm);

	offset  = (int)((uvaddr - ring->user_vstart) >> PAGE_SHIFT);
	usr_idx = offset / BLKIF_MAX_SEGMENTS_PER_REQUEST;
	seg     = offset % BLKIF_MAX_SEGMENTS_PER_REQUEST;

	offset  = (int)((uvaddr - vma->vm_start) >> PAGE_SHIFT);
	page    = map[offset];
	if (page) {
		ClearPageReserved(page);
		if (PageBlkback(page)) {
			ClearPageBlkback(page);
			set_page_private(page, 0);
		}
	}
	map[offset] = NULL;

	request = tap->pending_requests[usr_idx];
	kvaddr  = request_to_kaddr(request, seg);
	khandle = request->handles + seg;

	if (khandle->kernel != INVALID_GRANT_HANDLE) {
		gnttab_set_unmap_op(&unmap[count], kvaddr, 
				    GNTMAP_host_map, khandle->kernel);
		count++;

		set_phys_to_machine(__pa(kvaddr) >> PAGE_SHIFT, 
				    INVALID_P2M_ENTRY);
	}


	if (khandle->user != INVALID_GRANT_HANDLE) {
		BUG_ON(xen_feature(XENFEAT_auto_translated_physmap));

		copy = *ptep;
		gnttab_set_unmap_op(&unmap[count], virt_to_machine(ptep), 
				    GNTMAP_host_map 
				    | GNTMAP_application_map 
				    | GNTMAP_contains_pte,
				    khandle->user);
		count++;
	} else
		copy = xen_ptep_get_and_clear_full(vma, uvaddr, ptep,
						   is_fullmm);

	if (count)
		if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
					      unmap, count))
			BUG();

	khandle->kernel = INVALID_GRANT_HANDLE;
	khandle->user   = INVALID_GRANT_HANDLE;

	return copy;
}

static void
blktap_ring_vm_unmap(struct vm_area_struct *vma)
{
	struct blktap *tap = vma_to_blktap(vma);

	down_write(&tap->tap_sem);
	clear_bit(BLKTAP_RING_VMA, &tap->dev_inuse);
	clear_bit(BLKTAP_PAUSED, &tap->dev_inuse);
	clear_bit(BLKTAP_PAUSE_REQUESTED, &tap->dev_inuse);
	up_write(&tap->tap_sem);
}

static void
blktap_ring_vm_close(struct vm_area_struct *vma)
{
	struct blktap *tap = vma_to_blktap(vma);
	struct blktap_ring *ring = &tap->ring;

	blktap_ring_vm_unmap(vma);                 /* fail future requests */
	blktap_device_fail_pending_requests(tap);  /* fail pending requests */
	blktap_device_restart(tap);                /* fail deferred requests */

	down_write(&tap->tap_sem);

	zap_page_range(vma, vma->vm_start, vma->vm_end - vma->vm_start, NULL);

	kfree(ring->foreign_map.map);
	ring->foreign_map.map = NULL;

	/* Free the ring page. */
	ClearPageReserved(virt_to_page(ring->ring.sring));
	free_page((unsigned long)ring->ring.sring);

	BTINFO("unmapping ring %d\n", tap->minor);
	ring->ring.sring = NULL;
	ring->vma = NULL;

	up_write(&tap->tap_sem);

	wake_up(&tap->wq);
}

static struct vm_operations_struct blktap_ring_vm_operations = {
	.close    = blktap_ring_vm_close,
	.unmap    = blktap_ring_vm_unmap,
	.fault    = blktap_ring_fault,
	.zap_pte  = blktap_ring_clear_pte,
};

static int
blktap_ring_open(struct inode *inode, struct file *filp)
{
	int idx;
	struct blktap *tap;

	idx = iminor(inode);
	if (idx < 0 || idx > MAX_BLKTAP_DEVICE || blktaps[idx] == NULL) {
		BTERR("unable to open device blktap%d\n", idx);
		return -ENODEV;
	}

	tap = blktaps[idx];

	BTINFO("opening device blktap%d\n", idx);

	if (!test_bit(BLKTAP_CONTROL, &tap->dev_inuse))
		return -ENODEV;

	/* Only one process can access ring at a time */
	if (test_and_set_bit(BLKTAP_RING_FD, &tap->dev_inuse))
		return -EBUSY;

	filp->private_data = tap;
	BTINFO("opened device %d\n", tap->minor);

	return 0;
}

static int
blktap_ring_release(struct inode *inode, struct file *filp)
{
	struct blktap *tap = filp->private_data;

	BTINFO("freeing device %d\n", tap->minor);
	clear_bit(BLKTAP_RING_FD, &tap->dev_inuse);
	filp->private_data = NULL;
	wake_up(&tap->wq);	
	return 0;
}

/* Note on mmap:
 * We need to map pages to user space in a way that will allow the block
 * subsystem set up direct IO to them.  This couldn't be done before, because
 * there isn't really a sane way to translate a user virtual address down to a 
 * physical address when the page belongs to another domain.
 *
 * My first approach was to map the page in to kernel memory, add an entry
 * for it in the physical frame list (using alloc_lomem_region as in blkback)
 * and then attempt to map that page up to user space.  This is disallowed
 * by xen though, which realizes that we don't really own the machine frame
 * underlying the physical page.
 *
 * The new approach is to provide explicit support for this in xen linux.
 * The VMA now has a flag, VM_FOREIGN, to indicate that it contains pages
 * mapped from other vms.  vma->vm_private_data is set up as a mapping 
 * from pages to actual page structs.  There is a new clause in get_user_pages
 * that does the right thing for this sort of mapping.
 */
static int
blktap_ring_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int size, err;
	struct page **map;
	struct blktap *tap;
	blkif_sring_t *sring;
	struct blktap_ring *ring;

	tap   = filp->private_data;
	ring  = &tap->ring;
	map   = NULL;
	sring = NULL;

	if (!tap || test_and_set_bit(BLKTAP_RING_VMA, &tap->dev_inuse))
		return -ENOMEM;

	size = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	if (size != (MMAP_PAGES + RING_PAGES)) {
		BTERR("you _must_ map exactly %lu pages!\n",
		      MMAP_PAGES + RING_PAGES);
		return -EAGAIN;
	}

	/* Allocate the fe ring. */
	sring = (blkif_sring_t *)get_zeroed_page(GFP_KERNEL);
	if (!sring) {
		BTERR("Couldn't alloc sring.\n");
		goto fail_mem;
	}

	map = kzalloc(size * sizeof(struct page *), GFP_KERNEL);
	if (!map) {
		BTERR("Couldn't alloc VM_FOREIGN map.\n");
		goto fail_mem;
	}

	SetPageReserved(virt_to_page(sring));
    
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&ring->ring, sring, PAGE_SIZE);

	ring->ring_vstart = vma->vm_start;
	ring->user_vstart = ring->ring_vstart + (RING_PAGES << PAGE_SHIFT);

	/* Map the ring pages to the start of the region and reserve it. */
	if (xen_feature(XENFEAT_auto_translated_physmap))
		err = vm_insert_page(vma, vma->vm_start,
				     virt_to_page(ring->ring.sring));
	else
		err = remap_pfn_range(vma, vma->vm_start,
				      __pa(ring->ring.sring) >> PAGE_SHIFT,
				      PAGE_SIZE, vma->vm_page_prot);
	if (err) {
		BTERR("Mapping user ring failed: %d\n", err);
		goto fail;
	}

	/* Mark this VM as containing foreign pages, and set up mappings. */
	ring->foreign_map.map = map;
	vma->vm_private_data = &ring->foreign_map;
	vma->vm_flags |= VM_FOREIGN;
	vma->vm_flags |= VM_DONTCOPY;
	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &blktap_ring_vm_operations;

#ifdef CONFIG_X86
	vma->vm_mm->context.has_foreign_mappings = 1;
#endif

	tap->pid = current->pid;
	BTINFO("blktap: mapping pid is %d\n", tap->pid);

	ring->vma = vma;
	return 0;

 fail:
	/* Clear any active mappings. */
	zap_page_range(vma, vma->vm_start, 
		       vma->vm_end - vma->vm_start, NULL);
	ClearPageReserved(virt_to_page(sring));
 fail_mem:
	free_page((unsigned long)sring);
	kfree(map);

	return -ENOMEM;
}

static inline void
blktap_ring_set_message(struct blktap *tap, int msg)
{
	struct blktap_ring *ring = &tap->ring;

	down_read(&tap->tap_sem);
	if (ring->ring.sring)
		ring->ring.sring->pad[0] = msg;
	up_read(&tap->tap_sem);
}

static int
blktap_ring_ioctl(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
	struct blktap_params params;
	struct blktap *tap = filp->private_data;

	BTDBG("%d: cmd: %u, arg: %lu\n", tap->minor, cmd, arg);

	switch(cmd) {
	case BLKTAP2_IOCTL_KICK_FE:
		/* There are fe messages to process. */
		return blktap_read_ring(tap);

	case BLKTAP2_IOCTL_CREATE_DEVICE:
		if (!arg)
			return -EINVAL;

		if (copy_from_user(&params, (struct blktap_params __user *)arg,
				   sizeof(params))) {
			BTERR("failed to get params\n");
			return -EFAULT;
		}

		if (blktap_validate_params(tap, &params)) {
			BTERR("invalid params\n");
			return -EINVAL;
		}

		tap->params = params;
		return blktap_device_create(tap);

	case BLKTAP2_IOCTL_SET_PARAMS:
		if (!arg)
			return -EINVAL;

		if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse))
			return -EINVAL;

		if (copy_from_user(&params, (struct blktap_params __user *)arg,
				   sizeof(params))) {
			BTERR("failed to get params\n");
			return -EFAULT;
		}

		if (blktap_validate_params(tap, &params)) {
			BTERR("invalid params\n");
			return -EINVAL;
		}

		tap->params = params;
		return 0;

	case BLKTAP2_IOCTL_PAUSE:
		if (!test_bit(BLKTAP_PAUSE_REQUESTED, &tap->dev_inuse))
			return -EINVAL;

		set_bit(BLKTAP_PAUSED, &tap->dev_inuse);
		clear_bit(BLKTAP_PAUSE_REQUESTED, &tap->dev_inuse);

		blktap_ring_set_message(tap, 0);
		wake_up_interruptible(&tap->wq);

		return 0;


	case BLKTAP2_IOCTL_REOPEN:
		if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse))
			return -EINVAL;

		if (!arg)
			return -EINVAL;

		if (copy_to_user((char __user *)arg,
				 tap->params.name,
				 strlen(tap->params.name) + 1))
			return -EFAULT;

		blktap_ring_set_message(tap, 0);
		wake_up_interruptible(&tap->wq);

		return 0;

	case BLKTAP2_IOCTL_RESUME:
		if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse))
			return -EINVAL;

		tap->ring.response = (int)arg;
		if (!tap->ring.response)
			clear_bit(BLKTAP_PAUSED, &tap->dev_inuse);

		blktap_ring_set_message(tap, 0);
		wake_up_interruptible(&tap->wq);

		return 0;
	}

	return -ENOIOCTLCMD;
}

static unsigned int blktap_ring_poll(struct file *filp, poll_table *wait)
{
	struct blktap *tap = filp->private_data;
	struct blktap_ring *ring = &tap->ring;

	poll_wait(filp, &ring->poll_wait, wait);
	if (ring->ring.sring->pad[0] != 0 ||
	    ring->ring.req_prod_pvt != ring->ring.sring->req_prod) {
		RING_PUSH_REQUESTS(&ring->ring);
		return POLLIN | POLLRDNORM;
	}

	return 0;
}

static struct file_operations blktap_ring_file_operations = {
	.owner    = THIS_MODULE,
	.open     = blktap_ring_open,
	.release  = blktap_ring_release,
	.ioctl    = blktap_ring_ioctl,
	.mmap     = blktap_ring_mmap,
	.poll     = blktap_ring_poll,
};

void
blktap_ring_kick_user(struct blktap *tap)
{
	wake_up_interruptible(&tap->ring.poll_wait);
}

int
blktap_ring_resume(struct blktap *tap)
{
	int err;
	struct blktap_ring *ring = &tap->ring;

	if (!blktap_active(tap))
		return -ENODEV;

	if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse))
		return -EINVAL;

	/* set shared flag for resume */
	ring->response = 0;

	blktap_ring_set_message(tap, BLKTAP2_RING_MESSAGE_RESUME);
	blktap_ring_kick_user(tap);

	wait_event_interruptible(tap->wq, ring->response ||
				 !test_bit(BLKTAP_PAUSED, &tap->dev_inuse));

	err = ring->response;
	ring->response = 0;

	BTDBG("err: %d\n", err);

	if (err)
		return err;

	if (test_bit(BLKTAP_PAUSED, &tap->dev_inuse))
		return -EAGAIN;

	return 0;
}

int
blktap_ring_pause(struct blktap *tap)
{
	if (!blktap_active(tap))
		return -ENODEV;

	if (!test_bit(BLKTAP_PAUSE_REQUESTED, &tap->dev_inuse))
		return -EINVAL;

	BTDBG("draining queue\n");
	wait_event_interruptible(tap->wq, !tap->pending_cnt);
	if (tap->pending_cnt)
		return -EAGAIN;

	blktap_ring_set_message(tap, BLKTAP2_RING_MESSAGE_PAUSE);
	blktap_ring_kick_user(tap);

	BTDBG("waiting for tapdisk response\n");
	wait_event_interruptible(tap->wq, test_bit(BLKTAP_PAUSED, &tap->dev_inuse));
	if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse))
		return -EAGAIN;

	return 0;
}

int
blktap_ring_destroy(struct blktap *tap)
{
	if (!test_bit(BLKTAP_RING_FD, &tap->dev_inuse) &&
	    !test_bit(BLKTAP_RING_VMA, &tap->dev_inuse))
		return 0;

	BTDBG("sending tapdisk close message\n");
	blktap_ring_set_message(tap, BLKTAP2_RING_MESSAGE_CLOSE);
	blktap_ring_kick_user(tap);

	return -EAGAIN;
}

static void
blktap_ring_initialize(struct blktap_ring *ring, int minor)
{
	memset(ring, 0, sizeof(*ring));
	init_waitqueue_head(&ring->poll_wait);
	ring->devno = MKDEV(blktap_ring_major, minor);
}

int
blktap_ring_create(struct blktap *tap)
{
	struct blktap_ring *ring = &tap->ring;
	blktap_ring_initialize(ring, tap->minor);
	return blktap_sysfs_create(tap);
}

int
blktap_ring_init(int *major)
{
	int err;

	err = register_chrdev(0, "blktap2", &blktap_ring_file_operations);
	if (err < 0) {
		BTERR("error registering blktap ring device: %d\n", err);
		return err;
	}

	blktap_ring_major = *major = err;
	BTINFO("blktap ring major: %d\n", blktap_ring_major);
	return 0;
}

int
blktap_ring_free(void)
{
	if (blktap_ring_major)
		unregister_chrdev(blktap_ring_major, "blktap2");

	return 0;
}
