#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <linux/hdreg.h>
#include <linux/module.h>
#include <linux/version.h>
#include <asm/tlbflush.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>

#include <xen/xenbus.h>
#include <xen/interface/io/blkif.h>

#include "blktap.h"

#include "../blkback/blkback-pagemap.h"

#if 0
#define DPRINTK_IOCTL(_f, _a...) printk(KERN_ALERT _f, ## _a)
#else
#define DPRINTK_IOCTL(_f, _a...) ((void)0)
#endif

struct blktap_grant_table {
	int cnt;
	struct gnttab_map_grant_ref grants[BLKIF_MAX_SEGMENTS_PER_REQUEST * 2];
};

static int blktap_device_major;

static inline struct blktap *
dev_to_blktap(struct blktap_device *dev)
{
	return container_of(dev, struct blktap, device);
}

static int
blktap_device_open(struct block_device *bd, fmode_t mode)
{
	struct blktap *tap;
	struct blktap_device *dev = bd->bd_disk->private_data;

	if (!dev)
		return -ENOENT;

	tap = dev_to_blktap(dev);
	if (!blktap_active(tap) ||
	    test_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse))
		return -ENOENT;

	dev->users++;

	return 0;
}

static int
blktap_device_release(struct gendisk *disk, fmode_t mode)
{
	struct blktap_device *dev = disk->private_data;
	struct blktap *tap = dev_to_blktap(dev);

	dev->users--;
	if (test_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse))
		blktap_device_destroy(tap);

	return 0;
}

static int
blktap_device_getgeo(struct block_device *bd, struct hd_geometry *hg)
{
	/* We don't have real geometry info, but let's at least return
	   values consistent with the size of the device */
	sector_t nsect = get_capacity(bd->bd_disk);
	sector_t cylinders = nsect;

	hg->heads = 0xff;
	hg->sectors = 0x3f;
	sector_div(cylinders, hg->heads * hg->sectors);
	hg->cylinders = cylinders;
	if ((sector_t)(hg->cylinders + 1) * hg->heads * hg->sectors < nsect)
		hg->cylinders = 0xffff;
	return 0;
}

static int
blktap_device_ioctl(struct block_device *bd, fmode_t mode,
		    unsigned command, unsigned long argument)
{
	int i;

	DPRINTK_IOCTL("command: 0x%x, argument: 0x%lx\n",
		      command, (long)argument);

	switch (command) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16)
	case HDIO_GETGEO: {
		struct hd_geometry geo;
		int ret;

                if (!argument)
                        return -EINVAL;

		geo.start = get_start_sect(bd);
		ret = blktap_device_getgeo(bd, &geo);
		if (ret)
			return ret;

		if (copy_to_user((struct hd_geometry __user *)argument, &geo,
				 sizeof(geo)))
                        return -EFAULT;

                return 0;
	}
#endif
	case CDROMMULTISESSION:
		BTDBG("FIXME: support multisession CDs later\n");
		for (i = 0; i < sizeof(struct cdrom_multisession); i++)
			if (put_user(0, (char __user *)(argument + i)))
				return -EFAULT;
		return 0;

	case SCSI_IOCTL_GET_IDLUN:
		if (!access_ok(VERIFY_WRITE, argument, 
			sizeof(struct scsi_idlun)))
			return -EFAULT;

		/* return 0 for now. */
		__put_user(0, &((struct scsi_idlun __user *)argument)->dev_id);
		__put_user(0, 
			&((struct scsi_idlun __user *)argument)->host_unique_id);
		return 0;

	default:
		/*printk(KERN_ALERT "ioctl %08x not supported by Xen blkdev\n",
		  command);*/
		return -EINVAL; /* same return as native Linux */
	}

	return 0;
}

static const struct block_device_operations blktap_device_file_operations = {
	.owner     = THIS_MODULE,
	.open      = blktap_device_open,
	.release   = blktap_device_release,
	.ioctl     = blktap_device_ioctl,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
	.getgeo    = blktap_device_getgeo
#endif
};

static int
blktap_map_uaddr_fn(pte_t *ptep, struct page *pmd_page,
		    unsigned long addr, void *data)
{
	pte_t *pte = (pte_t *)data;

	BTDBG("ptep %p -> %012llx\n", ptep, (unsigned long long)pte_val(*pte));
	set_pte(ptep, *pte);
	return 0;
}

static int
blktap_map_uaddr(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	return apply_to_page_range(vma ? vma->vm_mm : NULL, address,
				   PAGE_SIZE, blktap_map_uaddr_fn, &pte);
}

