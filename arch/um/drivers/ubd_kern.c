/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

/* 2001-09-28...2002-04-17
 * Partition stuff by James_McMechan@hotmail.com
 * old style ubd by setting UBD_SHIFT to 0
 * 2002-09-27...2002-10-18 massive tinkering for 2.5
 * partitions have changed in 2.5
 */

#define MAJOR_NR UBD_MAJOR
#define UBD_SHIFT 4

#include "linux/config.h"
#include "linux/module.h"
#include "linux/blkdev.h"
#include "linux/hdreg.h"
#include "linux/init.h"
#include "linux/devfs_fs_kernel.h"
#include "linux/cdrom.h"
#include "linux/proc_fs.h"
#include "linux/ctype.h"
#include "linux/capability.h"
#include "linux/mm.h"
#include "linux/vmalloc.h"
#include "linux/blkpg.h"
#include "linux/genhd.h"
#include "linux/spinlock.h"
#include "asm/segment.h"
#include "asm/uaccess.h"
#include "asm/irq.h"
#include "asm/types.h"
#include "asm/tlbflush.h"
#include "user_util.h"
#include "mem_user.h"
#include "kern_util.h"
#include "kern.h"
#include "mconsole_kern.h"
#include "init.h"
#include "irq_user.h"
#include "ubd_user.h"
#include "2_5compat.h"
#include "os.h"

static spinlock_t ubd_io_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t ubd_lock = SPIN_LOCK_UNLOCKED;

static void (*do_ubd)(void);

static int ubd_open(struct inode * inode, struct file * filp);
static int ubd_release(struct inode * inode, struct file * file);
static int ubd_ioctl(struct inode * inode, struct file * file,
		     unsigned int cmd, unsigned long arg);

#define MAX_DEV (8)

static struct block_device_operations ubd_blops = {
        .owner		= THIS_MODULE,
        .open		= ubd_open,
        .release	= ubd_release,
        .ioctl		= ubd_ioctl,
};

/* Protected by the queue_lock */
static request_queue_t *ubd_queue;

/* Protected by ubd_lock */
static int fake_major = 0;

static struct gendisk *ubd_gendisk[MAX_DEV];
static struct gendisk *fake_gendisk[MAX_DEV];
 
#ifdef CONFIG_BLK_DEV_UBD_SYNC
#define OPEN_FLAGS ((struct openflags) { .r = 1, .w = 1, .s = 1, .c = 0, \
					 .cl = 1 })
#else
#define OPEN_FLAGS ((struct openflags) { .r = 1, .w = 1, .s = 0, .c = 0, \
					 .cl = 1 })
#endif

/* Not protected - changed only in ubd_setup_common and then only to
 * to enable O_SYNC.
 */
static struct openflags global_openflags = OPEN_FLAGS;

struct cow {
	char *file;
	int fd;
	unsigned long *bitmap;
	unsigned long bitmap_len;
	int bitmap_offset;
        int data_offset;
};

struct ubd {
	char *file;
	int is_dir;
	int count;
	int fd;
	__u64 size;
	struct openflags boot_openflags;
	struct openflags openflags;
	struct cow cow;
};

#define DEFAULT_COW { \
	.file =			NULL, \
        .fd =			-1, \
        .bitmap =		NULL, \
	.bitmap_offset =	0, \
        .data_offset =		0, \
}

#define DEFAULT_UBD { \
	.file = 		NULL, \
	.is_dir =		0, \
	.count =		0, \
	.fd =			-1, \
	.size =			-1, \
	.boot_openflags =	OPEN_FLAGS, \
	.openflags =		OPEN_FLAGS, \
        .cow =			DEFAULT_COW, \
}

struct ubd ubd_dev[MAX_DEV] = { [ 0 ... MAX_DEV - 1 ] = DEFAULT_UBD };

static int ubd0_init(void)
{
	if(ubd_dev[0].file == NULL)
		ubd_dev[0].file = "root_fs";
	return(0);
}

