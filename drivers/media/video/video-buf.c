/*
 * generic helper functions for video4linux capture buffers, to handle
 * memory management and PCI DMA.  Right now bttv + saa7134 use it.
 *
 * The functions expect the hardware being able to scatter gatter
 * (i.e. the buffers are not linear in physical memory, but fragmented
 * into PAGE_SIZE chunks).  They also assume the driver does not need
 * to touch the video data (thus it is probably not useful for USB as
 * data often must be uncompressed by the drivers).
 * 
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/iobuf.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include "video-buf.h"

static int debug = 0;

MODULE_DESCRIPTION("helper module to manage video4linux pci dma buffers");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");
MODULE_PARM(debug,"i");

#define dprintk(level, fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "vbuf: " fmt, ## arg)

struct scatterlist*
videobuf_vmalloc_to_sg(unsigned char *virt, int nr_pages)
{
	struct scatterlist *sglist;
	struct page *pg;
	int i;

	sglist = kmalloc(sizeof(struct scatterlist)*nr_pages, GFP_KERNEL);
	if (NULL == sglist)
		return NULL;
	memset(sglist,0,sizeof(struct scatterlist)*nr_pages);
	for (i = 0; i < nr_pages; i++, virt += PAGE_SIZE) {
		pg = vmalloc_to_page(virt);
		if (NULL == pg)
			goto err;
		if (PageHighMem(pg))
			BUG();
		sglist[i].page   = pg;
		sglist[i].length = PAGE_SIZE;
	}
	return sglist;
	
 err:
	kfree(sglist);
	return NULL;
}

struct scatterlist*
videobuf_iobuf_to_sg(struct kiobuf *iobuf)
{
	struct scatterlist *sglist;
	int i = 0;

	sglist = kmalloc(sizeof(struct scatterlist) * iobuf->nr_pages,
			 GFP_KERNEL);
	if (NULL == sglist)
		return NULL;
	memset(sglist,0,sizeof(struct scatterlist) * iobuf->nr_pages);

	if (NULL == iobuf->maplist[0])
		goto err;
	if (PageHighMem(iobuf->maplist[0]))
		/* DMA to highmem pages might not work */
		goto err;
	sglist[0].page   = iobuf->maplist[0];
	sglist[0].offset = iobuf->offset;
	sglist[0].length = PAGE_SIZE - iobuf->offset;
	for (i = 1; i < iobuf->nr_pages; i++) {
		if (NULL == iobuf->maplist[i])
			goto err;
		if (PageHighMem(iobuf->maplist[i]))
			goto err;
		sglist[i].page   = iobuf->maplist[i];
		sglist[i].length = PAGE_SIZE;
	}
	return sglist;

 err:
	kfree(sglist);
	return NULL;
}

/* --------------------------------------------------------------------- */

int videobuf_dma_init_user(struct videobuf_dmabuf *dma, int direction,
			   unsigned long data, unsigned long size)
{
	int err, rw = 0;

	dma->direction = direction;
	switch (dma->direction) {
	case PCI_DMA_FROMDEVICE: rw = READ;  break;
	case PCI_DMA_TODEVICE:   rw = WRITE; break;
	default:                 BUG();
	}
	if (0 != (err = alloc_kiovec(1,&dma->iobuf)))
		return err;
	if (0 != (err = map_user_kiobuf(rw, dma->iobuf, data, size))) {
		dprintk(1,"map_user_kiobuf: %d\n",err);
		return err;
	}
	dma->nr_pages = dma->iobuf->nr_pages;
	return 0;
}

int videobuf_dma_init_kernel(struct videobuf_dmabuf *dma, int direction,
			     int nr_pages)
{
	dma->direction = direction;
	dma->vmalloc = vmalloc_32(nr_pages << PAGE_SHIFT);
	if (NULL == dma->vmalloc) {
		dprintk(1,"vmalloc_32(%d pages) failed\n",nr_pages);
		return -ENOMEM;
	}
	memset(dma->vmalloc,0,nr_pages << PAGE_SHIFT);
	dma->nr_pages = nr_pages;
	return 0;
}

