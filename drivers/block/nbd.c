/*
 * Network block device - make block devices work over TCP
 *
 * Note that you can not swap over this thing, yet. Seems to work but
 * deadlocks sometimes - you can not swap over TCP in general.
 * 
 * Copyright 1997-2000 Pavel Machek <pavel@ucw.cz>
 * Parts copyright 2001 Steven Whitehouse <steve@chygwyn.com>
 *
 * (part of code stolen from loop.c)
 *
 * 97-3-25 compiled 0-th version, not yet tested it 
 *   (it did not work, BTW) (later that day) HEY! it works!
 *   (bit later) hmm, not that much... 2:00am next day:
 *   yes, it works, but it gives something like 50kB/sec
 * 97-4-01 complete rewrite to make it possible for many requests at 
 *   once to be processed
 * 97-4-11 Making protocol independent of endianity etc.
 * 97-9-13 Cosmetic changes
 * 98-5-13 Attempt to make 64-bit-clean on 64-bit machines
 * 99-1-11 Attempt to make 64-bit-clean on 32-bit machines <ankry@mif.pg.gda.pl>
 * 01-2-27 Fix to store proper blockcount for kernel (calculated using
 *   BLOCK_SIZE_BITS, not device blocksize) <aga@permonline.ru>
 * 01-3-11 Make nbd work with new Linux block layer code. It now supports
 *   plugging like all the other block devices. Also added in MSG_MORE to
 *   reduce number of partial TCP segments sent. <steve@chygwyn.com>
 * 01-12-6 Fix deadlock condition by making queue locks independent of
 *   the transmit lock. <steve@chygwyn.com>
 * 02-10-11 Allow hung xmit to be aborted via SIGKILL & various fixes.
 *   <Paul.Clements@SteelEye.com> <James.Bottomley@SteelEye.com>
 *
 * possible FIXME: make set_sock / set_blksize / set_size / do_it one syscall
 * why not: would need verify_area and friends, would share yet another 
 *          structure with userland
 */

#define PARANOIA
#include <linux/major.h>

#include <linux/blk.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/ioctl.h>
#include <linux/blkdev.h>
#include <linux/blk.h>
#include <net/sock.h>

#include <linux/devfs_fs_kernel.h>

#include <asm/uaccess.h>
#include <asm/types.h>

#include <linux/nbd.h>

#define LO_MAGIC 0x68797548

static struct nbd_device nbd_dev[MAX_NBD];

static spinlock_t nbd_lock = SPIN_LOCK_UNLOCKED;

#define DEBUG( s )
/* #define DEBUG( s ) printk( s ) 
 */

static int requests_in;
static int requests_out;

static void nbd_end_request(struct request *req)
{
	int uptodate = (req->errors == 0) ? 1 : 0;
	request_queue_t *q = req->q;
	unsigned long flags;

#ifdef PARANOIA
	requests_out++;
#endif
	spin_lock_irqsave(q->queue_lock, flags);
	if (!end_that_request_first(req, uptodate, req->nr_sectors)) {
		end_that_request_last(req);
	}
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static int nbd_open(struct inode *inode, struct file *file)
{
	struct nbd_device *lo = inode->i_bdev->bd_disk->private_data;
	lo->refcnt++;
	return 0;
}

/*
 *  Send or receive packet.
 */
static int nbd_xmit(int send, struct socket *sock, char *buf, int size, int msg_flags)
{
	mm_segment_t oldfs;
	int result;
	struct msghdr msg;
	struct iovec iov;
	unsigned long flags;
	sigset_t oldset;

	oldfs = get_fs();
	set_fs(get_ds());
	/* Allow interception of SIGKILL only
	 * Don't allow other signals to interrupt the transmission */
	spin_lock_irqsave(&current->sighand->siglock, flags);
	oldset = current->blocked;
	sigfillset(&current->blocked);
	sigdelsetmask(&current->blocked, sigmask(SIGKILL));
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, flags);


	do {
		sock->sk->sk_allocation = GFP_NOIO;
		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_namelen = 0;
		msg.msg_flags = msg_flags | MSG_NOSIGNAL;

		if (send)
			result = sock_sendmsg(sock, &msg, size);
		else
			result = sock_recvmsg(sock, &msg, size, 0);

		if (signal_pending(current)) {
			siginfo_t info;
			spin_lock_irqsave(&current->sighand->siglock, flags);
			printk(KERN_WARNING "NBD (pid %d: %s) got signal %d\n",
				current->pid, current->comm, 
				dequeue_signal(current, &current->blocked, &info));
			spin_unlock_irqrestore(&current->sighand->siglock, flags);
			result = -EINTR;
			break;
		}

		if (result <= 0) {
#ifdef PARANOIA
			printk(KERN_ERR "NBD: %s - sock=%ld at buf=%ld, size=%d returned %d.\n",
			       send ? "send" : "receive", (long) sock, (long) buf, size, result);
#endif
			break;
		}
		size -= result;
		buf += result;
	} while (size > 0);

	spin_lock_irqsave(&current->sighand->siglock, flags);
	current->blocked = oldset;
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, flags);

	set_fs(oldfs);
	return result;
}