__initcall(ubd0_init);

/* Only changed by fake_ide_setup which is a setup */
static int fake_ide = 0;
static struct proc_dir_entry *proc_ide_root = NULL;
static struct proc_dir_entry *proc_ide = NULL;

static void make_proc_ide(void)
{
	proc_ide_root = proc_mkdir("ide", 0);
	proc_ide = proc_mkdir("ide0", proc_ide_root);
}

static int proc_ide_read_media(char *page, char **start, off_t off, int count,
			       int *eof, void *data)
{
	int len;

	strcpy(page, "disk\n");
	len = strlen("disk\n");
	len -= off;
	if (len < count){
		*eof = 1;
		if (len <= 0) return 0;
	}
	else len = count;
	*start = page + off;
	return len;
}

static void make_ide_entries(char *dev_name)
{
	struct proc_dir_entry *dir, *ent;
	char name[64];

	if(proc_ide_root == NULL) make_proc_ide();

	dir = proc_mkdir(dev_name, proc_ide);
	if(!dir) return;

	ent = create_proc_entry("media", S_IFREG|S_IRUGO, dir);
	if(!ent) return;
	ent->nlink = 1;
	ent->data = NULL;
	ent->read_proc = proc_ide_read_media;
	ent->write_proc = NULL;
	sprintf(name,"ide0/%s", dev_name);
	proc_symlink(dev_name, proc_ide_root, name);
}

static int fake_ide_setup(char *str)
{
	fake_ide = 1;
	return(1);
}

__setup("fake_ide", fake_ide_setup);

__uml_help(fake_ide_setup,
"fake_ide\n"
"    Create ide0 entries that map onto ubd devices.\n\n"
);

static int ubd_setup_common(char *str, int *index_out)
{
	struct openflags flags = global_openflags;
	char *backing_file;
	int n, err;

	if(index_out) *index_out = -1;
	n = *str++;
	if(n == '='){
		static int fake_major_allowed = 1;
		char *end;
		int major;

		if(!strcmp(str, "sync")){
			global_openflags.s = 1;
			return(0);
		}
		major = simple_strtoul(str, &end, 0);
		if((*end != '\0') || (end == str)){
			printk(KERN_ERR 
			       "ubd_setup : didn't parse major number\n");
			return(1);
		}

		if(!fake_major_allowed){
			printk(KERN_ERR "Can't assign a fake major twice\n");
			return(1);
		}

		err = 1;
 		spin_lock(&ubd_lock);
 		if(!fake_major_allowed){
 			printk(KERN_ERR "Can't assign a fake major twice\n");
 			goto out1;
 		}
 
 		fake_major = major;
		fake_major_allowed = 0;

		printk(KERN_INFO "Setting extra ubd major number to %d\n",
		       major);
 		err = 0;
 	out1:
 		spin_unlock(&ubd_lock);
		return(err);
	}

	if(n < '0'){
		printk(KERN_ERR "ubd_setup : index out of range\n"); }

	if((n >= '0') && (n <= '9')) n -= '0';
	else if((n >= 'a') && (n <= 'z')) n -= 'a';
	else {
		printk(KERN_ERR "ubd_setup : device syntax invalid\n");
		return(1);
	}
	if(n >= MAX_DEV){
		printk(KERN_ERR "ubd_setup : index out of range "
		       "(%d devices)\n", MAX_DEV);	
		return(1);
	}

	err = 1;
	spin_lock(&ubd_lock);

	if(ubd_dev[n].file != NULL){
		printk(KERN_ERR "ubd_setup : device already configured\n");
		goto out2;
	}

	if(index_out) *index_out = n;

	if (*str == 'r'){
		flags.w = 0;
		str++;
	}
	if (*str == 's'){
		flags.s = 1;
		str++;
	}
	if(*str++ != '='){
		printk(KERN_ERR "ubd_setup : Expected '='\n");
		goto out2;
	}

	err = 0;
	backing_file = strchr(str, ',');
	if(backing_file){
		*backing_file = '\0';
		backing_file++;
	}
	ubd_dev[n].file = str;
	if(ubd_is_dir(ubd_dev[n].file))
		ubd_dev[n].is_dir = 1;
	ubd_dev[n].cow.file = backing_file;
	ubd_dev[n].boot_openflags = flags;
 out2:
	spin_unlock(&ubd_lock);
	return(err);
}

