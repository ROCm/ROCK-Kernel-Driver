/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

/* 2001-09-28...2002-04-17
 * Partition stuff by James_McMechan@hotmail.com
 * old style ubd by setting UBD_SHIFT to 0
 */

#define MAJOR_NR UBD_MAJOR
#define UBD_SHIFT 4

#include "linux/config.h"
#include "linux/blk.h"
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

static spinlock_t ubd_lock;
static void (*do_ubd)(void);

static int ubd_open(struct inode * inode, struct file * filp);
static int ubd_release(struct inode * inode, struct file * file);
static int ubd_ioctl(struct inode * inode, struct file * file,
		     unsigned int cmd, unsigned long arg);
static int ubd_revalidate(kdev_t rdev);

#define MAX_DEV (8)
#define MAX_MINOR (MAX_DEV << UBD_SHIFT)

#define DEVICE_NR(n) (minor(n) >> UBD_SHIFT)

static struct block_device_operations ubd_blops = {
        open:		ubd_open,
        release:	ubd_release,
        ioctl:		ubd_ioctl,
        revalidate:	ubd_revalidate,
};

static request_queue_t *ubd_queue;

static int fake_major = 0;
static struct gendisk ubd_gendisk[MAX_DEV];
static struct gendisk fake_gendisk[MAX_DEV];
 
#ifdef CONFIG_BLK_DEV_UBD_SYNC
#define OPEN_FLAGS ((struct openflags) { r : 1, w : 1, s : 1, c : 0 })
#else
#define OPEN_FLAGS ((struct openflags) { r : 1, w : 1, s : 0, c : 0 })
#endif

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
	devfs_handle_t real;
	devfs_handle_t fake;
	struct cow cow;
};

#define DEFAULT_COW { \
	file:			NULL, \
        fd:			-1, \
        bitmap:			NULL, \
	bitmap_offset:		0, \
        data_offset:		0, \
}

#define DEFAULT_UBD { \
	file: 			NULL, \
	is_dir:			0, \
	count:			0, \
	fd:			-1, \
	size:			-1, \
	boot_openflags:		OPEN_FLAGS, \
	openflags:		OPEN_FLAGS, \
	real:			NULL, \
	fake:			NULL, \
        cow:			DEFAULT_COW, \
}

struct ubd ubd_dev[MAX_DEV] = { [ 0 ... MAX_DEV - 1 ] = DEFAULT_UBD };

static int ubd0_init(void)
{
	if(ubd_dev[0].file == NULL)
		ubd_dev[0].file = "root_fs";
	return(0);
}

__initcall(ubd0_init);

static struct hd_driveid ubd_id = {
        cyls:		0,
	heads:		128,
	sectors:	32,
};

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

	if(!fake_ide) return;
	if(proc_ide_root == NULL) make_proc_ide();
	dir = proc_mkdir(dev_name, proc_ide);
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
	int i, n;

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
		if(*end != '\0'){
			printk(KERN_ERR 
			       "ubd_setup : didn't parse major number\n");
			return(1);
		}

		if(!fake_major_allowed){
			printk(KERN_ERR "Can't assign a fake major twice\n");
			return(1);
		}

		fake_major = major;
		fake_major_allowed = 0;

		printk(KERN_INFO "Setting extra ubd major number to %d\n",
		       major);
		return(0);
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

	if(ubd_dev[n].file != NULL){
		printk(KERN_ERR "ubd_setup : device already configured\n");
		return(1);
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
		return(1);
	}
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
	return(0);
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

int thread_fd = -1;

int intr_count = 0;

static void ubd_finish(int error)
{
	int nsect;

	if(error){
		end_request(CURRENT, 0);
		return;
	}
	nsect = CURRENT->current_nr_sectors;
	CURRENT->sector += nsect;
	CURRENT->buffer += nsect << 9;
	CURRENT->errors = 0;
	CURRENT->nr_sectors -= nsect;
	CURRENT->current_nr_sectors = 0;
	end_request(CURRENT, 1);
}

static void ubd_handler(void)
{
	struct io_thread_req req;
	int n;

	do_ubd = NULL;
	intr_count++;
	n = read_ubd_fs(thread_fd, &req, sizeof(req));
	if(n != sizeof(req)){
		printk(KERN_ERR "Pid %d - spurious interrupt in ubd_handler, "
		       "errno = %d\n", os_getpid(), -n);
		spin_lock(&ubd_lock);
		end_request(CURRENT, 0);
		spin_unlock(&ubd_lock);
		return;
	}
        
        if((req.offset != ((__u64) (CURRENT->sector)) << 9) ||
	   (req.length != (CURRENT->current_nr_sectors) << 9))
		panic("I/O op mismatch");
	
	spin_lock(&ubd_lock);
	ubd_finish(req.error);
	reactivate_fd(thread_fd, UBD_IRQ);	
	do_ubd_request(ubd_queue);
	spin_unlock(&ubd_lock);
}