static int
blktap_umap_uaddr_fn(pte_t *ptep, struct page *pmd_page,
		     unsigned long addr, void *data)
{
	struct vm_area_struct *vma = data;

	BTDBG("ptep %p\n", ptep);
	xen_ptep_get_and_clear_full(vma, addr, ptep, 1);
	return 0;
}

static int
blktap_umap_uaddr(struct vm_area_struct *vma, unsigned long address)
{
	struct mm_struct *mm = NULL;

	if (!vma) {
#ifdef CONFIG_X86
		if (HYPERVISOR_update_va_mapping(address, __pte(0),
						 UVMF_INVLPG|UVMF_ALL))
			BUG();
		return 1;
#endif
	} else
		mm = vma->vm_mm;
	return apply_to_page_range(mm, address,
				   PAGE_SIZE, blktap_umap_uaddr_fn, vma);
}

static inline void
flush_tlb_kernel_page(unsigned long kvaddr)
{
#ifdef CONFIG_X86
	xen_invlpg_all(kvaddr);
#else
	flush_tlb_kernel_range(kvaddr, kvaddr + PAGE_SIZE);
#endif
}

/*
 * tap->tap_sem held on entry
 */
static void
blktap_device_fast_flush(struct blktap *tap, struct blktap_request *request)
{
	uint64_t ptep;
	int ret, usr_idx;
	unsigned int i, cnt;
	struct page **map, *page;
	struct blktap_ring *ring;
	struct grant_handle_pair *khandle;
	unsigned long kvaddr, uvaddr, offset;
	struct gnttab_unmap_grant_ref unmap[BLKIF_MAX_SEGMENTS_PER_REQUEST * 2];
	grant_handle_t self_gref[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	int self_gref_nr = 0;

	cnt     = 0;
	ring    = &tap->ring;
	usr_idx = request->usr_idx;
	map     = ring->foreign_map.map;

	if (!ring->vma)
		return;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		zap_page_range(ring->vma, 
			       MMAP_VADDR(ring->user_vstart, usr_idx, 0),
			       request->nr_pages << PAGE_SHIFT, NULL);

	for (i = 0; i < request->nr_pages; i++) {
		kvaddr = request_to_kaddr(request, i);
		uvaddr = MMAP_VADDR(ring->user_vstart, usr_idx, i);

		khandle = request->handles + i;

		if (khandle->kernel != INVALID_GRANT_HANDLE) {
			gnttab_set_unmap_op(&unmap[cnt], kvaddr,
					    GNTMAP_host_map, khandle->kernel);
			cnt++;
			set_phys_to_machine(__pa(kvaddr) >> PAGE_SHIFT,
					    INVALID_P2M_ENTRY);
		}

		if (khandle->user != INVALID_GRANT_HANDLE) {
			BUG_ON(xen_feature(XENFEAT_auto_translated_physmap));
			if (create_lookup_pte_addr(ring->vma->vm_mm,
						   uvaddr, &ptep) != 0) {
				BTERR("Couldn't get a pte addr!\n");
				return;
			}

			gnttab_set_unmap_op(&unmap[cnt], ptep,
					    GNTMAP_host_map
					    | GNTMAP_application_map
					    | GNTMAP_contains_pte,
					    khandle->user);
			cnt++;
		}

		offset = (uvaddr - ring->vma->vm_start) >> PAGE_SHIFT;

		BTDBG("offset: 0x%08lx, page: %p, request: %p, usr_idx: %d, "
		      "seg: %d, kvaddr: 0x%08lx, khandle: %u, uvaddr: "
		      "0x%08lx, handle: %u\n", offset, map[offset], request,
		      usr_idx, i, kvaddr, khandle->kernel, uvaddr,
		      khandle->user);

		page = map[offset];
		if (page) {
			ClearPageReserved(map[offset]);
			if (PageBlkback(page)) {
				ClearPageBlkback(page);
				set_page_private(page, 0);
			} else if (
				xen_feature(XENFEAT_auto_translated_physmap)) {
				self_gref[self_gref_nr] = khandle->kernel;
				self_gref_nr++;
			}
		}
		map[offset] = NULL;

		khandle->kernel = INVALID_GRANT_HANDLE;
		khandle->user   = INVALID_GRANT_HANDLE;
	}

	if (cnt) {
		ret = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
						unmap, cnt);
		BUG_ON(ret);
	}

	if (!xen_feature(XENFEAT_auto_translated_physmap))
		zap_page_range(ring->vma, 
			       MMAP_VADDR(ring->user_vstart, usr_idx, 0), 
			       request->nr_pages << PAGE_SHIFT, NULL);
	else {
		for (i = 0; i < self_gref_nr; i++) {
			gnttab_end_foreign_access_ref(self_gref[i]);
		}
	}
}