static int ubd_setup(char *str)
{
	ubd_setup_common(str, NULL);
	return(1);
}

__setup("ubd", ubd_setup);
__uml_help(ubd_setup,
"ubd<n>=<filename>\n"
"    This is used to associate a device with a file in the underlying\n"
"    filesystem. Usually, there is a filesystem in the file, but \n"
"    that's not required. Swap devices containing swap files can be\n"
"    specified like this. Also, a file which doesn't contain a\n"
"    filesystem can have its contents read in the virtual \n"
"    machine by running dd on the device. n must be in the range\n"
"    0 to 7. Appending an 'r' to the number will cause that device\n"
"    to be mounted read-only. For example ubd1r=./ext_fs. Appending\n"
"    an 's' (has to be _after_ 'r', if there is one) will cause data\n"
"    to be written to disk on the host immediately.\n\n"
);

static int fakehd_set = 0;
static int fakehd(char *str)
{
	printk(KERN_INFO 
	       "fakehd : Changing ubd name to \"hd\".\n");
	fakehd_set = 1;
	return 1;
}

__setup("fakehd", fakehd);
__uml_help(fakehd,
"fakehd\n"
"    Change the ubd device name to \"hd\".\n\n"
);

static void do_ubd_request(request_queue_t * q);

/* Only changed by ubd_init, which is an initcall. */
int thread_fd = -1;

/* Changed by ubd_handler, which is serialized because interrupts only
 * happen on CPU 0.
 */
int intr_count = 0;

static void ubd_finish(struct request *req, int error)
{
	int nsect;

	if(error){
 		spin_lock(&ubd_io_lock);
		end_request(req, 0);
 		spin_unlock(&ubd_io_lock);
		return;
	}
	nsect = req->current_nr_sectors;
	req->sector += nsect;
	req->buffer += nsect << 9;
	req->errors = 0;
	req->nr_sectors -= nsect;
	req->current_nr_sectors = 0;
	spin_lock(&ubd_io_lock);
	end_request(req, 1);
	spin_unlock(&ubd_io_lock);
}

static void ubd_handler(void)
{
	struct io_thread_req req;
	struct request *rq = elv_next_request(ubd_queue);
	int n;

	do_ubd = NULL;
	intr_count++;
	n = read_ubd_fs(thread_fd, &req, sizeof(req));
	if(n != sizeof(req)){
		printk(KERN_ERR "Pid %d - spurious interrupt in ubd_handler, "
		       "errno = %d\n", os_getpid(), -n);
		spin_lock(&ubd_io_lock);
		end_request(rq, 0);
		spin_unlock(&ubd_io_lock);
		return;
	}
        
        if((req.offset != ((__u64) (rq->sector)) << 9) ||
	   (req.length != (rq->current_nr_sectors) << 9))
		panic("I/O op mismatch");
	
	ubd_finish(rq, req.error);
	reactivate_fd(thread_fd, UBD_IRQ);	
	do_ubd_request(ubd_queue);
}

static void ubd_intr(int irq, void *dev, struct pt_regs *unused)
{
	ubd_handler();
}

/* Only changed by ubd_init, which is an initcall. */
static int io_pid = -1;

void kill_io_thread(void)
{
	if(io_pid != -1) 
		os_kill_process(io_pid, 1);
}

__uml_exitcall(kill_io_thread);

