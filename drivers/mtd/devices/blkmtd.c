/* 
 * $Id: blkmtd.c,v 1.3 2001/10/02 15:33:20 dwmw2 Exp $
 * blkmtd.c - use a block device as a fake MTD
 *
 * Author: Simon Evans <spse@secret.org.uk>
 *
 * Copyright (C) 2001 Simon Evans <spse@secret.org.uk>
 * 
 * Licence: GPL
 *
 * How it works:
 *       The driver uses raw/io to read/write the device and the page
 *       cache to cache access. Writes update the page cache with the
 *       new data but make a copy of the new page(s) and then a kernel
 *       thread writes pages out to the device in the background. This
 *       ensures tht writes are order even if a page is updated twice.
 *       Also, since pages in the page cache are never marked as dirty,
 *       we dont have to worry about writepage() being called on some 
 *       random page which may not be in the write order.
 * 
 *       Erases are handled like writes, so the callback is called after
 *       the page cache has been updated. Sync()ing will wait until it is 
 *       all done.
 *
 *       It can be loaded Read-Only to prevent erases and writes to the 
 *       medium.
 *
 * Todo:
 *       Make the write queue size dynamic so this it is not too big on
 *       small memory systems and too small on large memory systems.
 * 
 *       Page cache usage may still be a bit wrong. Check we are doing
 *       everything proberly.
 * 
 *       Somehow allow writes to dirty the page cache so we dont use too
 *       much memory making copies of outgoing pages. Need to handle case
 *       where page x is written to, then page y, then page x again before
 *       any of them have been committed to disk.
 * 
 *       Reading should read multiple pages at once rather than using 
 *       readpage() for each one. This is easy and will be fixed asap.
 *
 *       Dont run the write_thread if readonly. This is also easy and will
 *       be fixed asap.
 * 
 *       Even though the multiple erase regions are used if the default erase
 *       block size doesnt match the device properly, erases currently wont
 *       work on the last page if it is not a full page.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/iobuf.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mtd/compatmac.h>
#include <linux/mtd/mtd.h>

/* Default erase size in K, always make it a multiple of PAGE_SIZE */
#define CONFIG_MTD_BLKDEV_ERASESIZE 128
#define VERSION "1.1"
extern int *blk_size[];
extern int *blksize_size[];

/* Info for the block device */
typedef struct mtd_raw_dev_data_s {
  struct block_device *binding;
  int sector_size, sector_bits, total_sectors;
  size_t totalsize;
  int readonly;
  struct address_space as;
  struct file *file;
} mtd_raw_dev_data_t;

/* Info for each queue item in the write queue */
typedef struct mtdblkdev_write_queue_s {
  mtd_raw_dev_data_t *rawdevice;
  struct page **pages;
  int pagenr;
  int pagecnt;
  int iserase;
} mtdblkdev_write_queue_t;


/* Static info about the MTD, used in cleanup_module */
static struct mtd_info *mtd_info;

/* Write queue fixed size */
#define WRITE_QUEUE_SZ 512

/* Storage for the write queue */
static mtdblkdev_write_queue_t write_queue[WRITE_QUEUE_SZ];
static int volatile write_queue_head;
static int volatile write_queue_tail;
static int volatile write_queue_cnt;
static spinlock_t mbd_writeq_lock = SPIN_LOCK_UNLOCKED;

/* Tell the write thread to finish */
static volatile int write_task_finish = 0;

/* ipc with the write thread */
#if LINUX_VERSION_CODE > 0x020300
static DECLARE_MUTEX_LOCKED(thread_sem);
static DECLARE_WAIT_QUEUE_HEAD(thr_wq);
static DECLARE_WAIT_QUEUE_HEAD(mtbd_sync_wq);
#else
static struct semaphore thread_sem = MUTEX_LOCKED;
DECLARE_WAIT_QUEUE_HEAD(thr_wq);
DECLARE_WAIT_QUEUE_HEAD(mtbd_sync_wq);
#endif



/* Module parameters passed by insmod/modprobe */
char *device;    /* the block device to use */
int erasesz;     /* optional default erase size */
int ro;          /* optional read only flag */
int bs;          /* optionally force the block size (avoid using) */
int count;       /* optionally force the block count (avoid using) */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Evans <spse@secret.org.uk>");
MODULE_DESCRIPTION("Emulate an MTD using a block device");
MODULE_PARM(device, "s");
MODULE_PARM_DESC(device, "block device to use");
MODULE_PARM(erasesz, "i");
MODULE_PARM_DESC(erasesz, "optional erase size to use in KB. eg 4=4K.");
MODULE_PARM(ro, "i");
MODULE_PARM_DESC(ro, "1=Read only, writes and erases cause errors");
MODULE_PARM(bs, "i");
MODULE_PARM_DESC(bs, "force the block size in bytes");
MODULE_PARM(count, "i");
MODULE_PARM_DESC(count, "force the block count");
#endif