/*
 * tap->tap_sem held on entry
 */
static void
blktap_unmap(struct blktap *tap, struct blktap_request *request)
{
	int i, usr_idx;
	unsigned long kvaddr;

	usr_idx = request->usr_idx;
	down_write(&tap->ring.vma->vm_mm->mmap_sem);

	for (i = 0; i < request->nr_pages; i++) {
		BTDBG("request: %p, seg: %d, kvaddr: 0x%08lx, khandle: %u, "
		      "uvaddr: 0x%08lx, uhandle: %u\n", request, i,
		      request_to_kaddr(request, i),
		      request->handles[i].kernel,
		      MMAP_VADDR(tap->ring.user_vstart, usr_idx, i),
		      request->handles[i].user);

		if (!xen_feature(XENFEAT_auto_translated_physmap) &&
		    request->handles[i].kernel == INVALID_GRANT_HANDLE) {
			kvaddr = request_to_kaddr(request, i);
			if (blktap_umap_uaddr(NULL, kvaddr) == 0)
				flush_tlb_kernel_page(kvaddr);
			set_phys_to_machine(__pa(kvaddr) >> PAGE_SHIFT,
					    INVALID_P2M_ENTRY);
		}
	}

	blktap_device_fast_flush(tap, request);
	up_write(&tap->ring.vma->vm_mm->mmap_sem);
}

/*
 * called if the tapdisk process dies unexpectedly.
 * fail and release any pending requests and disable queue.
 */
void
blktap_device_fail_pending_requests(struct blktap *tap)
{
	int usr_idx;
	struct request *req;
	struct blktap_device *dev;
	struct blktap_request *request;

	if (!test_bit(BLKTAP_DEVICE, &tap->dev_inuse))
		return;

	down_write(&tap->tap_sem);

	dev = &tap->device;
	for (usr_idx = 0; usr_idx < MAX_PENDING_REQS; usr_idx++) {
		request = tap->pending_requests[usr_idx];
		if (!request || request->status != BLKTAP_REQUEST_PENDING)
			continue;

		BTERR("%u:%u: failing pending %s of %d pages\n",
		      blktap_device_major, tap->minor,
		      (request->operation == BLKIF_OP_PACKET ?
		       "packet" : request->operation == BLKIF_OP_READ ?
		       "read" : "write"), request->nr_pages);

		blktap_unmap(tap, request);
		req = (struct request *)(unsigned long)request->id;
		blk_end_request_all(req, -ENODEV);
		blktap_request_free(tap, request);
	}

	up_write(&tap->tap_sem);

	spin_lock_irq(&dev->lock);

	/* fail any future requests */
	dev->gd->queue->queuedata = NULL;
	blk_start_queue(dev->gd->queue);

	spin_unlock_irq(&dev->lock);
}

/*
 * tap->tap_sem held on entry
 */
void
blktap_device_finish_request(struct blktap *tap,
			     blkif_response_t *res,
			     struct blktap_request *request)
{
	struct request *req;

	blktap_unmap(tap, request);

	req = (struct request *)(unsigned long)request->id;

	BTDBG("req %p res status %d operation %d/%d id %lld\n", req,
	      res->status, res->operation, request->operation,
	      (unsigned long long)res->id);

	switch (request->operation) {
	case BLKIF_OP_READ:
	case BLKIF_OP_WRITE:
	case BLKIF_OP_PACKET:
		if (unlikely(res->status != BLKIF_RSP_OKAY))
			BTERR("Bad return from device data "
				"request: %x\n", res->status);
		blk_end_request_all(req,
			res->status == BLKIF_RSP_OKAY ? 0 : -EIO);
		break;
	default:
		BUG();
	}

	blktap_request_free(tap, request);
}

static int
blktap_prep_foreign(struct blktap *tap,
		    struct blktap_request *request,
		    blkif_request_t *blkif_req,
		    unsigned int seg, struct page *page,
		    struct blktap_grant_table *table)
{
	uint64_t ptep;
	uint32_t flags;
	struct page *tap_page;
	struct blktap_ring *ring;
	struct blkback_pagemap map;
	unsigned long uvaddr, kvaddr;

	ring = &tap->ring;
	map  = blkback_pagemap_read(page);
	blkif_req->seg[seg].gref = map.gref;