int videobuf_dma_pci_map(struct pci_dev *dev, struct videobuf_dmabuf *dma)
{
	int err;

	if (0 == dma->nr_pages)
		BUG();
	
	if (dma->iobuf) {
		if (0 != (err = lock_kiovec(1,&dma->iobuf,1))) {
			dprintk(1,"lock_kiovec: %d\n",err);
			return err;
		}
		dma->sglist = videobuf_iobuf_to_sg(dma->iobuf);
	}
	if (dma->vmalloc) {
		dma->sglist = videobuf_vmalloc_to_sg
			(dma->vmalloc,dma->nr_pages);
	}
	if (NULL == dma->sglist) {
		dprintk(1,"scatterlist is NULL\n");
		return -ENOMEM;
	}
	dma->sglen = pci_map_sg(dev,dma->sglist,dma->nr_pages,
				 dma->direction);
	return 0;
}

int videobuf_dma_pci_sync(struct pci_dev *dev, struct videobuf_dmabuf *dma)
{
	if (!dma->sglen)
		BUG();

	pci_dma_sync_sg(dev,dma->sglist,dma->nr_pages,dma->direction);
	return 0;
}

int videobuf_dma_pci_unmap(struct pci_dev *dev, struct videobuf_dmabuf *dma)
{
	if (!dma->sglen)
		return 0;

	pci_unmap_sg(dev,dma->sglist,dma->nr_pages,dma->direction);
	kfree(dma->sglist);
	dma->sglist = NULL;
	dma->sglen = 0;
	if (dma->iobuf)
		unlock_kiovec(1,&dma->iobuf);
	return 0;
}

int videobuf_dma_free(struct videobuf_dmabuf *dma)
{
	if (dma->sglen)
		BUG();

	if (dma->iobuf) {
		unmap_kiobuf(dma->iobuf);
		free_kiovec(1,&dma->iobuf);
		dma->iobuf = NULL;
	}
	if (dma->vmalloc) {
		vfree(dma->vmalloc);
		dma->vmalloc = NULL;
	}
	dma->direction = PCI_DMA_NONE;
	return 0;
}

/* --------------------------------------------------------------------- */

void* videobuf_alloc(int size, int type)
{
	struct videobuf_buffer *vb;

	vb = kmalloc(size,GFP_KERNEL);
	if (NULL != vb) {
		memset(vb,0,size);
		vb->type = type;
		init_waitqueue_head(&vb->done);
	}
	return vb;
}

int videobuf_waiton(struct videobuf_buffer *vb, int non_blocking, int intr)
{
	int retval = 0;
	DECLARE_WAITQUEUE(wait, current);
	
	add_wait_queue(&vb->done, &wait);
	while (vb->state == STATE_ACTIVE ||
	       vb->state == STATE_QUEUED) {
		if (non_blocking) {
			retval = -EAGAIN;
			break;
		}
		current->state = intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;
		schedule();
		if (intr && signal_pending(current)) {
			dprintk(1,"buffer waiton: -EINTR\n");
			retval = -EINTR;
			break;
		}
	}
	remove_wait_queue(&vb->done, &wait);
	return retval;
}

int
videobuf_iolock(struct pci_dev *pci, struct videobuf_buffer *vb)
{
	int err,pages;

	if (0 == vb->baddr) {
		/* no userspace addr -- kernel bounce buffer */
		pages = PAGE_ALIGN(vb->size) >> PAGE_SHIFT;
		dprintk(1,"kernel buf size=%ld (%d pages)\n",
			vb->size,pages);
		err = videobuf_dma_init_kernel(&vb->dma,PCI_DMA_FROMDEVICE,
					       pages);
		if (0 != err)
			return err;
	} else {
		/* dma directly to userspace */
		dprintk(1,"user buf addr=%08lx size=%ld\n",
			vb->baddr,vb->bsize);
		err = videobuf_dma_init_user(&vb->dma,PCI_DMA_FROMDEVICE,
					     vb->baddr,vb->bsize);
		if (0 != err)
			return err;
	}
	err = videobuf_dma_pci_map(pci,&vb->dma);
	if (0 != err)
		return err;
	return 0;
}

