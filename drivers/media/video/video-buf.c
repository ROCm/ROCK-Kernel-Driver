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

void* videobuf_alloc(int size)
{
	struct videobuf_buffer *vb;

	vb = kmalloc(size,GFP_KERNEL);
	if (NULL != vb) {
		memset(vb,0,size);
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

/* --------------------------------------------------------------------- */

void
videobuf_queue_init(struct videobuf_queue *q,
		    struct videobuf_queue_ops *ops,
		    struct pci_dev *pci,
		    spinlock_t *irqlock,
		    int type,
		    int msize)
{
	memset(q,0,sizeof(*q));

	q->irqlock = irqlock;
	q->pci     = pci;
	q->type    = type;
	q->msize   = msize;
	q->ops     = ops;

	init_MUTEX(&q->lock);
	INIT_LIST_HEAD(&q->stream);
}

void
videobuf_queue_cancel(struct file *file, struct videobuf_queue *q)
{
	unsigned long flags;
	int i;

	/* remove queued buffers from list */
	spin_lock_irqsave(q->irqlock,flags);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		if (q->bufs[i]->state == STATE_QUEUED) {
			list_del(&q->bufs[i]->queue);
			q->bufs[i]->state = STATE_ERROR;
		}
	}
	spin_unlock_irqrestore(q->irqlock,flags);

	/* free all buffers + clear queue */
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		q->ops->buf_release(file,q->bufs[i]);
	}
	INIT_LIST_HEAD(&q->stream);
}

/* --------------------------------------------------------------------- */