	uvaddr = MMAP_VADDR(ring->user_vstart, request->usr_idx, seg);
	kvaddr = request_to_kaddr(request, seg);
	flags  = GNTMAP_host_map |
		(request->operation == BLKIF_OP_WRITE ? GNTMAP_readonly : 0);

	gnttab_set_map_op(&table->grants[table->cnt],
			  kvaddr, flags, map.gref, map.domid);
	table->cnt++;

	/* enable chained tap devices */
	tap_page = pfn_to_page(__pa(kvaddr) >> PAGE_SHIFT);
	set_page_private(tap_page, page_private(page));
	SetPageBlkback(tap_page);

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	if (create_lookup_pte_addr(ring->vma->vm_mm, uvaddr, &ptep)) {
		BTERR("couldn't get a pte addr!\n");
		return -1;
	}

	flags |= GNTMAP_application_map | GNTMAP_contains_pte;
	gnttab_set_map_op(&table->grants[table->cnt],
			  ptep, flags, map.gref, map.domid);
	table->cnt++;

	return 0;
}

static int
blktap_map_foreign(struct blktap *tap,
		   struct blktap_request *request,
		   blkif_request_t *blkif_req,
		   struct blktap_grant_table *table)
{
	struct page *page;
	int i, grant, err, usr_idx;
	struct blktap_ring *ring;
	unsigned long uvaddr, kvaddr, foreign_mfn;

	if (!table->cnt)
		return 0;

	err = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref,
					table->grants, table->cnt);
	BUG_ON(err);

	grant   = 0;
	usr_idx = request->usr_idx;
	ring    = &tap->ring;

	for (i = 0; i < request->nr_pages; i++) {
		if (!blkif_req->seg[i].gref)
			continue;

		uvaddr = MMAP_VADDR(ring->user_vstart, usr_idx, i);
		kvaddr = request_to_kaddr(request, i);

		if (unlikely(table->grants[grant].status)) {
			BTERR("invalid kernel buffer: could not remap it\n");
			err |= 1;
			table->grants[grant].handle = INVALID_GRANT_HANDLE;
		}

		request->handles[i].kernel = table->grants[grant].handle;
		foreign_mfn = table->grants[grant].dev_bus_addr >> PAGE_SHIFT;
		grant++;

		if (xen_feature(XENFEAT_auto_translated_physmap))
			goto done;

		if (unlikely(table->grants[grant].status)) {
			BTERR("invalid user buffer: could not remap it\n");
			err |= 1;
			table->grants[grant].handle = INVALID_GRANT_HANDLE;
		}

		request->handles[i].user = table->grants[grant].handle;
		grant++;

	done:
		if (err)
			continue;

		page = pfn_to_page(__pa(kvaddr) >> PAGE_SHIFT);

		if (!xen_feature(XENFEAT_auto_translated_physmap))
			set_phys_to_machine(__pa(kvaddr) >> PAGE_SHIFT,
					    FOREIGN_FRAME(foreign_mfn));
		else if (vm_insert_page(ring->vma, uvaddr, page))
			err |= 1;

		BTDBG("pending_req: %p, seg: %d, page: %p, "
		      "kvaddr: 0x%08lx, khandle: %u, uvaddr: 0x%08lx, "
		      "uhandle: %u\n", request, i, page,
		      kvaddr, request->handles[i].kernel,		       
		      uvaddr, request->handles[i].user);
	}

	return err;
}