#define FAIL( s ) { printk( KERN_ERR "NBD: " s "(result %d)\n", result ); goto error_out; }

void nbd_send_req(struct nbd_device *lo, struct request *req)
{
	int result, i, flags;
	struct nbd_request request;
	unsigned long size = req->nr_sectors << 9;
	struct socket *sock = lo->sock;

	DEBUG("NBD: sending control, ");
	
	request.magic = htonl(NBD_REQUEST_MAGIC);
	request.type = htonl(nbd_cmd(req));
	request.from = cpu_to_be64( (u64) req->sector << 9);
	request.len = htonl(size);
	memcpy(request.handle, &req, sizeof(req));

	down(&lo->tx_lock);

	if (!sock || !lo->sock) {
		printk(KERN_ERR "NBD: Attempted sendmsg to closed socket\n");
		goto error_out;
	}

	result = nbd_xmit(1, sock, (char *) &request, sizeof(request), nbd_cmd(req) == NBD_CMD_WRITE ? MSG_MORE : 0);
	if (result <= 0)
		FAIL("Sendmsg failed for control.");

	if (nbd_cmd(req) == NBD_CMD_WRITE) {
		struct bio *bio;
		/*
		 * we are really probing at internals to determine
		 * whether to set MSG_MORE or not...
		 */
		rq_for_each_bio(bio, req) {
			struct bio_vec *bvec;
			bio_for_each_segment(bvec, bio, i) {
				flags = 0;
				if ((i < (bio->bi_vcnt - 1)) || bio->bi_next)
					flags = MSG_MORE;
				DEBUG("data, ");
				result = nbd_xmit(1, sock, page_address(bvec->bv_page) + bvec->bv_offset, bvec->bv_len, flags);
				if (result <= 0)
					FAIL("Send data failed.");
			}
		}
	}
	up(&lo->tx_lock);
	return;

      error_out:
	up(&lo->tx_lock);
	req->errors++;
}

static struct request *nbd_find_request(struct nbd_device *lo, char *handle)
{
	struct request *req;
	struct list_head *tmp;
	struct request *xreq;

	memcpy(&xreq, handle, sizeof(xreq));

	spin_lock(&lo->queue_lock);
	list_for_each(tmp, &lo->queue_head) {
		req = list_entry(tmp, struct request, queuelist);
		if (req != xreq)
			continue;
		list_del_init(&req->queuelist);
		spin_unlock(&lo->queue_lock);
		return req;
	}
	spin_unlock(&lo->queue_lock);
	return NULL;
}

