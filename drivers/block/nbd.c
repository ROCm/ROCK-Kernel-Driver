/*
 * Network block device - make block devices work over TCP
 *
 * Note that you can not swap over this thing, yet. Seems to work but
 * deadlocks sometimes - you can not swap over TCP in general.
 * 
 * Copyright 1997-2000 Pavel Machek <pavel@ucw.cz>
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
 *
 * possible FIXME: make set_sock / set_blksize / set_size / do_it one syscall
 * why not: would need verify_area and friends, would share yet another 
 *          structure with userland
 */

#undef	NBD_PLUGGABLE
#define PARANOIA
#include <linux/major.h>

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/ioctl.h>
#include <net/sock.h>

#include <linux/devfs_fs_kernel.h>

#include <asm/segment.h>
#include <asm/uaccess.h>
#include <asm/types.h>

#define MAJOR_NR NBD_MAJOR
#include <linux/nbd.h>

#define LO_MAGIC 0x68797548

static int nbd_blksizes[MAX_NBD];
static int nbd_blksize_bits[MAX_NBD];
static int nbd_sizes[MAX_NBD];
static u64 nbd_bytesizes[MAX_NBD];

static struct nbd_device nbd_dev[MAX_NBD];
static devfs_handle_t devfs_handle;

#define DEBUG( s )
/* #define DEBUG( s ) printk( s ) 
 */

#ifdef PARANOIA
static int requests_in;
static int requests_out;
#endif

static void nbd_plug_device(request_queue_t *q, kdev_t dev) { }

static int nbd_open(struct inode *inode, struct file *file)
{
	int dev;

	if (!inode)
		return -EINVAL;
	dev = MINOR(inode->i_rdev);
	if (dev >= MAX_NBD)
		return -ENODEV;

	nbd_dev[dev].refcnt++;
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 *  Send or receive packet.
 */
static int nbd_xmit(int send, struct socket *sock, char *buf, int size)
{
	mm_segment_t oldfs;
	int result;
	struct msghdr msg;
	struct iovec iov;
	unsigned long flags;
	sigset_t oldset;

	oldfs = get_fs();
	set_fs(get_ds());

	spin_lock_irqsave(&current->sigmask_lock, flags);
	oldset = current->blocked;
	sigfillset(&current->blocked);
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, flags);


	do {
		sock->sk->allocation = GFP_BUFFER;
		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_namelen = 0;
		msg.msg_flags = 0;

		if (send)
			result = sock_sendmsg(sock, &msg, size);
		else
			result = sock_recvmsg(sock, &msg, size, 0);

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

	spin_lock_irqsave(&current->sigmask_lock, flags);
	current->blocked = oldset;
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, flags);

	set_fs(oldfs);
	return result;
}

#define FAIL( s ) { printk( KERN_ERR "NBD: " s "(result %d)\n", result ); goto error_out; }

void nbd_send_req(struct socket *sock, struct request *req)
{
	int result;
	struct nbd_request request;

	DEBUG("NBD: sending control, ");
	request.magic = htonl(NBD_REQUEST_MAGIC);
	request.type = htonl(req->cmd);
	request.from = cpu_to_be64( (u64) req->sector << 9);
	request.len = htonl(req->current_nr_sectors << 9);
	memcpy(request.handle, &req, sizeof(req));

	result = nbd_xmit(1, sock, (char *) &request, sizeof(request));
	if (result <= 0)
		FAIL("Sendmsg failed for control.");

	if (req->cmd == WRITE) {
		DEBUG("data, ");
		result = nbd_xmit(1, sock, req->buffer, req->current_nr_sectors << 9);
		if (result <= 0)
			FAIL("Send data failed.");
	}
	return;

      error_out:
	req->errors++;
}