#ifdef HAVE_V4L2
void
videobuf_status(struct v4l2_buffer *b, struct videobuf_buffer *vb, int type)
{
	b->index  = vb->i;
	b->type   = type;
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

int
videobuf_reqbufs(struct file *file, struct videobuf_queue *q,
		 struct v4l2_requestbuffers *req)
{
	int size,count,retval;

	if ((req->type & V4L2_BUF_TYPE_field) != q->type)
		return -EINVAL;
	if (req->count < 1)
		return -EINVAL;

	down(&q->lock);
	count = req->count;
	if (count > VIDEO_MAX_FRAME)
		count = VIDEO_MAX_FRAME;
	size = 0;
	q->ops->buf_setup(file,&count,&size);
	size = PAGE_ALIGN(size);

	retval = videobuf_mmap_setup(file,q,count,size);
	if (retval < 0)
		goto done;
	req->type  = q->type;
	req->count = count;

 done:
	up(&q->lock);
	return retval;
}

int
videobuf_querybuf(struct videobuf_queue *q, struct v4l2_buffer *b)
{
	if ((b->type & V4L2_BUF_TYPE_field) != q->type)
		return -EINVAL;
	if (b->index < 0 || b->index >= VIDEO_MAX_FRAME)
		return -EINVAL;
	if (NULL == q->bufs[b->index])
		return -EINVAL;
	videobuf_status(b,q->bufs[b->index],q->type);
	return 0;
}

int
videobuf_qbuf(struct file *file, struct videobuf_queue *q,
	      struct v4l2_buffer *b)
{
	struct videobuf_buffer *buf;
	unsigned long flags;
	int field = 0;
	int retval;

	down(&q->lock);
	retval = -EBUSY;
	if (q->reading)
		goto done;
	retval = -EINVAL;
	if ((b->type & V4L2_BUF_TYPE_field) != q->type)
		goto done;
	if (b->index < 0 || b->index >= VIDEO_MAX_FRAME)
		goto done;
	buf = q->bufs[b->index];
	if (NULL == buf)
		goto done;
	if (0 == buf->baddr)
		goto done;
	if (buf->state == STATE_QUEUED ||
	    buf->state == STATE_ACTIVE)
		goto done;

	if (b->flags & V4L2_BUF_FLAG_TOPFIELD)
		field |= VBUF_FIELD_ODD;
	if (b->flags & V4L2_BUF_FLAG_BOTFIELD)
		field |= VBUF_FIELD_EVEN;
	retval = q->ops->buf_prepare(file,buf,field);
	if (0 != retval)
		goto done;
	
	list_add_tail(&buf->stream,&q->stream);
	if (q->streaming) {
		spin_lock_irqsave(q->irqlock,flags);
		q->ops->buf_queue(file,buf);
		spin_unlock_irqrestore(q->irqlock,flags);
	}
	retval = 0;
	
 done:
	up(&q->lock);
	return retval;
}

int
videobuf_dqbuf(struct file *file, struct videobuf_queue *q,
	       struct v4l2_buffer *b)
{
	struct videobuf_buffer *buf;
	int retval;
	
	down(&q->lock);
	retval = -EBUSY;
	if (q->reading)
		goto done;
	retval = -EINVAL;
	if ((b->type & V4L2_BUF_TYPE_field) != q->type)
		goto done;
	if (list_empty(&q->stream))
		goto done;
	buf = list_entry(q->stream.next, struct videobuf_buffer, stream);
	retval = videobuf_waiton(buf,1,1);
	if (retval < 0)
		goto done;
	switch (buf->state) {
	case STATE_ERROR:
		retval = -EIO;
		/* fall through */
	case STATE_DONE:
		videobuf_dma_pci_sync(q->pci,&buf->dma);
		buf->state = STATE_IDLE;
		break;
	default:
		retval = -EINVAL;
		goto done;
	}
	list_del(&buf->stream);
	memset(b,0,sizeof(*b));
	videobuf_status(b,buf,q->type);

 done:
	up(&q->lock);
	return retval;
}
#endif /* HAVE_V4L2 */

int videobuf_streamon(struct file *file, struct videobuf_queue *q)
{
	struct videobuf_buffer *buf;
	struct list_head *list;
	unsigned long flags;
	int retval;
	
	down(&q->lock);
	retval = -EBUSY;
	if (q->reading)
		goto done;
	retval = 0;
	if (q->streaming)
		goto done;
	q->streaming = 1;
	spin_lock_irqsave(q->irqlock,flags);
	list_for_each(list,&q->stream) {
		buf = list_entry(list, struct videobuf_buffer, stream);
		if (buf->state == STATE_PREPARED)
			q->ops->buf_queue(file,buf);
	}
	spin_unlock_irqrestore(q->irqlock,flags);

 done:
	up(&q->lock);
	return retval;
}

int videobuf_streamoff(struct file *file, struct videobuf_queue *q)
{
	int retval = -EINVAL;

	down(&q->lock);
	if (!q->streaming)
		goto done;
	videobuf_queue_cancel(file,q);
	q->streaming = 0;
	retval = 0;

 done:
	up(&q->lock);
	return retval;
}

static ssize_t
videobuf_read_zerocopy(struct file *file, struct videobuf_queue *q,
		       char *data, size_t count, loff_t *ppos)
{
        int retval;

        /* setup stuff */
	retval = -ENOMEM;
	q->read_buf = videobuf_alloc(q->msize);
	if (NULL == q->read_buf)
		goto done;

	q->read_buf->baddr = (unsigned long)data;
        q->read_buf->bsize = count;
	retval = q->ops->buf_prepare(file,q->read_buf,0);
	if (0 != retval)
		goto done;
	
        /* start capture & wait */
	q->ops->buf_queue(file,q->read_buf);
        retval = videobuf_waiton(q->read_buf,0,0);
        if (0 == retval) {
		videobuf_dma_pci_sync(q->pci,&q->read_buf->dma);
                retval = q->read_buf->size;
	}

 done:
	/* cleanup */
	q->ops->buf_release(file,q->read_buf);
	kfree(q->read_buf);
	q->read_buf = NULL;
	return retval;
}

ssize_t videobuf_read_one(struct file *file, struct videobuf_queue *q,
			  char *data, size_t count, loff_t *ppos)
{
	int retval, bytes, size, nbufs;

	down(&q->lock);

	nbufs = 1; size = 0;
	q->ops->buf_setup(file,&nbufs,&size);
	if (NULL == q->read_buf  &&
	    count >= size        &&
	    !(file->f_flags & O_NONBLOCK)) {
		retval = videobuf_read_zerocopy(file,q,data,count,ppos);
		if (retval >= 0)
			/* ok, all done */
			goto done;
		/* fallback to kernel bounce buffer on failures */
	}

	if (NULL == q->read_buf) {
		/* need to capture a new frame */
		retval = -ENOMEM;
		q->read_buf = videobuf_alloc(q->msize);
		if (NULL == q->read_buf)
			goto done;
		retval = q->ops->buf_prepare(file,q->read_buf,0);
		if (0 != retval)
			goto done;
		q->ops->buf_queue(file,q->read_buf);
		q->read_off = 0;
	}

	/* wait until capture is done */
        retval = videobuf_waiton(q->read_buf, file->f_flags & O_NONBLOCK, 1);
	if (0 != retval)
		goto done;
	videobuf_dma_pci_sync(q->pci,&q->read_buf->dma);

	/* copy to userspace */
	bytes = count;
	if (bytes > q->read_buf->size - q->read_off)
		bytes = q->read_buf->size - q->read_off;
	retval = -EFAULT;
	if (copy_to_user(data, q->read_buf->dma.vmalloc+q->read_off, bytes))
		goto done;

	retval = bytes;
	q->read_off += bytes;
	if (q->read_off == q->read_buf->size) {
		/* all data copied, cleanup */
		q->ops->buf_release(file,q->read_buf);
		kfree(q->read_buf);
		q->read_buf = NULL;
	}

 done:
	up(&q->lock);
	return retval;
}

int videobuf_read_start(struct file *file, struct videobuf_queue *q)
{
	unsigned long flags;
	int count = 0, size = 0;
	int err, i;

	q->ops->buf_setup(file,&count,&size);
	if (count < 2)
		count = 2;
	if (count > VIDEO_MAX_FRAME)
		count = VIDEO_MAX_FRAME;
	size = PAGE_ALIGN(size);

	err = videobuf_mmap_setup(file, q, count, size);
	if (err)
		return err;
	for (i = 0; i < count; i++) {
		err = q->ops->buf_prepare(file,q->bufs[i],0);
		if (err)
			return err;
		list_add_tail(&q->bufs[i]->stream, &q->stream);
	}
	spin_lock_irqsave(q->irqlock,flags);
	for (i = 0; i < count; i++)
		q->ops->buf_queue(file,q->bufs[i]);
	spin_unlock_irqrestore(q->irqlock,flags);
	q->reading = 1;
	return 0;
}

void videobuf_read_stop(struct file *file, struct videobuf_queue *q)
{
	int i;
	
	videobuf_queue_cancel(file,q);
	INIT_LIST_HEAD(&q->stream);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		kfree(q->bufs[i]);
		q->bufs[i] = NULL;
	}
	q->read_buf = NULL;
	q->reading  = 0;
}