/* Page cache stuff */

/* writepage() - should never be called - catch it anyway */
static int blkmtd_writepage(struct page *page)
{
  printk("blkmtd: writepage called!!!\n");
  return -EIO;
}


/* readpage() - reads one page from the block device */                 
static int blkmtd_readpage(struct file *file, struct page *page)
{  
  int err;
  int sectornr, sectors, i;
  struct kiobuf *iobuf;
  mtd_raw_dev_data_t *rawdevice = (mtd_raw_dev_data_t *)file->private_data;
  kdev_t dev;

  if(!rawdevice) {
    printk("blkmtd: readpage: PANIC file->private_data == NULL\n");
    return -EIO;
  }
  dev = to_kdev_t(rawdevice->binding->bd_dev);

  DEBUG(2, "blkmtd: readpage called, dev = `%s' page = %p index = %ld\n",
	bdevname(dev), page, page->index);

  if(Page_Uptodate(page)) {
    DEBUG(1, "blkmtd: readpage page %ld is already upto date\n", page->index);
    UnlockPage(page);
    return 0;
  }

  ClearPageUptodate(page);
  ClearPageError(page);

  /* see if page is in the outgoing write queue */
  spin_lock(&mbd_writeq_lock);
  if(write_queue_cnt) {
    int i = write_queue_tail;
    while(i != write_queue_head) {
      mtdblkdev_write_queue_t *item = &write_queue[i];
      if(page->index >= item->pagenr && page->index < item->pagenr+item->pagecnt) {
	/* yes it is */
	int index = item->pagenr - page->index;
	DEBUG(1, "blkmtd: readpage: found page %ld in outgoing write queue\n",
	      page->index);
	if(item->iserase) {
	  memset(page_address(page), 0xff, PAGE_SIZE);
	} else {
	  memcpy(page_address(page), page_address(item->pages[index]), PAGE_SIZE);
	}
	SetPageUptodate(page);
	flush_dcache_page(page);
	UnlockPage(page);
	spin_unlock(&mbd_writeq_lock);
	return 0;
      }
      i++;
      i %= WRITE_QUEUE_SZ;
    }
  }
  spin_unlock(&mbd_writeq_lock);


  DEBUG(3, "blkmtd: readpage: getting kiovec\n");
  err = alloc_kiovec(1, &iobuf);
  if (err) {
    return err;
  }
  iobuf->offset = 0;
  iobuf->nr_pages = 1;
  iobuf->length = PAGE_SIZE;
  iobuf->locked = 1;
  iobuf->maplist[0] = page;
  sectornr = page->index << (PAGE_SHIFT - rawdevice->sector_bits);
  sectors = 1 << (PAGE_SHIFT - rawdevice->sector_bits);
  DEBUG(3, "blkmtd: readpage: sectornr = %d sectors = %d\n", sectornr, sectors);
  for(i = 0; i < sectors; i++) {
    iobuf->blocks[i] = sectornr++;
  }

  DEBUG(3, "bklmtd: readpage: starting brw_kiovec\n");
  err = brw_kiovec(READ, 1, &iobuf, dev, iobuf->blocks, rawdevice->sector_size);
  DEBUG(3, "blkmtd: readpage: finished, err = %d\n", err);
  iobuf->locked = 0;
  free_kiovec(1, &iobuf);
  if(err != PAGE_SIZE) {
    printk("blkmtd: readpage: error reading page %ld\n", page->index);
    memset(page_address(page), 0, PAGE_SIZE);
    SetPageError(page);
    err = -EIO;
  } else {
    DEBUG(3, "blkmtd: readpage: setting page upto date\n");
    SetPageUptodate(page);
    err = 0;
  }
  flush_dcache_page(page);
  UnlockPage(page);
  DEBUG(2, "blkmtd: readpage: finished, err = %d\n", err);
  return 0;
}

                    
static struct address_space_operations blkmtd_aops = {
  writepage:     blkmtd_writepage,
  readpage:      blkmtd_readpage,
}; 