static void ubd_intr(int irq, void *dev, struct pt_regs *unused)
{
	ubd_handler();
}

static int io_pid = -1;

void kill_io_thread(void)
{
	if(io_pid != -1) kill(io_pid, SIGKILL);
}

__uml_exitcall(kill_io_thread);

int sync = 0;

static int ubd_file_size(struct ubd *dev, __u64 *size_out)
{
	char *file;

	file = dev->cow.file ? dev->cow.file : dev->file;
	return(os_file_size(file, size_out));
}

devfs_handle_t ubd_dir_handle;
devfs_handle_t ubd_fake_dir_handle;

static int ubd_add(int n)
{
 	devfs_handle_t real, fake;
	char name[sizeof("nnnnnn\0")];
	struct ubd *dev = &ubd_dev[n];
	u64 size;

	if (!dev->file)
		return -1;

	ubd_gendisk[n].major = MAJOR_NR;
	ubd_gendisk[n].first_minor = n << UBD_SHIFT;
	ubd_gendisk[n].minor_shift = UBD_SHIFT;
	ubd_gendisk[n].fops = &ubd_blops;
	if (fakehd_set)
		sprintf(ubd_gendisk[n].disk_name, "hd%c", n + 'a');
	else
		sprintf(ubd_gendisk[n].disk_name, "ubd%d", n);

	if (fake_major) {
		fake_gendisk[n].major = fake_major;
		fake_gendisk[n].first_minor = n << UBD_SHIFT;
		fake_gendisk[n].minor_shift = UBD_SHIFT;
		fake_gendisk[n].fops = &ubd_blops;
		sprintf(fake_gendisk[n].disk_name, "ubd%d", n);
	}

	if (!dev->is_dir && ubd_file_size(dev, &size) == 0) {
		set_capacity(&ubd_gendisk[n], size/512);
		set_capacity(&fake_gendisk[n], size/512);
	}
 
	sprintf(name, "%d", n);
	real = devfs_register(ubd_dir_handle, name, DEVFS_FL_REMOVABLE, 
			      MAJOR_NR, n << UBD_SHIFT,
			      S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP |S_IWGRP,
			      &ubd_blops, NULL);
	add_disk(&ubd_gendisk[n]);
	if (fake_major) {
		fake = devfs_register(ubd_fake_dir_handle, name, 
				      DEVFS_FL_REMOVABLE, fake_major,
				      n << UBD_SHIFT, 
				      S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP |
				      S_IWGRP, &ubd_blops, NULL);
		add_disk(&fake_gendisk[n]);
 		if(fake == NULL) return(-1);
 		ubd_dev[n].fake = fake;
	}
 
 	if(real == NULL) return(-1);
 	ubd_dev[n].real = real;
 
	make_ide_entries(ubd_gendisk[n].disk_name);
	return(0);
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

	err = ubd_add(n);
	if(err){
		ubd_dev[n].file = NULL;
		return(err);
	}

	return(0);
}

static int ubd_remove(char *str)
{
	struct ubd *dev;
	int n;

	if(!isdigit(*str)) return(-1);
	n = *str - '0';
	if(n > MAX_DEV) return(-1);
	dev = &ubd_dev[n];
	del_gendisk(&ubd_gendisk[n]);
	if (fake_major)
		del_gendisk(&fake_gendisk[n]);
	if(dev->file == NULL) return(0);
	if(dev->count > 0) return(-1);
	if(dev->real != NULL) devfs_unregister(dev->real);
	if(dev->fake != NULL) devfs_unregister(dev->fake);
	*dev = ((struct ubd) DEFAULT_UBD);
	return(0);
}

static struct mc_device ubd_mc = {
	name:		"ubd",
	config:		ubd_config,
	remove:		ubd_remove,
};

static int ubd_mc_init(void)
{
	mconsole_register_dev(&ubd_mc);
	return(0);
}

__initcall(ubd_mc_init);

static request_queue_t *ubd_get_queue(kdev_t device)
{
	return(ubd_queue);
}