static int ubd_file_size(struct ubd *dev, __u64 *size_out)
{
	char *file;

	file = dev->cow.file ? dev->cow.file : dev->file;
	return(os_file_size(file, size_out));
}

static void ubd_close(struct ubd *dev)
{
	os_close_file(dev->fd);
	if(dev->cow.file == NULL)
		return;

	os_close_file(dev->cow.fd);
	vfree(dev->cow.bitmap);
	dev->cow.bitmap = NULL;
}

static int ubd_open_dev(struct ubd *dev)
{
	struct openflags flags;
	int err, n, create_cow, *create_ptr;

	create_cow = 0;
	create_ptr = (dev->cow.file != NULL) ? &create_cow : NULL;
	dev->fd = open_ubd_file(dev->file, &dev->openflags, &dev->cow.file,
				&dev->cow.bitmap_offset, &dev->cow.bitmap_len, 
				&dev->cow.data_offset, create_ptr);

	if((dev->fd == -ENOENT) && create_cow){
		n = dev - ubd_dev;
		dev->fd = create_cow_file(dev->file, dev->cow.file, 
					  dev->openflags, 1 << 9,
					  &dev->cow.bitmap_offset, 
					  &dev->cow.bitmap_len,
					  &dev->cow.data_offset);
		if(dev->fd >= 0){
			printk(KERN_INFO "Creating \"%s\" as COW file for "
			       "\"%s\"\n", dev->file, dev->cow.file);
		}
	}

	if(dev->fd < 0) return(dev->fd);

	if(dev->cow.file != NULL){
		err = -ENOMEM;
		dev->cow.bitmap = (void *) vmalloc(dev->cow.bitmap_len);
		if(dev->cow.bitmap == NULL) goto error;
		flush_tlb_kernel_vm();

		err = read_cow_bitmap(dev->fd, dev->cow.bitmap, 
				      dev->cow.bitmap_offset, 
				      dev->cow.bitmap_len);
		if(err) goto error;

		flags = dev->openflags;
		flags.w = 0;
		err = open_ubd_file(dev->cow.file, &flags, NULL, NULL, NULL, 
				    NULL, NULL);
		if(err < 0) goto error;
		dev->cow.fd = err;
	}
	return(0);
 error:
	os_close_file(dev->fd);
	return(err);
}

static int ubd_new_disk(int major, u64 size, int unit,
			struct gendisk **disk_out)
			
{
	struct gendisk *disk;

	disk = alloc_disk(1 << UBD_SHIFT);
	if (!disk)
		return -ENOMEM;

	disk->major = major;
	disk->first_minor = unit << UBD_SHIFT;
	disk->fops = &ubd_blops;
	set_capacity(disk, size / 512);
	sprintf(disk->disk_name, "ubd");
	sprintf(disk->devfs_name, "ubd/disc%d", unit);

	disk->private_data = &ubd_dev[unit];
	disk->queue = ubd_queue;
	add_disk(disk);

	*disk_out = disk;
	return 0;
}

static int ubd_add(int n)
{
	struct ubd *dev = &ubd_dev[n];
	int err;

	if(dev->is_dir)
		return(-EISDIR);

	if (!dev->file)
		return(-ENODEV);

	if (ubd_open_dev(dev))
		return(-ENODEV);

	err = ubd_file_size(dev, &dev->size);
	if(err)
		return(err);

	err = ubd_new_disk(MAJOR_NR, dev->size, n, &ubd_gendisk[n]);
	if(err) 
		return(err);
 
	if(fake_major)
		ubd_new_disk(fake_major, dev->size, n, 
			     &fake_gendisk[n]);

	/* perhaps this should also be under the "if (fake_major)" above */
	/* using the fake_disk->disk_name and also the fakehd_set name */
	if (fake_ide)
		make_ide_entries(ubd_gendisk[n]->disk_name);

	ubd_close(dev);
	return 0;
}