#ifdef HAVE_V4L2
extern void
videobuf_status(struct v4l2_buffer *b, struct videobuf_buffer *vb)
{
	b->index  = vb->i;
	b->type   = vb->type;
	b->offset = vb->boff;
	b->length = vb->bsize;
	b->flags  = 0;
	if (vb->map)
		b->flags |= V4L2_BUF_FLAG_MAPPED;
	switch (vb->state) {
	case STATE_PREPARED:
	case STATE_QUEUED:
	case STATE_ACTIVE:
		b->flags |= V4L2_BUF_FLAG_QUEUED;
		break;
	case STATE_DONE:
	case STATE_ERROR:
		b->flags |= V4L2_BUF_FLAG_DONE;
		break;
	case STATE_NEEDS_INIT:
	case STATE_IDLE:
		/* nothing */
		break;
	}
	if (!(vb->field & VBUF_FIELD_INTER)) {
		if (vb->field & VBUF_FIELD_ODD)
			b->flags |= V4L2_BUF_FLAG_TOPFIELD;
		if (vb->field & VBUF_FIELD_EVEN)
			b->flags |= V4L2_BUF_FLAG_BOTFIELD;
	}
	b->timestamp = vb->ts;
	b->bytesused = vb->size;
	b->sequence  = vb->field_count >> 1;
}
#endif /* HAVE_V4L2 */

/* --------------------------------------------------------------------- */

static void
videobuf_vm_open(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;
	map->count++;
}

static void
videobuf_vm_close(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;
	int i;

	/* down(&fh->lock); FIXME */
	map->count--;
	if (0 == map->count) {
		dprintk(1,"munmap %p\n",map);
		for (i = 0; i < VIDEO_MAX_FRAME; i++) {
			if (NULL == map->buflist[i])
				continue;
			if (map->buflist[i]->map != map)
				continue;
			map->buflist[i]->map   = NULL;
			map->buflist[i]->baddr = 0;
			if (map->buflist[i]->free)
				map->buflist[i]->free(vma->vm_file,map->buflist[i]);
		}
		kfree(map);
	}
	/* up(&fh->lock); FIXME */
	return;
}

/*
 * Get a anonymous page for the mapping.  Make sure we can DMA to that
 * memory location with 32bit PCI devices (i.e. don't use highmem for
 * now ...).  Bounce buffers don't work very well for the data rates
 * video capture has.
 */
static struct page*
videobuf_vm_nopage(struct vm_area_struct *vma, unsigned long vaddr,
		  int write_access)
{
	struct page *page;

	dprintk(3,"nopage: fault @ %08lx [vma %08lx-%08lx]\n",
		vaddr,vma->vm_start,vma->vm_end);
        if (vaddr > vma->vm_end)
		return NOPAGE_SIGBUS;
	page = alloc_page(GFP_USER);
	if (!page)
		return NOPAGE_OOM;
	clear_user_page(page_address(page), vaddr);
	return page;
}

static struct vm_operations_struct videobuf_vm_ops =
{
	open:     videobuf_vm_open,
	close:    videobuf_vm_close,
	nopage:   videobuf_vm_nopage,
};

int videobuf_mmap_setup(struct file *file, struct videobuf_buffer **buflist,
			int msize, int bcount, int bsize, int type,
			videobuf_buffer_free free)
{
	int i,err;

	err = videobuf_mmap_free(file,buflist);
	if (0 != err)
		return err;
	
	for (i = 0; i < bcount; i++) {
		buflist[i] = videobuf_alloc(msize,type);
		buflist[i]->i     = i;
		buflist[i]->boff  = bsize * i;
		buflist[i]->bsize = bsize;
		buflist[i]->free  = free;
	}
	dprintk(1,"mmap setup: %d buffers, %d bytes each\n",
		bcount,bsize);
	return 0;
}