int ubd_init(void)
{
        int i;

	ubd_dir_handle = devfs_mk_dir (NULL, "ubd", NULL);
	if(register_blkdev(MAJOR_NR, "ubd", &ubd_blops)){
		printk(KERN_ERR "ubd: unable to get major %d\n", MAJOR_NR);
		return -1;
	}
	ubd_queue = BLK_DEFAULT_QUEUE(MAJOR_NR);
	INIT_QUEUE(ubd_queue, do_ubd_request, &ubd_lock);
	INIT_ELV(ubd_queue, &ubd_queue->elevator);
	if(fake_major != 0){
		char name[sizeof("ubd_nnn\0")];

		snprintf(name, sizeof(name), "ubd_%d", fake_major);
		ubd_fake_dir_handle = devfs_mk_dir(NULL, name, NULL);
		if(register_blkdev(fake_major, "ubd", &ubd_blops)){
			printk(KERN_ERR "ubd: unable to get major %d\n",
			       fake_major);
			return -1;
		}
		blk_dev[fake_major].queue = ubd_get_queue;
	}
	for(i = 0; i < MAX_DEV; i++) 
		ubd_add(i);
	return(0);
}

late_initcall(ubd_init);

int ubd_driver_init(void){
	unsigned long stack;
	int err;

	if(sync){
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

static void ubd_close(struct ubd *dev)
{
	close_fd(dev->fd);
	if(dev->cow.file != NULL) {
		close_fd(dev->cow.fd);
		vfree(dev->cow.bitmap);
		dev->cow.bitmap = NULL;
	}
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
	close_fd(dev->fd);
	return(err);
}

static int ubd_open(struct inode *inode, struct file *filp)
{
	struct ubd *dev;
	int n, offset, err;

	n = DEVICE_NR(inode->i_rdev);
	dev = &ubd_dev[n];
	if(n > MAX_DEV)
		return -ENODEV;
	offset = n << UBD_SHIFT;
	if(dev->is_dir == 1)
		return(0);

	if(dev->count == 0){
		dev->openflags = dev->boot_openflags;

		err = ubd_open_dev(dev);
		if(err){
			printk(KERN_ERR "ubd%d: Can't open \"%s\": "
			       "errno = %d\n", n, dev->file, -err);
			return(err);
		}
		if(err) return(err);
	}
	dev->count++;
	if((filp->f_mode & FMODE_WRITE) && !dev->openflags.w){
	        if(--dev->count == 0) ubd_close(dev);
	        return -EROFS;
	}
	return(0);
}

static int ubd_release(struct inode * inode, struct file * file)
{
        int n, offset;

	n = DEVICE_NR(inode->i_rdev);
	offset = n << UBD_SHIFT;
	if(n > MAX_DEV)
		return -ENODEV;

	if(--ubd_dev[n].count == 0)
		ubd_close(&ubd_dev[n]);

	return(0);
}

int cow_read = 0;
int cow_write = 0;

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
				cow_read++;
			}
                }
        } 
        else {
		update_bitmap = 0;
		for(i = 0; i < req->length >> 9; i++){
			cow_write++;
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
	struct ubd *dev;
	__u64 block;
	int nsect, min, n;

	if(req->rq_status == RQ_INACTIVE) return(1);

	min = minor(req->rq_dev);
	n = min >> UBD_SHIFT;
	dev = &ubd_dev[n];
	if(dev->is_dir){
		strcpy(req->buffer, "HOSTFS:");
		strcat(req->buffer, dev->file);
		end_request(req, 1);
		return(1);
	}

	if((rq_data_dir(req) == WRITE) && !dev->openflags.w){
		printk("Write attempted on readonly ubd device %d\n", n);
		end_request(req, 0);
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
				ubd_finish(io_req.error);
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
 	struct ubd *dev;
	int n, min, err;

        if(!inode) return(-EINVAL);
	min = minor(inode->i_rdev);
	n = min >> UBD_SHIFT;
	if(n > MAX_DEV)
		return(-EINVAL);
	dev = &ubd_dev[n];
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
		if((arg > 1) || (min & ((1 << UBD_SHIFT) - 1)))
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
		if(min & ((1 << UBD_SHIFT) - 1))
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

static int ubd_revalidate(kdev_t rdev)
{
	__u64 size;
	int n, offset, err;
	struct ubd *dev;

	n = minor(rdev) >> UBD_SHIFT;
	dev = &ubd_dev[n];
	if(dev->is_dir) 
		return(0);
	
	err = ubd_file_size(dev, &size);
	if (!err) {
		set_capacity(&ubd_gendisk[n], size / 512);
		if(fake_major != 0)
			set_capacity(&fake_gendisk[n], size / 512);
		dev->size = size;
	}

	return err;
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