/* This is the kernel thread that empties the write queue to disk */
static int write_queue_task(void *data)
{
  int err;
  struct task_struct *tsk = current;
  struct kiobuf *iobuf;

  DECLARE_WAITQUEUE(wait, tsk);
  DEBUG(1, "blkmtd: writetask: starting (pid = %d)\n", tsk->pid);
  daemonize();
  strcpy(tsk->comm, "blkmtdd");
  tsk->tty = NULL;
  spin_lock_irq(&tsk->sigmask_lock);
  sigfillset(&tsk->blocked);
  recalc_sigpending(tsk);
  spin_unlock_irq(&tsk->sigmask_lock);
  exit_sighand(tsk);

  if(alloc_kiovec(1, &iobuf))
    return 0;
  DEBUG(2, "blkmtd: writetask: entering main loop\n");
  add_wait_queue(&thr_wq, &wait);

  while(1) {
    spin_lock(&mbd_writeq_lock);

    if(!write_queue_cnt) {
      /* If nothing in the queue, wake up anyone wanting to know when there
	 is space in the queue then sleep for 2*HZ */
      spin_unlock(&mbd_writeq_lock);
      DEBUG(3, "blkmtd: writetask: queue empty\n");
      if(waitqueue_active(&mtbd_sync_wq))
	 wake_up(&mtbd_sync_wq);
      interruptible_sleep_on_timeout(&thr_wq, 2*HZ);
      DEBUG(3, "blkmtd: writetask: woken up\n");
      if(write_task_finish)
	break;
    } else {
      /* we have stuff to write */
      mtdblkdev_write_queue_t *item = &write_queue[write_queue_tail];
      struct page **pages = item->pages;
      int pagecnt = item->pagecnt;
      int pagenr = item->pagenr;
      int i;
      int max_sectors = KIO_MAX_SECTORS >> (item->rawdevice->sector_bits - 9);
      kdev_t dev = to_kdev_t(item->rawdevice->binding->bd_dev);
  

      DEBUG(3, "blkmtd: writetask: got %d queue items\n", write_queue_cnt);
      set_current_state(TASK_RUNNING);
      spin_unlock(&mbd_writeq_lock);

      DEBUG(2, "blkmtd: write_task: writing pagenr = %d pagecnt = %d", 
	    item->pagenr, item->pagecnt);

      iobuf->offset = 0;
      iobuf->locked = 1;

      /* Loop through all the pages to be written in the queue item, remembering
	 we can only write KIO_MAX_SECTORS at a time */
	 
      while(pagecnt) {
	int sectornr = pagenr << (PAGE_SHIFT - item->rawdevice->sector_bits);
	int sectorcnt = pagecnt << (PAGE_SHIFT - item->rawdevice->sector_bits);
	int cursectors = (sectorcnt < max_sectors) ? sectorcnt : max_sectors;
	int cpagecnt = (cursectors << item->rawdevice->sector_bits) + PAGE_SIZE-1;
	cpagecnt >>= PAGE_SHIFT;
	
	for(i = 0; i < cpagecnt; i++) 
	    iobuf->maplist[i] = *(pages++);
	
	for(i = 0; i < cursectors; i++) {
	  iobuf->blocks[i] = sectornr++;
	}
	
	iobuf->nr_pages = cpagecnt;
	iobuf->length = cursectors << item->rawdevice->sector_bits;
	DEBUG(3, "blkmtd: write_task: about to kiovec\n");
	err = brw_kiovec(WRITE, 1, &iobuf, dev, iobuf->blocks, item->rawdevice->sector_size);
	DEBUG(3, "bklmtd: write_task: done, err = %d\n", err);
	if(err != (cursectors << item->rawdevice->sector_bits)) {
	  /* if an error occured - set this to exit the loop */
	  pagecnt = 0;
	} else {
	  pagenr += cpagecnt;
	  pagecnt -= cpagecnt;
	}
      }

      /* free up the pages used in the write and list of pages used in the write
	 queue item */
      iobuf->locked = 0;
      spin_lock(&mbd_writeq_lock);
      write_queue_cnt--;
      write_queue_tail++;
      write_queue_tail %= WRITE_QUEUE_SZ;
      for(i = 0 ; i < item->pagecnt; i++) {
	UnlockPage(item->pages[i]);
	__free_pages(item->pages[i], 0);
      }
      kfree(item->pages);
      item->pages = NULL;
      spin_unlock(&mbd_writeq_lock);
      /* Tell others there is some space in the write queue */
      if(waitqueue_active(&mtbd_sync_wq))
	wake_up(&mtbd_sync_wq);
    }
  }
  remove_wait_queue(&thr_wq, &wait);
  DEBUG(1, "blkmtd: writetask: exiting\n");
  free_kiovec(1, &iobuf);
  /* Tell people we have exitd */
  up(&thread_sem);
  return 0;
}