#define HARDFAIL( s ) { printk( KERN_ERR "NBD: " s "(result %d)\n", result ); lo->harderror = result; return NULL; }
struct request *nbd_read_stat(struct nbd_device *lo)
		/* NULL returned = something went wrong, inform userspace       */ 
{
	int result;
	struct nbd_reply reply;
	struct request *xreq, *req;

	DEBUG("reading control, ");
	reply.magic = 0;
	result = nbd_xmit(0, lo->sock, (char *) &reply, sizeof(reply));
	if (result <= 0)
		HARDFAIL("Recv control failed.");
	memcpy(&xreq, reply.handle, sizeof(xreq));
	req = blkdev_entry_prev_request(&lo->queue_head);

	if (xreq != req)
		FAIL("Unexpected handle received.\n");

	DEBUG("ok, ");
	if (ntohl(reply.magic) != NBD_REPLY_MAGIC)
		HARDFAIL("Not enough magic.");
	if (ntohl(reply.error))
		FAIL("Other side returned error.");
	if (req->cmd == READ) {
		DEBUG("data, ");
		result = nbd_xmit(0, lo->sock, req->buffer, req->current_nr_sectors << 9);
		if (result <= 0)
			HARDFAIL("Recv data failed.");
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
	int dequeued;

	down (&lo->queue_lock);
	while (1) {
		up (&lo->queue_lock);
		req = nbd_read_stat(lo);
		down (&lo->queue_lock);

		if (!req) {
			printk(KERN_ALERT "req should never be null\n" );
			goto out;
		}
#ifdef PARANOIA
		if (req != blkdev_entry_prev_request(&lo->queue_head)) {
			printk(KERN_ALERT "NBD: I have problem...\n");
		}
		if (lo != &nbd_dev[MINOR(req->rq_dev)]) {
			printk(KERN_ALERT "NBD: request corrupted!\n");
			continue;
		}
		if (lo->magic != LO_MAGIC) {
			printk(KERN_ALERT "NBD: nbd_dev[] corrupted: Not enough magic\n");
			goto out;
		}
#endif
		list_del(&req->queue);
		up (&lo->queue_lock);
		
		dequeued = nbd_end_request(req);

		down (&lo->queue_lock);
		if (!dequeued)
			list_add(&req->queue, &lo->queue_head);
	}
 out:
	up (&lo->queue_lock);
}

void nbd_clear_que(struct nbd_device *lo)
{
	struct request *req;
	int dequeued;

#ifdef PARANOIA
	if (lo->magic != LO_MAGIC) {
		printk(KERN_ERR "NBD: nbd_dev[] corrupted: Not enough magic when clearing!\n");
		return;
	}
#endif

	while (!list_empty(&lo->queue_head)) {
		req = blkdev_entry_prev_request(&lo->queue_head);
#ifdef PARANOIA
		if (!req) {
			printk( KERN_ALERT "NBD: panic, panic, panic\n" );
			break;
		}
		if (lo != &nbd_dev[MINOR(req->rq_dev)]) {
			printk(KERN_ALERT "NBD: request corrupted when clearing!\n");
			continue;
		}
#endif
		req->errors++;
		list_del(&req->queue);
		up(&lo->queue_lock);

		dequeued = nbd_end_request(req);

		down(&lo->queue_lock);
		if (!dequeued)
			list_add(&req->queue, &lo->queue_head);
	}
}

/*
 * We always wait for result of write, for now. It would be nice to make it optional
 * in future
 * if ((req->cmd == WRITE) && (lo->flags & NBD_WRITE_NOCHK)) 
 *   { printk( "Warning: Ignoring result!\n"); nbd_end_request( req ); }
 */

#undef FAIL
#define FAIL( s ) { printk( KERN_ERR "NBD, minor %d: " s "\n", dev ); goto error_out; }

static void do_nbd_request(request_queue_t * q)
{
	struct request *req;
	int dev = 0;
	struct nbd_device *lo;

	while (!QUEUE_EMPTY) {
		req = CURRENT;
#ifdef PARANOIA
		if (!req)
			FAIL("que not empty but no request?");
#endif
		dev = MINOR(req->rq_dev);
#ifdef PARANOIA
		if (dev >= MAX_NBD)
			FAIL("Minor too big.");		/* Probably can not happen */
#endif
		lo = &nbd_dev[dev];
		if (!lo->file)
			FAIL("Request when not-ready.");
		if ((req->cmd == WRITE) && (lo->flags & NBD_READ_ONLY))
			FAIL("Write on read-only");
#ifdef PARANOIA
		if (lo->magic != LO_MAGIC)
			FAIL("nbd[] is not magical!");
		requests_in++;
#endif
		req->errors = 0;
		blkdev_dequeue_request(req);
		spin_unlock_irq(&io_request_lock);

		down (&lo->queue_lock);
		list_add(&req->queue, &lo->queue_head);
		nbd_send_req(lo->sock, req);	/* Why does this block?         */
		up (&lo->queue_lock);

		spin_lock_irq(&io_request_lock);
		continue;

	      error_out:
		req->errors++;
		blkdev_dequeue_request(req);
		spin_unlock(&io_request_lock);
		nbd_end_request(req);
		spin_lock(&io_request_lock);
	}
	return;
}

static int nbd_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct nbd_device *lo;
	int dev, error, temp;
	struct request sreq ;

	/* Anyone capable of this syscall can do *real bad* things */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!inode)
		return -EINVAL;
	dev = MINOR(inode->i_rdev);
	if (dev >= MAX_NBD)
		return -ENODEV;

	lo = &nbd_dev[dev];
	switch (cmd) {
	case NBD_DISCONNECT:
	        printk("NBD_DISCONNECT\n") ;
                sreq.cmd=2 ; /* shutdown command */
                if (!lo->sock) return -EINVAL ;
                nbd_send_req(lo->sock,&sreq) ;
                return 0 ;
 
	case NBD_CLEAR_SOCK:
		down(&lo->queue_lock);
		nbd_clear_que(lo);
		if (!list_empty(&lo->queue_head)) {
			up(&lo->queue_lock);
			printk(KERN_ERR "nbd: Some requests are in progress -> can not turn off.\n");
			return -EBUSY;
		}
		up(&lo->queue_lock);
		file = lo->file;
		if (!file)
			return -EINVAL;
		lo->file = NULL;
		lo->sock = NULL;
		fput(file);
		return 0;
	case NBD_SET_SOCK:
		if (lo->file)
			return -EBUSY;
		error = -EINVAL;
		file = fget(arg);
		if (file) {
			inode = file->f_dentry->d_inode;
			/* N.B. Should verify that it's a socket */
			lo->file = file;
			lo->sock = &inode->u.socket_i;
			error = 0;
		}
		return error;
	case NBD_SET_BLKSIZE:
		if ((arg & (arg-1)) || (arg < 512) || (arg > PAGE_SIZE))
			return -EINVAL;
		nbd_blksizes[dev] = arg;
		temp = arg >> 9;
		nbd_blksize_bits[dev] = 9;
		while (temp > 1) {
			nbd_blksize_bits[dev]++;
			temp >>= 1;
		}
		nbd_sizes[dev] = nbd_bytesizes[dev] >> nbd_blksize_bits[dev];
		nbd_bytesizes[dev] = nbd_sizes[dev] << nbd_blksize_bits[dev];
		return 0;
	case NBD_SET_SIZE:
		nbd_sizes[dev] = arg >> nbd_blksize_bits[dev];
		nbd_bytesizes[dev] = nbd_sizes[dev] << nbd_blksize_bits[dev];
		return 0;
	case NBD_SET_SIZE_BLOCKS:
		nbd_sizes[dev] = arg;
		nbd_bytesizes[dev] = ((u64) arg) << nbd_blksize_bits[dev];
		return 0;
	case NBD_DO_IT:
		if (!lo->file)
			return -EINVAL;
		nbd_do_it(lo);
		return lo->harderror;
	case NBD_CLEAR_QUE:
		nbd_clear_que(lo);
		return 0;
#ifdef PARANOIA
	case NBD_PRINT_DEBUG:
		printk(KERN_INFO "NBD device %d: next = %p, prev = %p. Global: in %d, out %d\n",
		       dev, lo->queue_head.next, lo->queue_head.prev, requests_in, requests_out);
		return 0;
#endif
	case BLKGETSIZE:
		return put_user(nbd_bytesizes[dev] >> 9, (long *) arg);
	}
	return -EINVAL;
}