#define HARDFAIL( s ) { printk( KERN_ERR "NBD: " s "(result %d)\n", result ); lo->harderror = result; return NULL; }
struct request *nbd_read_stat(struct nbd_device *lo)
		/* NULL returned = something went wrong, inform userspace       */ 
{
	int result;
	struct nbd_reply reply;
	struct request *req;

	DEBUG("reading control, ");
	reply.magic = 0;
	result = nbd_xmit(0, lo->sock, (char *) &reply, sizeof(reply), MSG_WAITALL);
	if (result <= 0)
		HARDFAIL("Recv control failed.");
	req = nbd_find_request(lo, reply.handle);
	if (req == NULL)
		HARDFAIL("Unexpected reply");

	DEBUG("ok, ");
	if (ntohl(reply.magic) != NBD_REPLY_MAGIC)
		HARDFAIL("Not enough magic.");
	if (ntohl(reply.error))
		FAIL("Other side returned error.");

	if (nbd_cmd(req) == NBD_CMD_READ) {
		struct bio *bio = req->bio;
		DEBUG("data, ");
		do {
			result = nbd_xmit(0, lo->sock, bio_data(bio), bio->bi_size, MSG_WAITALL);
			if (result <= 0)
				HARDFAIL("Recv data failed.");
			bio = bio->bi_next;
		} while(bio);
	}
	DEBUG("done.\n");
	return req;

/* Can we get here? Yes, if other side returns error */
      error_out:
	req->errors++;
	return req;
}

void nbd_do_it(struct nbd_device *lo)
{
	struct request *req;

	while (1) {
		req = nbd_read_stat(lo);

		if (!req) {
			printk(KERN_ALERT "req should never be null\n" );
			goto out;
		}
		BUG_ON(lo->magic != LO_MAGIC);
		nbd_end_request(req);
	}
 out:
	return;
}

void nbd_clear_que(struct nbd_device *lo)
{
	struct request *req;

	BUG_ON(lo->magic != LO_MAGIC);

	do {
		req = NULL;
		spin_lock(&lo->queue_lock);
		if (!list_empty(&lo->queue_head)) {
			req = list_entry(lo->queue_head.next, struct request, queuelist);
			list_del_init(&req->queuelist);
		}
		spin_unlock(&lo->queue_lock);
		if (req) {
			req->errors++;
			nbd_end_request(req);
		}
	} while(req);
}

/*
 * We always wait for result of write, for now. It would be nice to make it optional
 * in future
 * if ((req->cmd == WRITE) && (lo->flags & NBD_WRITE_NOCHK)) 
 *   { printk( "Warning: Ignoring result!\n"); nbd_end_request( req ); }
 */

#undef FAIL
#define FAIL( s ) { printk( KERN_ERR "%s: " s "\n", req->rq_disk->disk_name ); goto error_out; }

static void do_nbd_request(request_queue_t * q)
{
	struct request *req;
	
	while ((req = elv_next_request(q)) != NULL) {
		struct nbd_device *lo;

		if (!(req->flags & REQ_CMD))
			goto error_out;

		lo = req->rq_disk->private_data;
		if (!lo->file)
			FAIL("Request when not-ready.");
		nbd_cmd(req) = NBD_CMD_READ;
		if (rq_data_dir(req) == WRITE) {
			nbd_cmd(req) = NBD_CMD_WRITE;
			if (lo->flags & NBD_READ_ONLY)
				FAIL("Write on read-only");
		}
		BUG_ON(lo->magic != LO_MAGIC);
		requests_in++;

		req->errors = 0;
		blkdev_dequeue_request(req);
		spin_unlock_irq(q->queue_lock);

		spin_lock(&lo->queue_lock);

		if (!lo->file) {
			spin_unlock(&lo->queue_lock);
			printk(KERN_ERR "nbd: failed between accept and semaphore, file lost\n");
			req->errors++;
			nbd_end_request(req);
			spin_lock_irq(q->queue_lock);
			continue;
		}

		list_add(&req->queuelist, &lo->queue_head);
		spin_unlock(&lo->queue_lock);

		nbd_send_req(lo, req);

		if (req->errors) {
			printk(KERN_ERR "nbd: nbd_send_req failed\n");
			spin_lock(&lo->queue_lock);
			list_del_init(&req->queuelist);
			spin_unlock(&lo->queue_lock);
			nbd_end_request(req);
			spin_lock_irq(q->queue_lock);
			continue;
		}

		spin_lock_irq(q->queue_lock);
		continue;

	      error_out:
		req->errors++;
		blkdev_dequeue_request(req);
		spin_unlock(q->queue_lock);
		nbd_end_request(req);
		spin_lock(q->queue_lock);
	}
	return;
}

