/*
 * Video capture interface for Linux
 *
 *		A generic video device interface for the LINUX operating system
 *		using a set of device structures/vectors for low level operations.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Alan Cox, <alan@redhat.com>
 *
 * Fixes:	20000516  Claudio Matsuoka <claudio@conectiva.com>
 *		- Added procfs support
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/videodev.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/kmod.h>


#define VIDEO_NUM_DEVICES	256 

/*
 *	Active devices 
 */
 
static struct video_device *video_device[VIDEO_NUM_DEVICES];


#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)

#include <linux/proc_fs.h>

struct videodev_proc_data {
	struct list_head proc_list;
	char name[16];
	struct video_device *vdev;
	struct proc_dir_entry *proc_entry;
};

static struct proc_dir_entry *video_dev_proc_entry = NULL;
struct proc_dir_entry *video_proc_entry = NULL;
EXPORT_SYMBOL(video_proc_entry);
LIST_HEAD(videodev_proc_list);

#endif /* CONFIG_PROC_FS && CONFIG_VIDEO_PROC_FS */


#ifdef CONFIG_VIDEO_BWQCAM
extern int init_bw_qcams(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_CPIA
extern int cpia_init(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_PLANB
extern int init_planbs(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_ZORAN
extern int init_zoran_cards(struct video_init *);
#endif

static struct video_init video_init_list[]={
#ifdef CONFIG_VIDEO_BWQCAM
	{"bw-qcam", init_bw_qcams},
#endif	
#ifdef CONFIG_VIDEO_CPIA
	{"cpia", cpia_init},
#endif	
#ifdef CONFIG_VIDEO_PLANB
	{"planb", init_planbs},
#endif
#ifdef CONFIG_VIDEO_ZORAN
	{"zoran", init_zoran_cards},
#endif	
	{"end", NULL}
};

/*
 *	Read will do some smarts later on. Buffer pin etc.
 */
 
static ssize_t video_read(struct file *file,
	char *buf, size_t count, loff_t *ppos)
{
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	if(vfl->read)
		return vfl->read(vfl, buf, count, file->f_flags&O_NONBLOCK);
	else
		return -EINVAL;
}


/*
 *	Write for now does nothing. No reason it shouldnt do overlay setting
 *	for some boards I guess..
 */

static ssize_t video_write(struct file *file, const char *buf, 
	size_t count, loff_t *ppos)
{
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	if(vfl->write)
		return vfl->write(vfl, buf, count, file->f_flags&O_NONBLOCK);
	else
		return 0;
}

/*
 *	Poll to see if we're readable, can probably be used for timing on incoming
 *  frames, etc..
 */

static unsigned int video_poll(struct file *file, poll_table * wait)
{
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	if(vfl->poll)
		return vfl->poll(vfl, file, wait);
	else
		return 0;
}


/*
 *	Open a video device.
 */

static int video_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int err, retval = 0;
	struct video_device *vfl;
	
	if(minor>=VIDEO_NUM_DEVICES)
		return -ENODEV;
	lock_kernel();		
	vfl=video_device[minor];
	if(vfl==NULL) {
		char modname[20];

		sprintf (modname, "char-major-%d-%d", VIDEO_MAJOR, minor);
		request_module(modname);
		vfl=video_device[minor];
		if (vfl==NULL) {
			retval = -ENODEV;
			goto error_out;
		}
	}
	if(vfl->busy) {
		retval = -EBUSY;
		goto error_out;
	}
	vfl->busy=1;		/* In case vfl->open sleeps */
	unlock_kernel();
	
	if(vfl->open)
	{
		err=vfl->open(vfl,0);	/* Tell the device it is open */
		if(err)
		{
			vfl->busy=0;
			return err;
		}
	}
	return 0;
error_out:
	unlock_kernel();
	return retval;
}

/*
 *	Last close of a video for Linux device
 */
	
static int video_release(struct inode *inode, struct file *file)
{
	struct video_device *vfl;
	lock_kernel();
	vfl=video_device[MINOR(inode->i_rdev)];
	if(vfl->close)
		vfl->close(vfl);
	vfl->busy=0;
	unlock_kernel();
	return 0;
}

/*
 *	Question: Should we be able to capture and then seek around the
 *	image ?
 */
 
static long long video_lseek(struct file * file,
			  long long offset, int origin)
{
	return -ESPIPE;
}

static int video_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct video_device *vfl=video_device[MINOR(inode->i_rdev)];
	int err=vfl->ioctl(vfl, cmd, (void *)arg);

	if(err!=-ENOIOCTLCMD)
		return err;
	
	switch(cmd)
	{
		default:
			return -EINVAL;
	}
}

/*
 *	We need to do MMAP support
 */
 
 
int video_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = -EINVAL;
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	if(vfl->mmap) {
		lock_kernel();
		ret = vfl->mmap(vfl, (char *)vma->vm_start, 
				(unsigned long)(vma->vm_end-vma->vm_start));
		unlock_kernel();
	}
	return ret;
}