ssize_t videobuf_read_stream(struct file *file, struct videobuf_queue *q,
			     char *data, size_t count, loff_t *ppos,
			     int vbihack)
{
	unsigned int *fc;
	int err, bytes, retval;
	unsigned long flags;
	
	down(&q->lock);
	retval = -EBUSY;
	if (q->streaming)
		goto done;
	if (!q->reading) {
		retval = videobuf_read_start(file,q);
		if (retval < 0)
			goto done;
	}

	retval = 0;
	while (count > 0) {
		/* get / wait for data */
		if (NULL == q->read_buf) {
			q->read_buf = list_entry(q->stream.next,
						 struct videobuf_buffer,
						 stream);
			list_del(&q->read_buf->stream);
			q->read_off = 0;
		}
		err = videobuf_waiton(q->read_buf,
				      file->f_flags & O_NONBLOCK,1);
		if (err < 0) {
			if (0 == retval)
				retval = err;
			break;
		}

		if (vbihack) {
			/* dirty, undocumented hack -- pass the frame counter
			 * within the last four bytes of each vbi data block.
			 * We need that one to maintain backward compatibility
			 * to all vbi decoding software out there ... */
			fc  = (unsigned int*)q->read_buf->dma.vmalloc;
			fc += (q->read_buf->size>>2) -1;
			*fc = q->read_buf->field_count >> 1;
		}

		/* copy stuff */
		bytes = count;
		if (bytes > q->read_buf->size - q->read_off)
			bytes = q->read_buf->size - q->read_off;
		if (copy_to_user(data + retval,
				 q->read_buf->dma.vmalloc + q->read_off,
				 bytes)) {
			if (0 == retval)
				retval = -EFAULT;
			break;
		}
		count       -= bytes;
		retval      += bytes;
		q->read_off += bytes;

		/* requeue buffer when done with copying */
		if (q->read_off == q->read_buf->size) {
			list_add_tail(&q->read_buf->stream,
				      &q->stream);
			spin_lock_irqsave(q->irqlock,flags);
			q->ops->buf_queue(file,q->read_buf);
			spin_unlock_irqrestore(q->irqlock,flags);
			q->read_buf = NULL;
		}
	}

 done:
	up(&q->lock);
	return retval;
}