static int
blktap_map(struct blktap *tap,
	   struct blktap_request *request,
	   unsigned int seg, struct page *page)
{
	pte_t pte;
	int usr_idx;
	struct blktap_ring *ring;
	unsigned long uvaddr, kvaddr;
	int err = 0;

	ring    = &tap->ring;
	usr_idx = request->usr_idx;
	uvaddr  = MMAP_VADDR(ring->user_vstart, usr_idx, seg);
	kvaddr  = request_to_kaddr(request, seg);

	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		pte = mk_pte(page, ring->vma->vm_page_prot);
		blktap_map_uaddr(ring->vma, uvaddr, pte_mkwrite(pte));
		flush_tlb_page(ring->vma, uvaddr);
		blktap_map_uaddr(NULL, kvaddr, mk_pte(page, PAGE_KERNEL));
		flush_tlb_kernel_page(kvaddr);

		set_phys_to_machine(__pa(kvaddr) >> PAGE_SHIFT, pte_mfn(pte));
		request->handles[seg].kernel = INVALID_GRANT_HANDLE;
	} else {
		/* grant this page access to self domain and map it. */
		domid_t domid = 0; /* XXX my domian id: grant table hypercall
				      doesn't understand DOMID_SELF */
		int gref;
		uint32_t flags;
		struct gnttab_map_grant_ref map;
		struct page *tap_page;

		gref = gnttab_grant_foreign_access(
			domid, page_to_pfn(page),
			(request->operation == BLKIF_OP_WRITE)?
			GTF_readonly: 0);

		flags  = GNTMAP_host_map |
			(request->operation == BLKIF_OP_WRITE ?
			 GNTMAP_readonly : 0);

		gnttab_set_map_op(&map, kvaddr, flags, gref, domid);

		/* enable chained tap devices */
		tap_page = pfn_to_page(__pa(kvaddr) >> PAGE_SHIFT);
		set_page_private(tap_page, page_private(page));
		SetPageBlkback(tap_page);

		err = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref,
						&map, 1);
		BUG_ON(err);

		err = vm_insert_page(ring->vma, uvaddr, tap_page);
		if (err) {
			struct gnttab_unmap_grant_ref unmap;
			gnttab_set_unmap_op(&unmap, kvaddr,
					    GNTMAP_host_map, gref);
			VOID(HYPERVISOR_grant_table_op(
				GNTTABOP_unmap_grant_ref, &unmap, 1));
		} else
			request->handles[seg].kernel = gref;
	}
	request->handles[seg].user = INVALID_GRANT_HANDLE;

	BTDBG("pending_req: %p, seg: %d, page: %p, kvaddr: 0x%08lx, "
	      "uvaddr: 0x%08lx\n", request, seg, page, kvaddr,
	      uvaddr);

	return err;
}

static int
blktap_device_process_request(struct blktap *tap,
			      struct blktap_request *request,
			      struct request *req)
{
	struct page *page;
	int i, usr_idx, err;
	struct blktap_ring *ring;
	struct scatterlist *sg;
	struct blktap_grant_table table;
	unsigned int fsect, lsect, nr_sects;
	unsigned long offset, uvaddr, kvaddr;
	struct blkif_request blkif_req, *target;

	err = -1;
	memset(&table, 0, sizeof(table));

	if (!blktap_active(tap))
		goto out;

	ring    = &tap->ring;
	usr_idx = request->usr_idx;
	blkif_req.id = usr_idx;
	blkif_req.sector_number = (blkif_sector_t)blk_rq_pos(req);
	blkif_req.handle = 0;
	blkif_req.operation = rq_data_dir(req) ?
		BLKIF_OP_WRITE : BLKIF_OP_READ;
	if (unlikely(blk_pc_request(req)))
		blkif_req.operation = BLKIF_OP_PACKET;

	request->id        = (unsigned long)req;
	request->operation = blkif_req.operation;
	request->status    = BLKTAP_REQUEST_PENDING;
	do_gettimeofday(&request->time);

	nr_sects = 0;
	request->nr_pages = 0;
	blkif_req.nr_segments = blk_rq_map_sg(req->q, req, tap->sg);
	BUG_ON(blkif_req.nr_segments > BLKIF_MAX_SEGMENTS_PER_REQUEST);
	for_each_sg(tap->sg, sg, blkif_req.nr_segments, i) {
			fsect = sg->offset >> 9;
			lsect = fsect + (sg->length >> 9) - 1;
			nr_sects += sg->length >> 9;

			blkif_req.seg[i] =
				(struct blkif_request_segment) {
				.gref       = 0,
				.first_sect = fsect,
				.last_sect  = lsect };

			if (PageBlkback(sg_page(sg))) {
				/* foreign page -- use xen */
				if (blktap_prep_foreign(tap,
							request,
							&blkif_req,
							i,
							sg_page(sg),
							&table))
					goto out;
			} else {
				/* do it the old fashioned way */
				if (blktap_map(tap,
					       request,
					       i,
					       sg_page(sg)))
					goto out;
			}

			uvaddr = MMAP_VADDR(ring->user_vstart, usr_idx, i);
			kvaddr = request_to_kaddr(request, i);
			offset = (uvaddr - ring->vma->vm_start) >> PAGE_SHIFT;
			page   = pfn_to_page(__pa(kvaddr) >> PAGE_SHIFT);
			ring->foreign_map.map[offset] = page;
			SetPageReserved(page);

			BTDBG("mapped uaddr %08lx to page %p pfn 0x%lx\n",
			      uvaddr, page, __pa(kvaddr) >> PAGE_SHIFT);
			BTDBG("offset: 0x%08lx, pending_req: %p, seg: %d, "
			      "page: %p, kvaddr: 0x%08lx, uvaddr: 0x%08lx\n",
			      offset, request, i,
			      page, kvaddr, uvaddr);

			request->nr_pages++;
	}