/* Add a range of pages into the outgoing write queue, making copies of them */
static int queue_page_write(mtd_raw_dev_data_t *rawdevice, struct page **pages,
			    int pagenr, int pagecnt, int iserase)
{
  struct page *outpage;
  struct page **new_pages;
  mtdblkdev_write_queue_t *item;
  int i;
  DECLARE_WAITQUEUE(wait, current);
  DEBUG(2, "mtdblkdev: queue_page_write: adding pagenr = %d pagecnt = %d\n", pagenr, pagecnt);

  if(!pagecnt)
    return 0;

  if(pages == NULL)
    return -EINVAL;

  /* create a array for the list of pages */
  new_pages = kmalloc(pagecnt * sizeof(struct page *), GFP_KERNEL);
  if(new_pages == NULL)
    return -ENOMEM;

  /* make copies of the pages in the page cache */
  for(i = 0; i < pagecnt; i++) {
    outpage = alloc_pages(GFP_KERNEL, 0);
    if(!outpage) {
      while(i--) {
	UnlockPage(new_pages[i]);
	__free_pages(new_pages[i], 0);
      }
      kfree(new_pages);
      return -ENOMEM;
    }
    lock_page(outpage);
    memcpy(page_address(outpage), page_address(pages[i]), PAGE_SIZE);
    new_pages[i] = outpage;
  }

  /* wait until there is some space in the write queue */
 test_lock:
  spin_lock(&mbd_writeq_lock);
  if(write_queue_cnt == WRITE_QUEUE_SZ) {
    spin_unlock(&mbd_writeq_lock);
    DEBUG(3, "blkmtd: queue_page: Queue full\n");
    current->state = TASK_UNINTERRUPTIBLE;
    add_wait_queue(&mtbd_sync_wq, &wait);
    wake_up_interruptible(&thr_wq);
    schedule();
    current->state = TASK_RUNNING;
    remove_wait_queue(&mtbd_sync_wq, &wait);
    DEBUG(3, "blkmtd: queue_page: Queue has %d items in it\n", write_queue_cnt);
    goto test_lock;
  }

  DEBUG(3, "blkmtd: queue_write_page: qhead: %d qtail: %d qcnt: %d\n", 
	write_queue_head, write_queue_tail, write_queue_cnt);

  /* fix up the queue item */
  item = &write_queue[write_queue_head];
  item->pages = new_pages;
  item->pagenr = pagenr;
  item->pagecnt = pagecnt;
  item->rawdevice = rawdevice;
  item->iserase = iserase;

  write_queue_head++;
  write_queue_head %= WRITE_QUEUE_SZ;
  write_queue_cnt++;
  DEBUG(3, "blkmtd: queue_write_page: qhead: %d qtail: %d qcnt: %d\n", 
	write_queue_head, write_queue_tail, write_queue_cnt);
  spin_unlock(&mbd_writeq_lock);
  DEBUG(2, "blkmtd: queue_page_write: finished\n");
  return 0;
}


/* erase a specified part of the device */
static int blkmtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
  mtd_raw_dev_data_t *rawdevice = mtd->priv;
  size_t from;
  u_long len;
  int err = 0;

  /* check readonly */
  if(rawdevice->readonly) {
    printk("blkmtd: error: trying to erase readonly device %s\n", device);
    instr->state = MTD_ERASE_FAILED;
    goto erase_callback;
  }

  instr->state = MTD_ERASING;
  from = instr->addr;
  len = instr->len;

  /* check page alignment of start and length */
  DEBUG(2, "blkmtd: erase: dev = `%s' from = %d len = %ld\n",
	bdevname(rawdevice->binding->bd_dev), from, len);
  if(from % PAGE_SIZE) {
    printk("blkmtd: erase: addr not page aligned (addr = %d)\n", from);
    instr->state = MTD_ERASE_FAILED;
    err = -EIO;
  }

  if(len % PAGE_SIZE) {
    printk("blkmtd: erase: len not a whole number of pages (len = %ld)\n", len);
    instr->state = MTD_ERASE_FAILED;
    err = -EIO;
  }
  
  if(instr->state != MTD_ERASE_FAILED) {
    /* start the erase */
    int pagenr, pagecnt;
    struct page *page, **pages;
    int i = 0;

    pagenr = from >> PAGE_SHIFT;
    pagecnt = len >> PAGE_SHIFT;
    DEBUG(3, "blkmtd: erase: pagenr = %d pagecnt = %d\n", pagenr, pagecnt);
    pages = kmalloc(pagecnt * sizeof(struct page *), GFP_KERNEL);
    if(pages == NULL) {
      err = -ENOMEM;
      instr->state = MTD_ERASE_FAILED;
      goto erase_out;
    }

    while(pagecnt) {
      /* get the page via the page cache */
      DEBUG(3, "blkmtd: erase: doing grap_cache_page() for page %d\n", pagenr);
      page = grab_cache_page(&rawdevice->as, pagenr);
      if(!page) {
	DEBUG(3, "blkmtd: erase: grab_cache_page() failed for page %d\n", pagenr);
	kfree(pages);
	err = -EIO;
	instr->state = MTD_ERASE_FAILED;
	goto erase_out;
      }
      memset(page_address(page), 0xff, PAGE_SIZE);
      pages[i] = page;
      pagecnt--;
      pagenr++;
      i++;
    }
    DEBUG(3, "blkmtd: erase: queuing page write\n");
    err = queue_page_write(rawdevice, pages, from >> PAGE_SHIFT, len >> PAGE_SHIFT, 1);
    pagecnt = len >> PAGE_SHIFT;
    if(!err) {
      while(pagecnt--) {
	SetPageUptodate(pages[pagecnt]);
	UnlockPage(pages[pagecnt]);
	page_cache_release(pages[pagecnt]);
	flush_dcache_page(pages[pagecnt]);
      }
      kfree(pages);
      instr->state = MTD_ERASE_DONE;
    } else {
      while(pagecnt--) {
	SetPageError(pages[pagecnt]);
	page_cache_release(pages[pagecnt]);
      }
      kfree(pages);
      instr->state = MTD_ERASE_FAILED;
    }
  }
 erase_out:
  DEBUG(3, "blkmtd: erase: checking callback\n");
 erase_callback:
  if (instr->callback) {
    (*(instr->callback))(instr);
  }
  DEBUG(2, "blkmtd: erase: finished (err = %d)\n", err);
  return err;
}