unsigned int videobuf_poll_stream(struct file *file,
				  struct videobuf_queue *q,
				  poll_table *wait)
{
	struct videobuf_buffer *buf = NULL;
	unsigned int rc = 0;

	down(&q->lock);
	if (q->streaming) {
		if (!list_empty(&q->stream))
			buf = list_entry(q->stream.next,
					 struct videobuf_buffer, stream);
	} else {
		if (!q->reading)
			videobuf_read_start(file,q);
		if (!q->reading) {
			rc = POLLERR;
		} else if (NULL == q->read_buf) {
			q->read_buf = list_entry(q->stream.next,
						 struct videobuf_buffer,
						 stream);
			list_del(&q->read_buf->stream);
			q->read_off = 0;
		}
		buf = q->read_buf;
	}
	if (!buf)
		rc = POLLERR;

	if (0 == rc) {
		poll_wait(file, &buf->done, wait);
		if (buf->state == STATE_DONE ||
		    buf->state == STATE_ERROR)
			rc = POLLIN|POLLRDNORM;
	}
	up(&q->lock);
	return rc;
}

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
			if (NULL == map->q->bufs[i])
				continue;
			if (map->q->bufs[i])
				;
			if (map->q->bufs[i]->map != map)
				continue;
			map->q->bufs[i]->map   = NULL;
			map->q->bufs[i]->baddr = 0;
			map->q->ops->buf_release(vma->vm_file,map->q->bufs[i]);
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
	clear_user_page(page_address(page), vaddr, page);
	return page;
}

static struct vm_operations_struct videobuf_vm_ops =
{
	open:     videobuf_vm_open,
	close:    videobuf_vm_close,
	nopage:   videobuf_vm_nopage,
};

int videobuf_mmap_setup(struct file *file, struct videobuf_queue *q,
			int bcount, int bsize)
{
	int i,err;

	err = videobuf_mmap_free(file,q);
	if (0 != err)
		return err;
	
	for (i = 0; i < bcount; i++) {
		q->bufs[i] = videobuf_alloc(q->msize);
		q->bufs[i]->i     = i;
		q->bufs[i]->boff  = bsize * i;
		q->bufs[i]->bsize = bsize;
	}
	dprintk(1,"mmap setup: %d buffers, %d bytes each\n",
		bcount,bsize);
	return 0;
}

int videobuf_mmap_free(struct file *file, struct videobuf_queue *q)
{
	int i;

	for (i = 0; i < VIDEO_MAX_FRAME; i++)
		if (q->bufs[i] && q->bufs[i]->map)
			return -EBUSY;
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		q->ops->buf_release(file,q->bufs[i]);
		kfree(q->bufs[i]);
		q->bufs[i] = NULL;
	}
	return 0;
}