	if (blktap_map_foreign(tap, request, &blkif_req, &table))
		goto out;

	/* Finally, write the request message to the user ring. */
	target = RING_GET_REQUEST(&ring->ring, ring->ring.req_prod_pvt);
	memcpy(target, &blkif_req, sizeof(blkif_req));
	target->id = request->usr_idx;
	wmb(); /* blktap_poll() reads req_prod_pvt asynchronously */
	ring->ring.req_prod_pvt++;

	if (unlikely(blk_pc_request(req)))
		tap->stats.st_pk_req++;
	else if (rq_data_dir(req)) {
		tap->stats.st_wr_sect += nr_sects;
		tap->stats.st_wr_req++;
	} else {
		tap->stats.st_rd_sect += nr_sects;
		tap->stats.st_rd_req++;
	}

	err = 0;

out:
	if (err)
		blktap_device_fast_flush(tap, request);
	return err;
}

#ifdef ENABLE_PASSTHROUGH
#define rq_for_each_bio_safe(_bio, _tmp, _req)				\
	if ((_req)->bio)						\
		for (_bio = (_req)->bio;				\
		     _bio && ((_tmp = _bio->bi_next) || 1);		\
		     _bio = _tmp)

static void
blktap_device_forward_request(struct blktap *tap, struct request *req)
{
	struct bio *bio, *tmp;
	struct blktap_device *dev;

	dev = &tap->device;

	rq_for_each_bio_safe(bio, tmp, req) {
		bio->bi_bdev = dev->bdev;
		submit_bio(bio->bi_rw, bio);
	}
}

static void
blktap_device_close_bdev(struct blktap *tap)
{
	struct blktap_device *dev;

	dev = &tap->device;

	if (dev->bdev)
		blkdev_put(dev->bdev);

	dev->bdev = NULL;
	clear_bit(BLKTAP_PASSTHROUGH, &tap->dev_inuse);
}

static int
blktap_device_open_bdev(struct blktap *tap, u32 pdev)
{
	struct block_device *bdev;
	struct blktap_device *dev;

	dev = &tap->device;

	bdev = open_by_devnum(pdev, FMODE_WRITE);
	if (IS_ERR(bdev)) {
		BTERR("opening device %x:%x failed: %ld\n",
		      MAJOR(pdev), MINOR(pdev), PTR_ERR(bdev));
		return PTR_ERR(bdev);
	}

	if (!bdev->bd_disk) {
		BTERR("device %x:%x doesn't exist\n",
		      MAJOR(pdev), MINOR(pdev));
		blkdev_put(dev->bdev);
		return -ENOENT;
	}

	dev->bdev = bdev;
	set_bit(BLKTAP_PASSTHROUGH, &tap->dev_inuse);

	/* TODO: readjust queue parameters */

	BTINFO("set device %d to passthrough on %x:%x\n",
	       tap->minor, MAJOR(pdev), MINOR(pdev));

	return 0;
}

int
blktap_device_enable_passthrough(struct blktap *tap,
				 unsigned major, unsigned minor)
{
	u32 pdev;
	struct blktap_device *dev;

	dev  = &tap->device;
	pdev = MKDEV(major, minor);

	if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse))
		return -EINVAL;

	if (dev->bdev) {
		if (pdev)
			return -EINVAL;
		blktap_device_close_bdev(tap);
		return 0;
	}

	return blktap_device_open_bdev(tap, pdev);
}
#endif

/*
 * dev->lock held on entry
 */