int videobuf_mmap_free(struct file *file, struct videobuf_buffer **buflist)
{
	int i;

	for (i = 0; i < VIDEO_MAX_FRAME; i++)
		if (buflist[i] && buflist[i]->map)
			return -EBUSY;
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == buflist[i])
			continue;
		if (buflist[i]->free)
			buflist[i]->free(file,buflist[i]);
		kfree(buflist[i]);
		buflist[i] = NULL;
	}
	return 0;
}

int videobuf_mmap_mapper(struct vm_area_struct *vma,
			struct videobuf_buffer **buflist)
{
	struct videobuf_mapping *map;
	int first,last,size,i;

	if (!(vma->vm_flags & VM_WRITE)) {
		dprintk(1,"mmap app bug: PROT_WRITE please\n");
		return -EINVAL;
	}
	if (!(vma->vm_flags & VM_SHARED)) {
		dprintk(1,"mmap app bug: MAP_SHARED please\n");
		return -EINVAL;
	}

	/* look for first buffer to map */
	for (first = 0; first < VIDEO_MAX_FRAME; first++) {
		if (NULL == buflist[first])
			continue;
		if (buflist[first]->boff  == (vma->vm_pgoff << PAGE_SHIFT))
			break;
	}
	if (VIDEO_MAX_FRAME == first) {
		dprintk(1,"mmap app bug: offset invalid [offset=0x%lx]\n",
			(vma->vm_pgoff << PAGE_SHIFT));
		return -EINVAL;
	}

	/* look for last buffer to map */
	for (size = 0, last = first; last < VIDEO_MAX_FRAME; last++) {
		if (NULL == buflist[last])
			continue;
		if (buflist[last]->map)
			return -EBUSY;
		size += buflist[last]->bsize;
		if (size == (vma->vm_end - vma->vm_start))
			break;
	}
	if (VIDEO_MAX_FRAME == last) {
		dprintk(1,"mmap app bug: size invalid [size=0x%lx]\n",
			(vma->vm_end - vma->vm_start));
		return -EINVAL;
	}

	/* create mapping + update buffer list */
	map = kmalloc(sizeof(struct videobuf_mapping),GFP_KERNEL);
	if (NULL == map)
		return -ENOMEM;
	for (size = 0, i = first; i <= last; size += buflist[i++]->bsize) {
		buflist[i]->map   = map;
		buflist[i]->baddr = vma->vm_start + size;
	}
	map->count   = 1;
	map->start   = vma->vm_start;
	map->end     = vma->vm_end;
	map->buflist = buflist;
	vma->vm_ops  = &videobuf_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND;
	vma->vm_private_data = map;
	dprintk(1,"mmap %p: %08lx-%08lx pgoff %08lx bufs %d-%d\n",
		map,vma->vm_start,vma->vm_end,vma->vm_pgoff,first,last);
	return 0;
}

/* --------------------------------------------------------------------- */

EXPORT_SYMBOL_GPL(videobuf_vmalloc_to_sg);
EXPORT_SYMBOL_GPL(videobuf_iobuf_to_sg);

EXPORT_SYMBOL_GPL(videobuf_dma_init_user);
EXPORT_SYMBOL_GPL(videobuf_dma_init_kernel);
EXPORT_SYMBOL_GPL(videobuf_dma_pci_map);
EXPORT_SYMBOL_GPL(videobuf_dma_pci_sync);
EXPORT_SYMBOL_GPL(videobuf_dma_pci_unmap);
EXPORT_SYMBOL_GPL(videobuf_dma_free);

EXPORT_SYMBOL_GPL(videobuf_alloc);
EXPORT_SYMBOL_GPL(videobuf_waiton);
EXPORT_SYMBOL_GPL(videobuf_iolock);
#ifdef HAVE_V4L2
EXPORT_SYMBOL_GPL(videobuf_status);
#endif

EXPORT_SYMBOL_GPL(videobuf_mmap_setup);
EXPORT_SYMBOL_GPL(videobuf_mmap_free);
EXPORT_SYMBOL_GPL(videobuf_mmap_mapper);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