static int ubd_config(char *str)
{
	int n, err;

	str = uml_strdup(str);
	if(str == NULL){
		printk(KERN_ERR "ubd_config failed to strdup string\n");
		return(1);
	}
	err = ubd_setup_common(str, &n);
	if(err){
		kfree(str);
		return(-1);
	}
	if(n == -1) return(0);

 	spin_lock(&ubd_lock);
	err = ubd_add(n);
	if(err)
		ubd_dev[n].file = NULL;
 	spin_unlock(&ubd_lock);

	return(err);
}

static int ubd_get_config(char *dev, char *str, int size, char **error_out)
{
	struct ubd *ubd;
	char *end;
	int major, n = 0;

	major = simple_strtoul(dev, &end, 0);
	if((*end != '\0') || (end == dev)){
		*error_out = "ubd_get_config : didn't parse major number";
		return(-1);
	}

	if((major >= MAX_DEV) || (major < 0)){
		*error_out = "ubd_get_config : major number out of range";
		return(-1);
	}

	ubd = &ubd_dev[major];
	spin_lock(&ubd_lock);

	if(ubd->file == NULL){
		CONFIG_CHUNK(str, size, n, "", 1);
		goto out;
	}

	CONFIG_CHUNK(str, size, n, ubd->file, 0);

	if(ubd->cow.file != NULL){
		CONFIG_CHUNK(str, size, n, ",", 0);
		CONFIG_CHUNK(str, size, n, ubd->cow.file, 1);
	}
	else CONFIG_CHUNK(str, size, n, "", 1);

 out:
	spin_unlock(&ubd_lock);
	return(n);
}

static int ubd_remove(char *str)
{
	struct ubd *dev;
	int n, err = -ENODEV;

	if(!isdigit(*str))
		return(err);	/* it should be a number 0-7/a-h */

	n = *str - '0';
	if(n >= MAX_DEV) 
		return(err);

	dev = &ubd_dev[n];
	if(dev->count > 0)
		return(-EBUSY);	/* you cannot remove a open disk */

	err = 0;
 	spin_lock(&ubd_lock);

	if(ubd_gendisk[n] == NULL)
		goto out;

	del_gendisk(ubd_gendisk[n]);
	put_disk(ubd_gendisk[n]);
	ubd_gendisk[n] = NULL;

	if(fake_gendisk[n] != NULL){
		del_gendisk(fake_gendisk[n]);
		put_disk(fake_gendisk[n]);
		fake_gendisk[n] = NULL;
	}

	*dev = ((struct ubd) DEFAULT_UBD);
	err = 0;
 out:
 	spin_unlock(&ubd_lock);
	return(err);
}

static struct mc_device ubd_mc = {
	.name		= "ubd",
	.config		= ubd_config,
 	.get_config	= ubd_get_config,
	.remove		= ubd_remove,
};

static int ubd_mc_init(void)
{
	mconsole_register_dev(&ubd_mc);
	return 0;
}

__initcall(ubd_mc_init);

int ubd_init(void)
{
        int i;

	devfs_mk_dir("ubd");
	if (register_blkdev(MAJOR_NR, "ubd"))
		return -1;

	ubd_queue = blk_init_queue(do_ubd_request, &ubd_io_lock);
	if (!ubd_queue) {
		unregister_blkdev(MAJOR_NR, "ubd");
		return -1;
	}
		
	elevator_init(ubd_queue, &elevator_noop);

	if (fake_major != 0) {
		char name[sizeof("ubd_nnn\0")];

		snprintf(name, sizeof(name), "ubd_%d", fake_major);
		devfs_mk_dir(name);
		if (register_blkdev(fake_major, "ubd"))
			return -1;
	}
	for (i = 0; i < MAX_DEV; i++) 
		ubd_add(i);
	return 0;
}

late_initcall(ubd_init);