static void
blktap_device_run_queue(struct blktap *tap)
{
	int queued, err;
	struct request_queue *rq;
	struct request *req;
	struct blktap_ring *ring;
	struct blktap_device *dev;
	struct blktap_request *request;

	queued = 0;
	ring   = &tap->ring;
	dev    = &tap->device;
	rq     = dev->gd->queue;

	BTDBG("running queue for %d\n", tap->minor);

	while ((req = blk_peek_request(rq)) != NULL) {
		if (RING_FULL(&ring->ring)) {
		wait:
			/* Avoid pointless unplugs. */
			blk_stop_queue(rq);
			blktap_defer(tap);
			break;
		}

		blk_start_request(req);

		if (!blk_fs_request(req)) {
			__blk_end_request_all(req, -EIO);
			continue;
		}

		if (blk_barrier_rq(req)) {
			__blk_end_request_all(req, -EOPNOTSUPP);
			continue;
		}

#ifdef ENABLE_PASSTHROUGH
		if (test_bit(BLKTAP_PASSTHROUGH, &tap->dev_inuse)) {
			blktap_device_forward_request(tap, req);
			continue;
		}
#endif

		request = blktap_request_allocate(tap);
		if (!request) {
			tap->stats.st_oo_req++;
			goto wait;
		}

		BTDBG("req %p: dev %d cmd %p, sec 0x%llx, (0x%x/0x%x) "
		      "buffer:%p [%s], pending: %p\n", req, tap->minor,
		      req->cmd, (unsigned long long)blk_rq_pos(req),
		      blk_rq_cur_sectors(req), blk_rq_sectors(req), req->buffer,
		      rq_data_dir(req) ? "write" : "read", request);

		spin_unlock_irq(&dev->lock);
		down_read(&tap->tap_sem);

		err = blktap_device_process_request(tap, request, req);
		if (!err)
			queued++;
		else {
			blk_end_request_all(req, err);
			blktap_request_free(tap, request);
		}

		up_read(&tap->tap_sem);
		spin_lock_irq(&dev->lock);
	}

	if (queued)
		blktap_ring_kick_user(tap);
}

/*
 * dev->lock held on entry
 */
static void
blktap_device_do_request(struct request_queue *rq)
{
	struct request *req;
	struct blktap *tap;
	struct blktap_device *dev;

	dev = rq->queuedata;
	if (!dev)
		goto fail;

	tap = dev_to_blktap(dev);
	if (!blktap_active(tap))
		goto fail;

	if (test_bit(BLKTAP_PAUSED, &tap->dev_inuse) ||
	    test_bit(BLKTAP_PAUSE_REQUESTED, &tap->dev_inuse)) {
		blktap_defer(tap);
		return;
	}

	blktap_device_run_queue(tap);
	return;

fail:
	while ((req = blk_peek_request(rq))) {
		BTERR("device closed: failing secs %llu - %llu\n",
		      (unsigned long long)blk_rq_pos(req),
		      (unsigned long long)blk_rq_pos(req)
		      + blk_rq_cur_sectors(req));
		blk_start_request(req);
		__blk_end_request_all(req, -EIO);
	}
}

void
blktap_device_restart(struct blktap *tap)
{
	struct blktap_device *dev;

	dev = &tap->device;
	if (!dev->gd || !dev->gd->queue)
		return;

	if (blktap_active(tap) && RING_FULL(&tap->ring.ring)) {
		blktap_defer(tap);
		return;
	}

	if (test_bit(BLKTAP_PAUSED, &tap->dev_inuse) ||
	    test_bit(BLKTAP_PAUSE_REQUESTED, &tap->dev_inuse)) {
		blktap_defer(tap);
		return;
	}

	spin_lock_irq(&dev->lock);

	/* Re-enable calldowns. */
	if (blk_queue_stopped(dev->gd->queue))
		blk_start_queue(dev->gd->queue);

	/* Kick things off immediately. */
	blktap_device_do_request(dev->gd->queue);

	spin_unlock_irq(&dev->lock);
}

static void
blktap_device_configure(struct blktap *tap)
{
	struct request_queue *rq;
	struct blktap_device *dev = &tap->device;

	if (!test_bit(BLKTAP_DEVICE, &tap->dev_inuse) || !dev->gd)
		return;

	dev = &tap->device;
	rq  = dev->gd->queue;

	spin_lock_irq(&dev->lock);

	set_capacity(dev->gd, tap->params.capacity);

	/* Hard sector size and max sectors impersonate the equiv. hardware. */
	blk_queue_logical_block_size(rq, tap->params.sector_size);
	blk_queue_max_sectors(rq, 512);

	/* Each segment in a request is up to an aligned page in size. */
	blk_queue_segment_boundary(rq, PAGE_SIZE - 1);
	blk_queue_max_segment_size(rq, PAGE_SIZE);

	/* Ensure a merged request will fit in a single I/O ring slot. */
	blk_queue_max_phys_segments(rq, BLKIF_MAX_SEGMENTS_PER_REQUEST);
	blk_queue_max_hw_segments(rq, BLKIF_MAX_SEGMENTS_PER_REQUEST);

	/* Make sure buffer addresses are sector-aligned. */
	blk_queue_dma_alignment(rq, 511);

	spin_unlock_irq(&dev->lock);
}