/*
 *	/proc support
 */

#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)

/* Hmm... i'd like to see video_capability information here, but
 * how can I access it (without changing the other drivers? -claudio
 */
static int videodev_proc_read(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	char *out = page;
	struct video_device *vfd = data;
	struct videodev_proc_data *d;
	struct list_head *tmp;
	int len;
	char c = ' ';

	list_for_each (tmp, &videodev_proc_list) {
		d = list_entry(tmp, struct videodev_proc_data, proc_list);
		if (vfd == d->vdev)
			break;
	}

	/* Sanity check */
	if (tmp == &videodev_proc_list)
		goto skip;
		
#define PRINT_VID_TYPE(x) do { if (vfd->type & x) \
	out += sprintf (out, "%c%s", c, #x); c='|';} while (0)

	out += sprintf (out, "name            : %s\n", vfd->name);
	out += sprintf (out, "type            :");
		PRINT_VID_TYPE(VID_TYPE_CAPTURE);
		PRINT_VID_TYPE(VID_TYPE_TUNER);
		PRINT_VID_TYPE(VID_TYPE_TELETEXT);
		PRINT_VID_TYPE(VID_TYPE_OVERLAY);
		PRINT_VID_TYPE(VID_TYPE_CHROMAKEY);
		PRINT_VID_TYPE(VID_TYPE_CLIPPING);
		PRINT_VID_TYPE(VID_TYPE_FRAMERAM);
		PRINT_VID_TYPE(VID_TYPE_SCALES);
		PRINT_VID_TYPE(VID_TYPE_MONOCHROME);
		PRINT_VID_TYPE(VID_TYPE_SUBCAPTURE);
		PRINT_VID_TYPE(VID_TYPE_MPEG_DECODER);
		PRINT_VID_TYPE(VID_TYPE_MPEG_ENCODER);
		PRINT_VID_TYPE(VID_TYPE_MJPEG_DECODER);
		PRINT_VID_TYPE(VID_TYPE_MJPEG_ENCODER);
	out += sprintf (out, "\n");
	out += sprintf (out, "hardware        : 0x%x\n", vfd->hardware);
#if 0
	out += sprintf (out, "channels        : %d\n", d->vcap.channels);
	out += sprintf (out, "audios          : %d\n", d->vcap.audios);
	out += sprintf (out, "maxwidth        : %d\n", d->vcap.maxwidth);
	out += sprintf (out, "maxheight       : %d\n", d->vcap.maxheight);
	out += sprintf (out, "minwidth        : %d\n", d->vcap.minwidth);
	out += sprintf (out, "minheight       : %d\n", d->vcap.minheight);
#endif

skip:
	len = out - page;
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;

	*start = page + off;

	return len;
}

static void videodev_proc_create(void)
{
	video_proc_entry = create_proc_entry("video", S_IFDIR, &proc_root);

	if (video_proc_entry == NULL) {
		printk("video_dev: unable to initialise /proc/video\n");
		return;
	}

	video_proc_entry->owner = THIS_MODULE;
	video_dev_proc_entry = create_proc_entry("dev", S_IFDIR, video_proc_entry);

	if (video_dev_proc_entry == NULL) {
		printk("video_dev: unable to initialise /proc/video/dev\n");
		return;
	}

	video_dev_proc_entry->owner = THIS_MODULE;
}

#ifdef MODULE
#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
static void videodev_proc_destroy(void)
{
	if (video_dev_proc_entry != NULL)
		remove_proc_entry("dev", video_proc_entry);

	if (video_proc_entry != NULL)
		remove_proc_entry("video", &proc_root);
}
#endif
#endif

static void videodev_proc_create_dev (struct video_device *vfd, char *name)
{
	struct videodev_proc_data *d;
	struct proc_dir_entry *p;

	if (video_dev_proc_entry == NULL)
		return;

	d = kmalloc (sizeof (struct videodev_proc_data), GFP_KERNEL);
	if (!d)
		return;

	p = create_proc_entry(name, S_IFREG|S_IRUGO|S_IWUSR, video_dev_proc_entry);
	p->data = vfd;
	p->read_proc = videodev_proc_read;

	d->proc_entry = p;
	d->vdev = vfd;
	strcpy (d->name, name);

	/* How can I get capability information ? */

	list_add (&d->proc_list, &videodev_proc_list);
}

static void videodev_proc_destroy_dev (struct video_device *vfd)
{
	struct list_head *tmp;
	struct videodev_proc_data *d;

	list_for_each (tmp, &videodev_proc_list) {
		d = list_entry(tmp, struct videodev_proc_data, proc_list);
		if (vfd == d->vdev) {
			remove_proc_entry(d->name, video_dev_proc_entry);
			list_del (&d->proc_list);
			kfree (d);
			break;
		}
	}
}

#endif /* CONFIG_VIDEO_PROC_FS */

extern struct file_operations video_fops;

/**
 *	video_register_device - register video4linux devices
 *	@vfd: video device structure we want to register
 *	@type: type of device to register
 *	FIXME: needs a semaphore on 2.3.x
 *	
 *	The registration code assigns minor numbers based on the type
 *	requested. -ENFILE is returned in all the device slots for this
 *	category are full. If not then the minor field is set and the
 *	driver initialize function is called (if non %NULL).
 *
 *	Zero is returned on success.
 *
 *	Valid types are
 *
 *	%VFL_TYPE_GRABBER - A frame grabber
 *
 *	%VFL_TYPE_VTX - A teletext device
 *
 *	%VFL_TYPE_VBI - Vertical blank data (undecoded)
 *
 *	%VFL_TYPE_RADIO - A radio card	
 */
 
int video_register_device(struct video_device *vfd, int type)
{
	int i=0;
	int base;
	int err;
	int end;
	char *name_base;
	
	switch(type)
	{
		case VFL_TYPE_GRABBER:
			base=0;
			end=64;
			name_base = "video";
			break;
		case VFL_TYPE_VTX:
			base=192;
			end=224;
			name_base = "vtx";
			break;
		case VFL_TYPE_VBI:
			base=224;
			end=240;
			name_base = "vbi";
			break;
		case VFL_TYPE_RADIO:
			base=64;
			end=128;
			name_base = "radio";
			break;
		default:
			return -1;
	}
	
	for(i=base;i<end;i++)
	{
		if(video_device[i]==NULL)
		{
			char name[16];

			video_device[i]=vfd;
			vfd->minor=i;
			/* The init call may sleep so we book the slot out
			   then call */
			MOD_INC_USE_COUNT;
			if(vfd->initialize)
			{
				err=vfd->initialize(vfd);
				if(err<0)
				{
					video_device[i]=NULL;
					MOD_DEC_USE_COUNT;
					return err;
				}
			}
			sprintf (name, "v4l/%s%d", name_base, i - base);
			/*
			 *	Start the device root only. Anything else
			 *	has serious privacy issues.
			 */
			vfd->devfs_handle =
			    devfs_register (NULL, name, DEVFS_FL_DEFAULT,
					    VIDEO_MAJOR, vfd->minor,
					    S_IFCHR | S_IRUSR | S_IWUSR,
					    &video_fops, NULL);

#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
			sprintf (name, "%s%d", name_base, i - base);
			videodev_proc_create_dev (vfd, name);
#endif
			

			return 0;
		}
	}
	return -ENFILE;
}

/**
 *	video_unregister_device - unregister a video4linux device
 *	@vfd: the device to unregister
 *
 *	This unregisters the passed device and deassigns the minor
 *	number. Future open calls will be met with errors.
 */
 
void video_unregister_device(struct video_device *vfd)
{
	if(video_device[vfd->minor]!=vfd)
		panic("vfd: bad unregister");

#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
	videodev_proc_destroy_dev (vfd);
#endif

	devfs_unregister (vfd->devfs_handle);
	video_device[vfd->minor]=NULL;
	MOD_DEC_USE_COUNT;
}


static struct file_operations video_fops=
{
	owner:		THIS_MODULE,
	llseek:		video_lseek,
	read:		video_read,
	write:		video_write,
	ioctl:		video_ioctl,
	mmap:		video_mmap,
	open:		video_open,
	release:	video_release,
	poll:		video_poll,
};

/*
 *	Initialise video for linux
 */
 
int __init videodev_init(void)
{
	struct video_init *vfli = video_init_list;
	
	printk(KERN_INFO "Linux video capture interface: v1.00\n");
	if(devfs_register_chrdev(VIDEO_MAJOR,"video_capture", &video_fops))
	{
		printk("video_dev: unable to get major %d\n", VIDEO_MAJOR);
		return -EIO;
	}

	/*
	 *	Init kernel installed video drivers
	 */
	 	
#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
	videodev_proc_create ();
#endif
	
	while(vfli->init!=NULL)
	{
		vfli->init(vfli);
		vfli++;
	}
	return 0;
}

#ifdef MODULE		
int init_module(void)
{
	return videodev_init();
}

void cleanup_module(void)
{
#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
	videodev_proc_destroy ();
#endif
	
	devfs_unregister_chrdev(VIDEO_MAJOR, "video_capture");
}

#endif

EXPORT_SYMBOL(video_register_device);
EXPORT_SYMBOL(video_unregister_device);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("Device registrar for Video4Linux drivers");