int ubd_driver_init(void){
	unsigned long stack;
	int err;

	if(global_openflags.s){
		printk(KERN_INFO "ubd : Synchronous mode\n");
		return(0);
	}
	stack = alloc_stack(0, 0);
	io_pid = start_io_thread(stack + PAGE_SIZE - sizeof(void *), 
				 &thread_fd);
	if(io_pid < 0){
		printk(KERN_ERR 
		       "ubd : Failed to start I/O thread (errno = %d) - "
		       "falling back to synchronous I/O\n", -io_pid);
		return(0);
	}
	err = um_request_irq(UBD_IRQ, thread_fd, IRQ_READ, ubd_intr, 
			     SA_INTERRUPT, "ubd", ubd_dev);
	if(err != 0) printk(KERN_ERR 
			    "um_request_irq failed - errno = %d\n", -err);
	return(err);
}

device_initcall(ubd_driver_init);

static int ubd_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ubd *dev = disk->private_data;
	int err = -EISDIR;

	if(dev->is_dir == 1)
		goto out;

	err = 0;
	if(dev->count == 0){
		dev->openflags = dev->boot_openflags;

		err = ubd_open_dev(dev);
		if(err){
			printk(KERN_ERR "%s: Can't open \"%s\": errno = %d\n",
			       disk->disk_name, dev->file, -err);
			goto out;
		}
	}
	dev->count++;
	if((filp->f_mode & FMODE_WRITE) && !dev->openflags.w){
	        if(--dev->count == 0) ubd_close(dev);
	        err = -EROFS;
	}
 out:
	return(err);
}

static int ubd_release(struct inode * inode, struct file * file)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ubd *dev = disk->private_data;

	if(--dev->count == 0)
		ubd_close(dev);
	return(0);
}

void cowify_req(struct io_thread_req *req, struct ubd *dev)
{
        int i, update_bitmap, sector = req->offset >> 9;

	if(req->length > (sizeof(req->sector_mask) * 8) << 9)
		panic("Operation too long");
	if(req->op == UBD_READ) {
		for(i = 0; i < req->length >> 9; i++){
			if(ubd_test_bit(sector + i, (unsigned char *) 
					dev->cow.bitmap)){
				ubd_set_bit(i, (unsigned char *) 
					    &req->sector_mask);
			}
                }
        } 
        else {
		update_bitmap = 0;
		for(i = 0; i < req->length >> 9; i++){
			ubd_set_bit(i, (unsigned char *) 
				    &req->sector_mask);
			if(!ubd_test_bit(sector + i, (unsigned char *) 
					 dev->cow.bitmap))
				update_bitmap = 1;
			ubd_set_bit(sector + i, (unsigned char *) 
				    dev->cow.bitmap);
		}
		if(update_bitmap){
			req->cow_offset = sector / (sizeof(unsigned long) * 8);
			req->bitmap_words[0] = 
				dev->cow.bitmap[req->cow_offset];
			req->bitmap_words[1] = 
				dev->cow.bitmap[req->cow_offset + 1];
			req->cow_offset *= sizeof(unsigned long);
			req->cow_offset += dev->cow.bitmap_offset;
		}
	}
}

static int prepare_request(struct request *req, struct io_thread_req *io_req)
{
	struct gendisk *disk = req->rq_disk;
	struct ubd *dev = disk->private_data;
	__u64 block;
	int nsect;

	if(req->rq_status == RQ_INACTIVE) return(1);

	if(dev->is_dir){
		strcpy(req->buffer, "HOSTFS:");
		strcat(req->buffer, dev->file);
 		spin_lock(&ubd_io_lock);
		end_request(req, 1);
 		spin_unlock(&ubd_io_lock);
		return(1);
	}

	if((rq_data_dir(req) == WRITE) && !dev->openflags.w){
		printk("Write attempted on readonly ubd device %s\n", 
		       disk->disk_name);
 		spin_lock(&ubd_io_lock);
		end_request(req, 0);
 		spin_unlock(&ubd_io_lock);
		return(1);
	}

        block = req->sector;
        nsect = req->current_nr_sectors;

	io_req->op = rq_data_dir(req) == READ ? UBD_READ : UBD_WRITE;
	io_req->fds[0] = (dev->cow.file != NULL) ? dev->cow.fd : dev->fd;
	io_req->fds[1] = dev->fd;
	io_req->offsets[0] = 0;
	io_req->offsets[1] = dev->cow.data_offset;
	io_req->offset = ((__u64) block) << 9;
	io_req->length = nsect << 9;
	io_req->buffer = req->buffer;
	io_req->sectorsize = 1 << 9;
	io_req->sector_mask = 0;
	io_req->cow_offset = -1;
	io_req->error = 0;

        if(dev->cow.file != NULL) cowify_req(io_req, dev);
	return(0);
}