/* read a range of the data via the page cache */
static int blkmtd_read(struct mtd_info *mtd, loff_t from, size_t len,
	     size_t *retlen, u_char *buf)
{
  mtd_raw_dev_data_t *rawdevice = mtd->priv;
  int err = 0;
  int offset;
  int pagenr, pages;

  *retlen = 0;

  DEBUG(2, "blkmtd: read: dev = `%s' from = %ld len = %d buf = %p\n",
	bdevname(rawdevice->binding->bd_dev), (long int)from, len, buf);

  pagenr = from >> PAGE_SHIFT;
  offset = from - (pagenr << PAGE_SHIFT);
  
  pages = (offset+len+PAGE_SIZE-1) >> PAGE_SHIFT;
  DEBUG(3, "blkmtd: read: pagenr = %d offset = %d, pages = %d\n", pagenr, offset, pages);

  /* just loop through each page, getting it via readpage() - slow but easy */
  while(pages) {
    struct page *page;
    int cpylen;
    DEBUG(3, "blkmtd: read: looking for page: %d\n", pagenr);
    page = read_cache_page(&rawdevice->as, pagenr, (filler_t *)blkmtd_readpage, rawdevice->file);
    if(IS_ERR(page)) {
      return PTR_ERR(page);
    }
    wait_on_page(page);
    if(!Page_Uptodate(page)) {
      /* error reading page */
      printk("blkmtd: read: page not uptodate\n");
      page_cache_release(page);
      return -EIO;
    }

    cpylen = (PAGE_SIZE > len) ? len : PAGE_SIZE;
    if(offset+cpylen > PAGE_SIZE)
      cpylen = PAGE_SIZE-offset;
    
    memcpy(buf + *retlen, page_address(page) + offset, cpylen);
    offset = 0;
    len -= cpylen;
    *retlen += cpylen;
    pagenr++;
    pages--;
    page_cache_release(page);
  }
  
  DEBUG(2, "blkmtd: end read: retlen = %d, err = %d\n", *retlen, err);
  return err;
}

    
/* write a range of the data via the page cache.
 *
 * Basic operation. break the write into three parts. 
 *
 * 1. From a page unaligned start up until the next page boundary
 * 2. Page sized, page aligned blocks
 * 3. From end of last aligned block to end of range
 *
 * 1,3 are read via the page cache and readpage() since these are partial
 * pages, 2 we just grab pages from the page cache, not caring if they are
 * already in memory or not since they will be completly overwritten.
 *
 */
 