static int nbd_release(struct inode *inode, struct file *file)
{
	struct nbd_device *lo;
	int dev;

	if (!inode)
		return -ENODEV;
	dev = MINOR(inode->i_rdev);
	if (dev >= MAX_NBD)
		return -ENODEV;
	lo = &nbd_dev[dev];
	if (lo->refcnt <= 0)
		printk(KERN_ALERT "nbd_release: refcount(%d) <= 0\n", lo->refcnt);
	lo->refcnt--;
	/* N.B. Doesn't lo->file need an fput?? */
	MOD_DEC_USE_COUNT;
	return 0;
}

static struct block_device_operations nbd_fops =
{
	open:		nbd_open,
	release:	nbd_release,
	ioctl:		nbd_ioctl,
};

/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

#ifdef MODULE
#define nbd_init init_module
#endif

int nbd_init(void)
{
	int i;

	if (sizeof(struct nbd_request) != 28) {
		printk(KERN_CRIT "Sizeof nbd_request needs to be 28 in order to work!\n" );
		return -EIO;
	}

	if (register_blkdev(MAJOR_NR, "nbd", &nbd_fops)) {
		printk("Unable to get major number %d for NBD\n",
		       MAJOR_NR);
		return -EIO;
	}
#ifdef MODULE
	printk("nbd: registered device at major %d\n", MAJOR_NR);
#endif
	blksize_size[MAJOR_NR] = nbd_blksizes;
	blk_size[MAJOR_NR] = nbd_sizes;
	blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), do_nbd_request);
