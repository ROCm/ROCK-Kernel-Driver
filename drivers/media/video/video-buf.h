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

#include <linux/videodev.h>

/* --------------------------------------------------------------------- */

/*
 * Return a scatterlist for some page-aligned vmalloc()'ed memory
 * block (NULL on errors).  Memory for the scatterlist is allocated
 * using kmalloc.  The caller must free the memory.
 */
struct scatterlist* videobuf_vmalloc_to_sg(unsigned char *virt, int nr_pages);

/*
 * Return a scatterlist for a locked iobuf (NULL on errors).  Memory
 * for the scatterlist is allocated using kmalloc.  The caller must
 * free the memory.
 */
struct scatterlist* videobuf_iobuf_to_sg(struct kiobuf *iobuf);

/* --------------------------------------------------------------------- */

/*
 * A small set of helper functions to manage buffers (both userland
 * and kernel) for DMA.
 *
 * videobuf_init_*_dmabuf()
 *	creates a buffer.  The userland version takes a userspace
 *	pointer + length.  The kernel version just wants the size and
 *	does memory allocation too using vmalloc_32().
 *
 * videobuf_pci_*_dmabuf()
 *	see Documentation/DMA-mapping.txt, these functions to
 *	basically the same.  The map function does also build a
 *	scatterlist for the buffer (and unmap frees it ...)
 *
 * videobuf_free_dmabuf()
 *	no comment ...
 *
 */

struct videobuf_dmabuf {
	/* for userland buffer */
	struct kiobuf       *iobuf;

	/* for kernel buffers */
	void                *vmalloc;

	/* common */
	struct scatterlist  *sglist;
	int                 sglen;
	int                 nr_pages;
	int                 direction;
};

int videobuf_dma_init_user(struct videobuf_dmabuf *dma, int direction,
			   unsigned long data, unsigned long size);
int videobuf_dma_init_kernel(struct videobuf_dmabuf *dma, int direction,
			     int nr_pages);
int videobuf_dma_pci_map(struct pci_dev *dev, struct videobuf_dmabuf *dma);
int videobuf_dma_pci_sync(struct pci_dev *dev,
			  struct videobuf_dmabuf *dma);
int videobuf_dma_pci_unmap(struct pci_dev *dev, struct videobuf_dmabuf *dma);
int videobuf_dma_free(struct videobuf_dmabuf *dma);

/* --------------------------------------------------------------------- */

/*
 * A small set of helper functions to manage video4linux buffers.
 *
 * struct videobuf_buffer holds the data structures used by the helper
 * functions, additionally some commonly used fields for v4l buffers
 * (width, height, lists, waitqueue) are in there.  That struct should
 * be used as first element in the drivers buffer struct.
 * 
 * about the mmap helpers (videobuf_mmap_*):
 *
 * The mmaper function allows to map any subset of contingous buffers.
 * This includes one mmap() call for all buffers (which the original
 * video4linux API uses) as well as one mmap() for every single buffer
 * (which v4l2 uses).
 *
 * If there is a valid mapping for a buffer, buffer->baddr/bsize holds
 * userspace address + size which can be feeded into the
 * videobuf_dma_init_user function listed above.
 *
 */

struct videobuf_buffer;
struct videobuf_queue;

struct videobuf_mapping {
	int count;
	int highmem_ok;
	unsigned long start;
	unsigned long end;
	struct videobuf_queue *q;
};

#define VBUF_FIELD_EVEN  1
#define VBUF_FIELD_ODD   2
#define VBUF_FIELD_INTER 4

enum videobuf_state {
	STATE_NEEDS_INIT = 0,
	STATE_PREPARED   = 1,
	STATE_QUEUED     = 2,
	STATE_ACTIVE     = 3,
	STATE_DONE       = 4,
	STATE_ERROR      = 5,
	STATE_IDLE       = 6,
};

struct videobuf_buffer {
	int                     i;

	/* info about the buffer */
	int                     width;
	int                     height;
	long                    size;
	int                     field;
	enum videobuf_state     state;
	struct videobuf_dmabuf  dma;
	struct list_head        stream;  /* QBUF/DQBUF list */

	/* for mmap'ed buffers */
	unsigned long  boff;             /* buffer offset (mmap) */
	unsigned long  bsize;            /* buffer size */
	unsigned long  baddr;            /* buffer addr (userland ptr!) */
	struct videobuf_mapping *map;

	/* touched by irq handler */
	struct list_head        queue;
	wait_queue_head_t       done;
	int                     field_count;
#ifdef HAVE_V4L2
	stamp_t                 ts;
#endif
};

struct videobuf_queue_ops {
	int (*buf_setup)(struct file *file, int *count, int *size);
	int (*buf_prepare)(struct file *file,struct videobuf_buffer *vb,
			   int field);
	void (*buf_queue)(struct file *file,struct videobuf_buffer *vb);
	void (*buf_release)(struct file *file,struct videobuf_buffer *vb);
};

struct videobuf_queue {
        struct semaphore           lock;
	spinlock_t                 *irqlock;
	struct pci_dev             *pci;

	int                        type;
	int                        msize;
	struct videobuf_buffer     *bufs[VIDEO_MAX_FRAME];
	struct videobuf_queue_ops  *ops;

	/* capture via mmap() + ioctl(QBUF/DQBUF) */
	int                        streaming;
	struct list_head           stream;

	/* capture via read() */
	int                        reading;
	int                        read_off;
	struct videobuf_buffer     *read_buf;
};

void* videobuf_alloc(int size);
int videobuf_waiton(struct videobuf_buffer *vb, int non_blocking, int intr);
int videobuf_iolock(struct pci_dev *pci, struct videobuf_buffer *vb);

void videobuf_queue_init(struct videobuf_queue *q,
			 struct videobuf_queue_ops *ops,
			 struct pci_dev *pci, spinlock_t *irqlock,
			 int type, int msize);
void videobuf_queue_cancel(struct file *file, struct videobuf_queue *q);

#ifdef HAVE_V4L2
void videobuf_status(struct v4l2_buffer *b, struct videobuf_buffer *vb,
		     int type);
int videobuf_reqbufs(struct file *file, struct videobuf_queue *q,
		     struct v4l2_requestbuffers *req);
int videobuf_querybuf(struct videobuf_queue *q, struct v4l2_buffer *b);
int videobuf_qbuf(struct file *file, struct videobuf_queue *q,
		  struct v4l2_buffer *b);
int videobuf_dqbuf(struct file *file, struct videobuf_queue *q,
		   struct v4l2_buffer *b);
#endif
int videobuf_streamon(struct file *file, struct videobuf_queue *q);
int videobuf_streamoff(struct file *file, struct videobuf_queue *q);

int videobuf_read_start(struct file *file, struct videobuf_queue *q);
void videobuf_read_stop(struct file *file, struct videobuf_queue *q);
ssize_t videobuf_read_stream(struct file *file, struct videobuf_queue *q,
			     char *data, size_t count, loff_t *ppos,
			     int vbihack);
ssize_t videobuf_read_one(struct file *file, struct videobuf_queue *q,
			  char *data, size_t count, loff_t *ppos);
unsigned int videobuf_poll_stream(struct file *file,
				  struct videobuf_queue *q,
				  poll_table *wait);

int videobuf_mmap_setup(struct file *file, struct videobuf_queue *q,
			int bcount, int bsize);
int videobuf_mmap_free(struct file *file, struct videobuf_queue *q);
int videobuf_mmap_mapper(struct vm_area_struct *vma,
			 struct videobuf_queue *q);

/* --------------------------------------------------------------------- */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