static int blkmtd_write(struct mtd_info *mtd, loff_t to, size_t len,
	      size_t *retlen, const u_char *buf)
{
  mtd_raw_dev_data_t *rawdevice = mtd->priv;
  int err = 0;
  int offset;
  int pagenr;
  size_t len1 = 0, len2 = 0, len3 = 0;
  struct page **pages;
  int pagecnt = 0;

  *retlen = 0;
  DEBUG(2, "blkmtd: write: dev = `%s' to = %ld len = %d buf = %p\n",
	bdevname(rawdevice->binding->bd_dev), (long int)to, len, buf);

  /* handle readonly and out of range numbers */

  if(rawdevice->readonly) {
    printk("blkmtd: error: trying to write to a readonly device %s\n", device);
    return -EROFS;
  }

  if(to >= rawdevice->totalsize) {
    return -ENOSPC;
  }

  if(to + len > rawdevice->totalsize) {
    len = (rawdevice->totalsize - to);
  }


  pagenr = to >> PAGE_SHIFT;
  offset = to - (pagenr << PAGE_SHIFT);

  /* see if we have to do a partial write at the start */
  if(offset) {
    if((offset + len) > PAGE_SIZE) {
      len1 = PAGE_SIZE - offset;
      len -= len1;
    } else {
      len1 = len;
      len = 0;
    }
  }

  /* calculate the length of the other two regions */
  len3 = len & ~PAGE_MASK;
  len -= len3;
  len2 = len;


  if(len1)
    pagecnt++;
  if(len2)
    pagecnt += len2 >> PAGE_SHIFT;
  if(len3)
    pagecnt++;

  DEBUG(3, "blkmtd: write: len1 = %d len2 = %d len3 = %d pagecnt = %d\n", len1, len2, len3, pagecnt);
  
  /* get space for list of pages */
  pages = kmalloc(pagecnt * sizeof(struct page *), GFP_KERNEL);
  if(pages == NULL) {
    return -ENOMEM;
  }
  pagecnt = 0;

  if(len1) {
    /* do partial start region */
    struct page *page;
    
    DEBUG(3, "blkmtd: write: doing partial start, page = %d len = %d offset = %d\n", pagenr, len1, offset);
    page = read_cache_page(&rawdevice->as, pagenr, (filler_t *)blkmtd_readpage, rawdevice->file);

    if(IS_ERR(page)) {
      kfree(pages);
      return PTR_ERR(page);
    }
    memcpy(page_address(page)+offset, buf, len1);
    pages[pagecnt++] = page;
    buf += len1;
    *retlen = len1;
    err = 0;
    pagenr++;
  }

  /* Now do the main loop to a page aligned, n page sized output */
  if(len2) {
    int pagesc = len2 >> PAGE_SHIFT;
    DEBUG(3, "blkmtd: write: whole pages start = %d, count = %d\n", pagenr, pagesc);
    while(pagesc) {
      struct page *page;

      /* see if page is in the page cache */
      DEBUG(3, "blkmtd: write: grabbing page %d from page cache\n", pagenr);
      page = grab_cache_page(&rawdevice->as, pagenr);
      DEBUG(3, "blkmtd: write: got page %d from page cache\n", pagenr);
      if(!page) {
	printk("blkmtd: write: cant grab cache page %d\n", pagenr);
	err = -EIO;
	goto write_err;
      }
      memcpy(page_address(page), buf, PAGE_SIZE);
      pages[pagecnt++] = page;
      UnlockPage(page);
      pagenr++;
      pagesc--;
      buf += PAGE_SIZE;
      *retlen += PAGE_SIZE;
    }
  }


  if(len3) {
    /* do the third region */
    struct page *page;
    DEBUG(3, "blkmtd: write: doing partial end, page = %d len = %d\n", pagenr, len3);
    page = read_cache_page(&rawdevice->as, pagenr, (filler_t *)blkmtd_readpage, rawdevice->file);
    if(IS_ERR(page)) {
      err = PTR_ERR(page);
      goto write_err;
    }
    memcpy(page_address(page), buf, len3);
    DEBUG(3, "blkmtd: write: writing out partial end\n");
    pages[pagecnt++] = page;
    *retlen += len3;
    err = 0;
  }
  DEBUG(2, "blkmtd: write: end, retlen = %d, err = %d\n", *retlen, err);
  /* submit it to the write task */
  err = queue_page_write(rawdevice, pages, to >> PAGE_SHIFT, pagecnt, 0);
  if(!err) {
    while(pagecnt--) {
      SetPageUptodate(pages[pagecnt]);
      flush_dcache_page(pages[pagecnt]);
      page_cache_release(pages[pagecnt]);
    }
    kfree(pages);
    return 0;
  }

 write_err:
  while(--pagecnt) {
    SetPageError(pages[pagecnt]);
    page_cache_release(pages[pagecnt]);
  }
  kfree(pages);
  return err;
}