static int nbd_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct nbd_device *lo = inode->i_bdev->bd_disk->private_data;
	int error, temp;
	struct request sreq ;

	/* Anyone capable of this syscall can do *real bad* things */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	switch (cmd) {
	case NBD_DISCONNECT:
	        printk(KERN_INFO "NBD_DISCONNECT\n");
		sreq.flags = REQ_SPECIAL;
		nbd_cmd(&sreq) = NBD_CMD_DISC;
                if (!lo->sock)
			return -EINVAL;
                nbd_send_req(lo, &sreq);
                return 0 ;
 
	case NBD_CLEAR_SOCK:
		nbd_clear_que(lo);
		spin_lock(&lo->queue_lock);
		if (!list_empty(&lo->queue_head)) {
			spin_unlock(&lo->queue_lock);
			printk(KERN_ERR "nbd: Some requests are in progress -> can not turn off.\n");
			return -EBUSY;
		}
		file = lo->file;
		if (!file) {
			spin_unlock(&lo->queue_lock);
			return -EINVAL;
		}
		lo->file = NULL;
		lo->sock = NULL;
		spin_unlock(&lo->queue_lock);
		fput(file);
		return 0;
	case NBD_SET_SOCK:
		if (lo->file)
			return -EBUSY;
		error = -EINVAL;
		file = fget(arg);
		if (file) {
			inode = file->f_dentry->d_inode;
			if (inode->i_sock) {
				lo->file = file;
				lo->sock = SOCKET_I(inode);
				error = 0;
			} else {
				fput(file);
			}
		}
		return error;
	case NBD_SET_BLKSIZE:
		if ((arg & (arg-1)) || (arg < 512) || (arg > PAGE_SIZE))
			return -EINVAL;
		lo->blksize = arg;
		temp = arg >> 9;
		lo->blksize_bits = 9;
		while (temp > 1) {
			lo->blksize_bits++;
			temp >>= 1;
		}
		lo->bytesize &= ~(lo->blksize-1); 
		set_capacity(lo->disk, lo->bytesize >> 9);
		return 0;
	case NBD_SET_SIZE:
		lo->bytesize = arg & ~(lo->blksize-1); 
		set_capacity(lo->disk, lo->bytesize >> 9);
		return 0;
	case NBD_SET_SIZE_BLOCKS:
		lo->bytesize = ((u64) arg) << lo->blksize_bits;
		set_capacity(lo->disk, lo->bytesize >> 9);
		return 0;
	case NBD_DO_IT:
		if (!lo->file)
			return -EINVAL;
		nbd_do_it(lo);
		/* on return tidy up in case we have a signal */
		/* Forcibly shutdown the socket causing all listeners
		 * to error
		 *
		 * FIXME: This code is duplicated from sys_shutdown, but
		 * there should be a more generic interface rather than
		 * calling socket ops directly here */
		down(&lo->tx_lock);
		printk(KERN_WARNING "nbd: shutting down socket\n");
		lo->sock->ops->shutdown(lo->sock, SEND_SHUTDOWN|RCV_SHUTDOWN);
		lo->sock = NULL;
		up(&lo->tx_lock);
		spin_lock(&lo->queue_lock);
		file = lo->file;
		lo->file = NULL;
		spin_unlock(&lo->queue_lock);
		nbd_clear_que(lo);
		printk(KERN_WARNING "nbd: queue cleared\n");
		if (file)
			fput(file);
		return lo->harderror;
	case NBD_CLEAR_QUE:
		nbd_clear_que(lo);
		return 0;
#ifdef PARANOIA
	case NBD_PRINT_DEBUG:
		printk(KERN_INFO "%s: next = %p, prev = %p. Global: in %d, out %d\n",
		       inode->i_bdev->bd_disk->disk_name, lo->queue_head.next,
		       lo->queue_head.prev, requests_in, requests_out);
		return 0;
#endif
	}
	return -EINVAL;
}