static void do_ubd_request(request_queue_t *q)
{
	struct io_thread_req io_req;
	struct request *req;
	int err, n;

	if(thread_fd == -1){
		while(!list_empty(&q->queue_head)){
			req = elv_next_request(q);
			err = prepare_request(req, &io_req);
			if(!err){
				do_io(&io_req);
				ubd_finish(req, io_req.error);
			}
		}
	}
	else {
		if(do_ubd || list_empty(&q->queue_head)) return;
		req = elv_next_request(q);
		err = prepare_request(req, &io_req);
		if(!err){
			do_ubd = ubd_handler;
			n = write_ubd_fs(thread_fd, (char *) &io_req, 
					 sizeof(io_req));
			if(n != sizeof(io_req))
				printk("write to io thread failed, "
				       "errno = %d\n", -n);
		}
	}
}

static int ubd_ioctl(struct inode * inode, struct file * file,
		     unsigned int cmd, unsigned long arg)
{
	struct hd_geometry *loc = (struct hd_geometry *) arg;
	struct ubd *dev = inode->i_bdev->bd_disk->private_data;
	int err;
	struct hd_driveid ubd_id = {
		.cyls		= 0,
		.heads		= 128,
		.sectors	= 32,
	};

	switch (cmd) {
	        struct hd_geometry g;
		struct cdrom_volctrl volume;
	case HDIO_GETGEO:
		if(!loc) return(-EINVAL);
		g.heads = 128;
		g.sectors = 32;
		g.cylinders = dev->size / (128 * 32 * 512);
		g.start = 2;
		return(copy_to_user(loc, &g, sizeof(g)) ? -EFAULT : 0);

	case HDIO_SET_UNMASKINTR:
		if(!capable(CAP_SYS_ADMIN)) return(-EACCES);
		if((arg > 1) || (inode->i_bdev->bd_contains != inode->i_bdev))
			return(-EINVAL);
		return(0);

	case HDIO_GET_UNMASKINTR:
		if(!arg)  return(-EINVAL);
		err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
		if(err)
			return(err);
		return(0);

	case HDIO_GET_MULTCOUNT:
		if(!arg)  return(-EINVAL);
		err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
		if(err)
			return(err);
		return(0);

	case HDIO_SET_MULTCOUNT:
		if(!capable(CAP_SYS_ADMIN)) return(-EACCES);
		if(inode->i_bdev->bd_contains != inode->i_bdev)
			return(-EINVAL);
		return(0);

	case HDIO_GET_IDENTITY:
		ubd_id.cyls = dev->size / (128 * 32 * 512);
		if(copy_to_user((char *) arg, (char *) &ubd_id, 
				 sizeof(ubd_id)))
			return(-EFAULT);
		return(0);
		
	case CDROMVOLREAD:
		if(copy_from_user(&volume, (char *) arg, sizeof(volume)))
			return(-EFAULT);
		volume.channel0 = 255;
		volume.channel1 = 255;
		volume.channel2 = 255;
		volume.channel3 = 255;
		if(copy_to_user((char *) arg, &volume, sizeof(volume)))
			return(-EFAULT);
		return(0);
	}
	return(-EINVAL);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