/* sync the device - wait until the write queue is empty */
static void blkmtd_sync(struct mtd_info *mtd)
{
  DECLARE_WAITQUEUE(wait, current);
  DEBUG(2, "blkmtd: sync: called\n");

 stuff_inq:
  spin_lock(&mbd_writeq_lock);
  if(write_queue_cnt) {
    spin_unlock(&mbd_writeq_lock);
    current->state = TASK_UNINTERRUPTIBLE;
    add_wait_queue(&mtbd_sync_wq, &wait);
    DEBUG(3, "blkmtd: sync: waking up task\n");
    wake_up_interruptible(&thr_wq);
    schedule();
    current->state = TASK_RUNNING;
    remove_wait_queue(&mtbd_sync_wq, &wait);
    DEBUG(3, "blkmtd: sync: waking up after write task\n");
    goto stuff_inq;
  }
  spin_unlock(&mbd_writeq_lock);

  DEBUG(2, "blkmtdL sync: finished\n");
}

/* Cleanup and exit - sync the device and kill of the kernel thread */
static void __exit cleanup_blkmtd(void)
{
  if (mtd_info) {
    mtd_raw_dev_data_t *rawdevice = mtd_info->priv;
    // sync the device
    if (rawdevice) {
      blkmtd_sync(mtd_info);
      write_task_finish = 1;
      wake_up_interruptible(&thr_wq);
      down(&thread_sem);
      if(rawdevice->binding != NULL)
	blkdev_put(rawdevice->binding, BDEV_RAW);
      filp_close(rawdevice->file, NULL);
      kfree(mtd_info->priv);
    }
    if(mtd_info->eraseregions)
      kfree(mtd_info->eraseregions);
    del_mtd_device(mtd_info);
    kfree(mtd_info);
    mtd_info = NULL;
  }
  printk("blkmtd: unloaded for %s\n", device);
}

extern struct module __this_module;

/* for a given size and initial erase size, calculate the number and size of each
   erase region */
static int __init calc_erase_regions(struct mtd_erase_region_info *info, size_t erase_size, size_t total_size)
{
  int count = 0;
  int offset = 0;
  int regions = 0;

   while(total_size) {
     count = total_size / erase_size;
     if(count) {
       total_size = total_size % erase_size;
       if(info) {
	 info->offset = offset;
	 info->erasesize = erase_size;
	 info->numblocks = count;
	 info++;
       }
       offset += (count * erase_size);
       regions++;
     }
     while(erase_size > total_size)
       erase_size >>= 1;
   }
   return regions;
}