int videobuf_mmap_mapper(struct vm_area_struct *vma,
			 struct videobuf_queue *q)
{
	struct videobuf_mapping *map;
	int first,last,size,i,retval;

	down(&q->lock);
	retval = -EINVAL;
	if (!(vma->vm_flags & VM_WRITE)) {
		dprintk(1,"mmap app bug: PROT_WRITE please\n");
		goto done;
	}
	if (!(vma->vm_flags & VM_SHARED)) {
		dprintk(1,"mmap app bug: MAP_SHARED please\n");
		goto done;
	}

	/* look for first buffer to map */
	for (first = 0; first < VIDEO_MAX_FRAME; first++) {
		if (NULL == q->bufs[first])
			continue;
		if (q->bufs[first]->boff  == (vma->vm_pgoff << PAGE_SHIFT))
			break;
	}
	if (VIDEO_MAX_FRAME == first) {
		dprintk(1,"mmap app bug: offset invalid [offset=0x%lx]\n",
			(vma->vm_pgoff << PAGE_SHIFT));
		goto done;
	}

	/* look for last buffer to map */
	for (size = 0, last = first; last < VIDEO_MAX_FRAME; last++) {
		if (NULL == q->bufs[last])
			continue;
		if (q->bufs[last]->map) {
			retval = -EBUSY;
			goto done;
		}
		size += q->bufs[last]->bsize;
		if (size == (vma->vm_end - vma->vm_start))
			break;
	}
	if (VIDEO_MAX_FRAME == last) {
		dprintk(1,"mmap app bug: size invalid [size=0x%lx]\n",
			(vma->vm_end - vma->vm_start));
		goto done;
	}

	/* create mapping + update buffer list */
	retval = -ENOMEM;
	map = kmalloc(sizeof(struct videobuf_mapping),GFP_KERNEL);
	if (NULL == map)
		goto done;
	for (size = 0, i = first; i <= last; size += q->bufs[i++]->bsize) {
		q->bufs[i]->map   = map;
		q->bufs[i]->baddr = vma->vm_start + size;
	}
	map->count   = 1;
	map->start   = vma->vm_start;
	map->end     = vma->vm_end;
	map->q       = q;
	vma->vm_ops  = &videobuf_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND;
	vma->vm_private_data = map;
	dprintk(1,"mmap %p: %08lx-%08lx pgoff %08lx bufs %d-%d\n",
		map,vma->vm_start,vma->vm_end,vma->vm_pgoff,first,last);
	retval = 0;

 done:
	up(&q->lock);
	return retval;
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

EXPORT_SYMBOL_GPL(videobuf_queue_init);
EXPORT_SYMBOL_GPL(videobuf_queue_cancel);

#ifdef HAVE_V4L2
EXPORT_SYMBOL_GPL(videobuf_status);
EXPORT_SYMBOL_GPL(videobuf_reqbufs);
EXPORT_SYMBOL_GPL(videobuf_querybuf);
EXPORT_SYMBOL_GPL(videobuf_qbuf);
EXPORT_SYMBOL_GPL(videobuf_dqbuf);
#endif
EXPORT_SYMBOL_GPL(videobuf_streamon);
EXPORT_SYMBOL_GPL(videobuf_streamoff);

EXPORT_SYMBOL_GPL(videobuf_read_start);
EXPORT_SYMBOL_GPL(videobuf_read_stop);
EXPORT_SYMBOL_GPL(videobuf_read_stream);
EXPORT_SYMBOL_GPL(videobuf_read_one);
EXPORT_SYMBOL_GPL(videobuf_poll_stream);

EXPORT_SYMBOL_GPL(videobuf_mmap_setup);
EXPORT_SYMBOL_GPL(videobuf_mmap_free);
EXPORT_SYMBOL_GPL(videobuf_mmap_mapper);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