static int nbd_release(struct inode *inode, struct file *file)
{
	struct nbd_device *lo = inode->i_bdev->bd_disk->private_data;
	if (lo->refcnt <= 0)
		printk(KERN_ALERT "nbd_release: refcount(%d) <= 0\n", lo->refcnt);
	lo->refcnt--;
	/* N.B. Doesn't lo->file need an fput?? */
	return 0;
}

static struct block_device_operations nbd_fops =
{
	.owner =	THIS_MODULE,
	.open =		nbd_open,
	.release =	nbd_release,
	.ioctl =	nbd_ioctl,
};

/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

static struct request_queue nbd_queue;

static int __init nbd_init(void)
{
	int err = -ENOMEM;
	int i;

	if (sizeof(struct nbd_request) != 28) {
		printk(KERN_CRIT "Sizeof nbd_request needs to be 28 in order to work!\n" );
		return -EIO;
	}

	for (i = 0; i < MAX_NBD; i++) {
		struct gendisk *disk = alloc_disk(1);
		if (!disk)
			goto out;
		nbd_dev[i].disk = disk;
	}

	if (register_blkdev(NBD_MAJOR, "nbd")) {
		err = -EIO;
		goto out;
	}
#ifdef MODULE
	printk("nbd: registered device at major %d\n", NBD_MAJOR);
#endif
	blk_init_queue(&nbd_queue, do_nbd_request, &nbd_lock);
	devfs_mk_dir("nbd");
	for (i = 0; i < MAX_NBD; i++) {
		struct gendisk *disk = nbd_dev[i].disk;
		nbd_dev[i].refcnt = 0;
		nbd_dev[i].file = NULL;
		nbd_dev[i].magic = LO_MAGIC;
		nbd_dev[i].flags = 0;
		spin_lock_init(&nbd_dev[i].queue_lock);
		INIT_LIST_HEAD(&nbd_dev[i].queue_head);
		init_MUTEX(&nbd_dev[i].tx_lock);
		nbd_dev[i].blksize = 1024;
		nbd_dev[i].blksize_bits = 10;
		nbd_dev[i].bytesize = ((u64)0x7ffffc00) << 10; /* 2TB */
		disk->major = NBD_MAJOR;
		disk->first_minor = i;
		disk->fops = &nbd_fops;
		disk->private_data = &nbd_dev[i];
		disk->queue = &nbd_queue;
		sprintf(disk->disk_name, "nbd%d", i);
		sprintf(disk->devfs_name, "nbd/%d", i);
		set_capacity(disk, 0x3ffffe);
		add_disk(disk);
	}

	return 0;
out:
	while (i--)
		put_disk(nbd_dev[i].disk);
	return err;
}

static void __exit nbd_cleanup(void)
{
	int i;
	for (i = 0; i < MAX_NBD; i++) {
		del_gendisk(nbd_dev[i].disk);
		put_disk(nbd_dev[i].disk);
	}
	devfs_remove("nbd");
	blk_cleanup_queue(&nbd_queue);
	unregister_blkdev(NBD_MAJOR, "nbd");
}

module_init(nbd_init);
module_exit(nbd_cleanup);

MODULE_DESCRIPTION("Network Block Device");
MODULE_LICENSE("GPL");