#ifndef NBD_PLUGGABLE
	blk_queue_pluggable(BLK_DEFAULT_QUEUE(MAJOR_NR), nbd_plug_device);
#endif
	blk_queue_headactive(BLK_DEFAULT_QUEUE(MAJOR_NR), 0);
	for (i = 0; i < MAX_NBD; i++) {
		nbd_dev[i].refcnt = 0;
		nbd_dev[i].file = NULL;
		nbd_dev[i].magic = LO_MAGIC;
		nbd_dev[i].flags = 0;
		INIT_LIST_HEAD(&nbd_dev[i].queue_head);
		init_MUTEX(&nbd_dev[i].queue_lock);
		nbd_blksizes[i] = 1024;
		nbd_blksize_bits[i] = 10;
		nbd_bytesizes[i] = 0x7ffffc00; /* 2GB */
		nbd_sizes[i] = nbd_bytesizes[i] >> nbd_blksize_bits[i];
		register_disk(NULL, MKDEV(MAJOR_NR,i), 1, &nbd_fops,
				nbd_bytesizes[i]>>9);
	}
	devfs_handle = devfs_mk_dir (NULL, "nbd", NULL);
	devfs_register_series (devfs_handle, "%u", MAX_NBD,
			       DEVFS_FL_DEFAULT, MAJOR_NR, 0,
			       S_IFBLK | S_IRUSR | S_IWUSR,
			       &nbd_fops, NULL);

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	devfs_unregister (devfs_handle);
	blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));

	if (unregister_blkdev(MAJOR_NR, "nbd") != 0)
		printk("nbd: cleanup_module failed\n");
	else
		printk("nbd: module cleaned up.\n");
}
#endif