/* Startup */
static int __init init_blkmtd(void)
{
  struct file *file = NULL;
  struct inode *inode;
  mtd_raw_dev_data_t *rawdevice = NULL;
  int maj, min;
  int i, blocksize, blocksize_bits;
  loff_t size = 0;
  int readonly = 0;
  int erase_size = CONFIG_MTD_BLKDEV_ERASESIZE;
  kdev_t rdev;
  int err;
  int mode;
  int totalsize = 0, total_sectors = 0;
  int regions;

  mtd_info = NULL;

  // Check args
  if(device == 0) {
    printk("blkmtd: error, missing `device' name\n");
    return 1;
  }

  if(ro)
    readonly = 1;

  if(erasesz)
    erase_size = erasesz;

  DEBUG(1, "blkmtd: got device = `%s' erase size = %dK readonly = %s\n", device, erase_size, readonly ? "yes" : "no");
  // Get a handle on the device
  mode = (readonly) ? O_RDONLY : O_RDWR;
  file = filp_open(device, mode, 0);
  if(IS_ERR(file)) {
    DEBUG(2, "blkmtd: open_namei returned %ld\n", PTR_ERR(file));
    return 1;
  }
  
  /* determine is this is a block device and if so get its major and minor
     numbers */
  inode = file->f_dentry->d_inode;
  if(!S_ISBLK(inode->i_mode)) {
    printk("blkmtd: %s not a block device\n", device);
    filp_close(file, NULL);
    return 1;
  }
  rdev = inode->i_rdev;
  //filp_close(file, NULL);
  DEBUG(1, "blkmtd: found a block device major = %d, minor = %d\n",
	 MAJOR(rdev), MINOR(rdev));
  maj = MAJOR(rdev);
  min = MINOR(rdev);

  if(maj == MTD_BLOCK_MAJOR) {
    printk("blkmtd: attempting to use an MTD device as a block device\n");
    return 1;
  }

  DEBUG(1, "blkmtd: devname = %s\n", bdevname(rdev));
  blocksize = BLOCK_SIZE;

  if(bs) {
    blocksize = bs;
  } else {
    if (blksize_size[maj] && blksize_size[maj][min]) {
      DEBUG(2, "blkmtd: blksize_size = %d\n", blksize_size[maj][min]);
      blocksize = blksize_size[maj][min];
    }
  }
  i = blocksize;
  blocksize_bits = 0;
  while(i != 1) {
    blocksize_bits++;
    i >>= 1;
  }

  if(count) {
    size = count;
  } else {
    if (blk_size[maj]) {
      size = ((loff_t) blk_size[maj][min] << BLOCK_SIZE_BITS) >> blocksize_bits;
    }
  }
  total_sectors = size;
  size *= blocksize;
  totalsize = size;
  DEBUG(1, "blkmtd: size = %ld\n", (long int)size);

  if(size == 0) {
    printk("blkmtd: cant determine size\n");
    return 1;
  }
  rawdevice = (mtd_raw_dev_data_t *)kmalloc(sizeof(mtd_raw_dev_data_t), GFP_KERNEL);
  if(rawdevice == NULL) {
    err = -ENOMEM;
    goto init_err;
  }
  memset(rawdevice, 0, sizeof(mtd_raw_dev_data_t));
  // get the block device
  rawdevice->binding = bdget(kdev_t_to_nr(MKDEV(maj, min)));
  err = blkdev_get(rawdevice->binding, mode, 0, BDEV_RAW);
  if (err) {
    goto init_err;
  }
  rawdevice->totalsize = totalsize;
  rawdevice->total_sectors = total_sectors;
  rawdevice->sector_size = blocksize;
  rawdevice->sector_bits = blocksize_bits;
  rawdevice->readonly = readonly;

  DEBUG(2, "sector_size = %d, sector_bits = %d\n", rawdevice->sector_size, rawdevice->sector_bits);

  mtd_info = (struct mtd_info *)kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
  if (mtd_info == NULL) {
    err = -ENOMEM;
    goto init_err;
  }
  memset(mtd_info, 0, sizeof(*mtd_info));

  // Setup the MTD structure
  mtd_info->name = "blkmtd block device";
  if(readonly) {
    mtd_info->type = MTD_ROM;
    mtd_info->flags = MTD_CAP_ROM;
    mtd_info->erasesize = erase_size << 10;
  } else {
    mtd_info->type = MTD_RAM;
    mtd_info->flags = MTD_CAP_RAM;
    mtd_info->erasesize = erase_size << 10;
  }
  mtd_info->size = size;
  mtd_info->erase = blkmtd_erase;
  mtd_info->read = blkmtd_read;
  mtd_info->write = blkmtd_write;
  mtd_info->sync = blkmtd_sync;
  mtd_info->point = 0;
  mtd_info->unpoint = 0;

  mtd_info->priv = rawdevice;
  regions = calc_erase_regions(NULL, erase_size << 10, size);
  DEBUG(1, "blkmtd: init: found %d erase regions\n", regions);
  mtd_info->eraseregions = kmalloc(regions * sizeof(struct mtd_erase_region_info), GFP_KERNEL);
  if(mtd_info->eraseregions == NULL) {
  }
  mtd_info->numeraseregions = regions;
  calc_erase_regions(mtd_info->eraseregions, erase_size << 10, size);

  /* setup the page cache info */
  INIT_LIST_HEAD(&rawdevice->as.clean_pages);
  INIT_LIST_HEAD(&rawdevice->as.dirty_pages);
  INIT_LIST_HEAD(&rawdevice->as.locked_pages);
  rawdevice->as.nrpages = 0;
  rawdevice->as.a_ops = &blkmtd_aops;
  rawdevice->as.host = inode;
  rawdevice->as.i_mmap = NULL;
  rawdevice->as.i_mmap_shared = NULL;
  spin_lock_init(&rawdevice->as.i_shared_lock);
  rawdevice->as.gfp_mask = GFP_KERNEL;
  rawdevice->file = file;

  file->private_data = rawdevice;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
   mtd_info->module = THIS_MODULE;			
#endif
   if (add_mtd_device(mtd_info)) {
     err = -EIO;
     goto init_err;
   }
   init_waitqueue_head(&thr_wq);
   init_waitqueue_head(&mtbd_sync_wq);
   DEBUG(3, "blkmtd: init: kernel task @ %p\n", write_queue_task);
   DEBUG(2, "blkmtd: init: starting kernel task\n");
   kernel_thread(write_queue_task, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
   DEBUG(2, "blkmtd: init: started\n");
   printk("blkmtd loaded: version = %s using %s erase_size = %dK %s\n", VERSION, device, erase_size, (readonly) ? "(read-only)" : "");
   return 0;

 init_err:
   if(!rawdevice) {
     if(rawdevice->binding) 
       blkdev_put(rawdevice->binding, BDEV_RAW);

     kfree(rawdevice);
     rawdevice = NULL;
   }
   if(mtd_info) {
     if(mtd_info->eraseregions)
       kfree(mtd_info->eraseregions);
     kfree(mtd_info);
     mtd_info = NULL;
   }
   return err;
}

module_init(init_blkmtd);
module_exit(cleanup_blkmtd);