int
blktap_device_resume(struct blktap *tap)
{
	int err;

	if (!test_bit(BLKTAP_DEVICE, &tap->dev_inuse) || !blktap_active(tap))
		return -ENODEV;

	if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse))
		return 0;

	err = blktap_ring_resume(tap);
	if (err)
		return err;

	/* device size may have changed */
	blktap_device_configure(tap);

	BTDBG("restarting device\n");
	blktap_device_restart(tap);

	return 0;
}

int
blktap_device_pause(struct blktap *tap)
{
	unsigned long flags;
	struct blktap_device *dev = &tap->device;

	if (!test_bit(BLKTAP_DEVICE, &tap->dev_inuse) || !blktap_active(tap))
		return -ENODEV;

	if (test_bit(BLKTAP_PAUSED, &tap->dev_inuse))
		return 0;

	spin_lock_irqsave(&dev->lock, flags);

	blk_stop_queue(dev->gd->queue);
	set_bit(BLKTAP_PAUSE_REQUESTED, &tap->dev_inuse);

	spin_unlock_irqrestore(&dev->lock, flags);

	return blktap_ring_pause(tap);
}

int
blktap_device_destroy(struct blktap *tap)
{
	struct blktap_device *dev = &tap->device;

	if (!test_bit(BLKTAP_DEVICE, &tap->dev_inuse))
		return 0;

	BTINFO("destroy device %d users %d\n", tap->minor, dev->users);

	if (dev->users)
		return -EBUSY;

	spin_lock_irq(&dev->lock);
	/* No more blktap_device_do_request(). */
	blk_stop_queue(dev->gd->queue);
	clear_bit(BLKTAP_DEVICE, &tap->dev_inuse);
	spin_unlock_irq(&dev->lock);

#ifdef ENABLE_PASSTHROUGH
	if (dev->bdev)
		blktap_device_close_bdev(tap);
#endif

	del_gendisk(dev->gd);
	put_disk(dev->gd);
	blk_cleanup_queue(dev->gd->queue);

	dev->gd = NULL;

	wake_up(&tap->wq);

	return 0;
}

int
blktap_device_create(struct blktap *tap)
{
	int minor, err;
	struct gendisk *gd;
	struct request_queue *rq;
	struct blktap_device *dev;

	gd    = NULL;
	rq    = NULL;
	dev   = &tap->device;
	minor = tap->minor;

	if (test_bit(BLKTAP_DEVICE, &tap->dev_inuse))
		return -EEXIST;

	if (blktap_validate_params(tap, &tap->params))
		return -EINVAL;

	BTINFO("minor %d sectors %Lu sector-size %lu\n",
	       minor, tap->params.capacity, tap->params.sector_size);

	err = -ENODEV;

	gd = alloc_disk(1);
	if (!gd)
		goto error;

	if (minor < 26)
		sprintf(gd->disk_name, "tapdev%c", 'a' + minor);
	else
		sprintf(gd->disk_name, "tapdev%c%c",
			'a' + ((minor / 26) - 1), 'a' + (minor % 26));

	gd->major = blktap_device_major;
	gd->first_minor = minor;
	gd->fops = &blktap_device_file_operations;
	gd->private_data = dev;

	spin_lock_init(&dev->lock);
	rq = blk_init_queue(blktap_device_do_request, &dev->lock);
	if (!rq)
		goto error;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
	elevator_init(rq, "noop");
#else
	elevator_init(rq, &elevator_noop);
#endif

	gd->queue     = rq;
	rq->queuedata = dev;
	dev->gd       = gd;

	set_bit(BLKTAP_DEVICE, &tap->dev_inuse);
	blktap_device_configure(tap);

	add_disk(gd);

	err = 0;
	goto out;

 error:
	if (gd)
		del_gendisk(gd);
	if (rq)
		blk_cleanup_queue(rq);

 out:
	BTINFO("creation of %u:%u: %d\n", blktap_device_major, tap->minor, err);
	return err;
}

int
blktap_device_init(int *maj)
{
	int major;

	/* Dynamically allocate a major for this device */
	major = register_blkdev(0, "tapdev");
	if (major < 0) {
		BTERR("Couldn't register blktap device\n");
		return -ENOMEM;
	}	

	blktap_device_major = *maj = major;
	BTINFO("blktap device major %d\n", major);

	return 0;
}

void
blktap_device_free(void)
{
	if (blktap_device_major)
		unregister_blkdev(blktap_device_major, "tapdev");
}
