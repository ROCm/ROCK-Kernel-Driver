/*
 * LIRC base driver
 *
 * (L) by Artur Lipowski <alipowski@interia.pl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: lirc_dev.c,v 1.27 2004/01/13 13:59:48 lirc Exp $
 *
 */

#include <linux/version.h>

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/errno.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>

#include <linux/lirc.h>

#include "lirc_dev.h"

static int debug = 0;

MODULE_PARM(debug,"i");

#define IRCTL_DEV_NAME    "BaseRemoteCtl"
#define SUCCESS           0
#define NOPLUG            -1
#define dprintk           if (debug) printk

#define LOGHEAD           "lirc_dev (%s[%d]): "

struct irctl
{
	struct lirc_plugin p;
	int open;

	struct lirc_buffer *buf;

	int t_pid;

	struct semaphore *t_notify;
	struct semaphore *t_notify2;
	int shutdown;
	long jiffies_to_wait;
};

DECLARE_MUTEX(plugin_lock);

static struct irctl irctls[CONFIG_LIRC_MAX_DEV];
static struct file_operations fops;


/*  helper function
 *  initializes the irctl structure
 */
static inline void init_irctl(struct irctl *ir)
{
	memset(&ir->p, 0, sizeof(struct lirc_plugin));
	ir->p.minor = NOPLUG;

	ir->t_pid = -1;
	ir->t_notify = NULL;
	ir->t_notify2 = NULL;
	ir->shutdown = 0;

	ir->jiffies_to_wait = 0;

	ir->open = 0;
}


/*  helper function
 *  reads key codes from plugin and puts them into buffer
 *  buffer free space is checked and locking performed
 *  returns 0 on success
 */

inline static int add_to_buf(struct irctl *ir)
{
	if (lirc_buffer_full(ir->buf)) {
		dprintk(LOGHEAD "buffer overflow\n",
			ir->p.name, ir->p.minor);
		return -EOVERFLOW;
	}

    if(ir->p.add_to_buf) {
        int res = -ENODATA;
        int got_data = 0;
        
        /* Service the device as long as it is returning
         * data and we have space
         */
        while( !lirc_buffer_full(ir->buf) )
        {
            res = ir->p.add_to_buf( ir->p.data, ir->buf );
            if( res == SUCCESS )
                got_data++;
            else
                break;
        }

        if( res == -ENODEV )
        {
            ir->shutdown = 1;
        }
        return (got_data ? SUCCESS : res);
    }

	return SUCCESS;
}

/* main function of the polling thread
 */
static int lirc_thread(void *irctl)
{
	struct irctl *ir = irctl;

	daemonize("lirc_dev");

	if (ir->t_notify != NULL) {
		up(ir->t_notify);
	}

	dprintk(LOGHEAD "poll thread started\n", ir->p.name, ir->p.minor);

	do {
		if (ir->open) {
			if (ir->jiffies_to_wait) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(ir->jiffies_to_wait);
			} else {
				interruptible_sleep_on(ir->p.get_queue(ir->p.data));
			}
			if (ir->shutdown || !ir->open) {
				break;
			}
			if (!add_to_buf(ir)) {
				wake_up_interruptible(&ir->buf->wait_poll);
			}
		} else {
			/* if device not opened so we can sleep half a second */
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(HZ/2);
		}
	} while (!ir->shutdown);

	dprintk(LOGHEAD "poll thread ended\n", ir->p.name, ir->p.minor);

	if (ir->t_notify2 != NULL) {
		down(ir->t_notify2);
	}

	ir->t_pid = -1;

	if (ir->t_notify != NULL) {
		up(ir->t_notify);
	}

	return 0;
}

/*
 *
 */
int lirc_register_plugin(struct lirc_plugin *p)
{
	struct irctl *ir;
	int minor;
	int bytes_in_key;
	DECLARE_MUTEX_LOCKED(tn);

	if (!p) {
		printk("lirc_dev: lirc_register_plugin:"
		       "plugin pointer must be not NULL!\n");
		return -EBADRQC;
	}

	if (CONFIG_LIRC_MAX_DEV <= p->minor) {
		printk("lirc_dev: lirc_register_plugin:"
		       "\" minor\" must be beetween 0 and %d (%d)!\n",
		       CONFIG_LIRC_MAX_DEV-1, p->minor);
		return -EBADRQC;
	}

	if (1 > p->code_length || (BUFLEN*8) < p->code_length) {
		printk("lirc_dev: lirc_register_plugin:"
		       "code length in bits for minor (%d) "
		       "must be less than %d!\n",
		       p->minor, BUFLEN*8);
		return -EBADRQC;
	}

	printk("lirc_dev: lirc_register_plugin:"
	       "sample_rate: %d\n",p->sample_rate);
	if (p->sample_rate) {
		if (2 > p->sample_rate || 100 < p->sample_rate) {
			printk("lirc_dev: lirc_register_plugin:"
			       "sample_rate must be beetween 2 and 100!\n");
			return -EBADRQC;
		}
        if (!p->add_to_buf) {
            printk("lirc_dev: lirc_register_plugin:"
                   "add_to_buf cannot be NULL when sample_rate is set\n");
            return -EBADRQC;
        }
	} else if (!(p->fops && p->fops->read)
			&& !p->get_queue && !p->rbuf) {
		printk("lirc_dev: lirc_register_plugin:"
		       "fops->read, get_queue and rbuf cannot all be NULL!\n");
		return -EBADRQC;
	} else if (!p->get_queue && !p->rbuf) {
		if (!(p->fops && p->fops->read && p->fops->poll)
				|| (!p->fops->ioctl && !p->ioctl)) {
			printk("lirc_dev: lirc_register_plugin:"
			       "neither read, poll nor ioctl can be NULL!\n");
			return -EBADRQC;
		}
	}

	down_interruptible(&plugin_lock);

	minor = p->minor;

	if (0 > minor) {
		/* find first free slot for plugin */
		for (minor=0; minor<CONFIG_LIRC_MAX_DEV; minor++)
			if (irctls[minor].p.minor == NOPLUG)
				break;
		if (CONFIG_LIRC_MAX_DEV == minor) {
			printk("lirc_dev: lirc_register_plugin: "
			       "no free slots for plugins!\n");
			up(&plugin_lock);
			return -ENOMEM;
		}
	} else if (irctls[minor].p.minor != NOPLUG) {
		printk("lirc_dev: lirc_register_plugin:"
		       "minor (%d) just registerd!\n", minor);
		up(&plugin_lock);
		return -EBUSY;
	}

	ir = &irctls[minor];

	if (p->sample_rate) {
		ir->jiffies_to_wait = HZ / p->sample_rate;
	} else {
                /* it means - wait for externeal event in task queue */
		ir->jiffies_to_wait = 0;
	}

	/* some safety check 8-) */
	p->name[sizeof(p->name)-1] = '\0';

	bytes_in_key = p->code_length/8 + (p->code_length%8 ? 1 : 0);

	if (p->rbuf) {
		ir->buf = p->rbuf;
	} else {
		ir->buf = kmalloc(sizeof(struct lirc_buffer), GFP_KERNEL);
		lirc_buffer_init(ir->buf, bytes_in_key, BUFLEN/bytes_in_key);
	}

	if (p->features==0)
		p->features = (p->code_length > 8) ?
			LIRC_CAN_REC_LIRCCODE : LIRC_CAN_REC_CODE;

	ir->p = *p;
	ir->p.minor = minor;

#ifdef CONFIG_DEVFS_FS
	devfs_mk_cdev(MKDEV(IRCTL_DEV_MAJOR, ir->p.minor), S_IFCHR | S_IRUSR | S_IWUSR, "lirc/lirc%d", ir->p.minor);
#endif

	if(p->sample_rate || p->get_queue) {
		/* try to fire up polling thread */
		ir->t_notify = &tn;
		ir->t_pid = kernel_thread(lirc_thread, (void*)ir, 0);
		if (ir->t_pid < 0) {
			up(&plugin_lock);
			printk("lirc_dev: lirc_register_plugin:"
			       "cannot run poll thread for minor = %d\n",
			       p->minor);
			return -ECHILD;
		}
		down(&tn);
		ir->t_notify = NULL;
	}
	up(&plugin_lock);

	try_module_get(THIS_MODULE);

	dprintk("lirc_dev: plugin %s registered at minor number = %d\n",
		ir->p.name, ir->p.minor);

	return minor;
}

/*
 *
 */
int lirc_unregister_plugin(int minor)
{
	struct irctl *ir;
	DECLARE_MUTEX_LOCKED(tn);
	DECLARE_MUTEX_LOCKED(tn2);

	if (minor < 0 || minor >= CONFIG_LIRC_MAX_DEV) {
		printk("lirc_dev: lirc_unregister_plugin:"
		       "\" minor\" must be beetween 0 and %d!\n",
		       CONFIG_LIRC_MAX_DEV-1);
		return -EBADRQC;
	}

	ir = &irctls[minor];

	down_interruptible(&plugin_lock);

	if (ir->p.minor != minor) {
		printk("lirc_dev: lirc_unregister_plugin:"
		       "minor (%d) device not registered!", minor);
		up(&plugin_lock);
		return -ENOENT;
	}

	if (ir->open) {
		printk("lirc_dev: lirc_unregister_plugin:"
		       "plugin %s[%d] in use!", ir->p.name, ir->p.minor);
		up(&plugin_lock);
		return -EBUSY;
	}

	/* end up polling thread */
	if (ir->t_pid >= 0) {
		ir->t_notify = &tn;
		ir->t_notify2 = &tn2;
		ir->shutdown = 1;
		{
			struct task_struct *p;

			p = find_task_by_pid(ir->t_pid);
			wake_up_process(p);
		}
		up(&tn2);
		down(&tn);
		ir->t_notify = NULL;
		ir->t_notify2 = NULL;
	}

	dprintk("lirc_dev: plugin %s unregistered from minor number = %d\n",
		ir->p.name, ir->p.minor);

#ifdef CONFIG_DEVFS_FS
	devfs_remove("lirc/lirc%d", ir->p.minor);
#endif

	if (ir->buf != ir->p.rbuf){
		lirc_buffer_free(ir->buf);
		kfree(ir->buf);
	}
	ir->buf = NULL;
	init_irctl(ir);
	up(&plugin_lock);

	module_put(THIS_MODULE);

	return SUCCESS;
}

/*
 *
 */
static int irctl_open(struct inode *inode, struct file *file)
{
	struct irctl *ir;
	int retval;

	if (MINOR(inode->i_rdev) >= CONFIG_LIRC_MAX_DEV) {
		dprintk("lirc_dev [%d]: open result = -ENODEV\n",
			MINOR(inode->i_rdev));
		return -ENODEV;
	}

	ir = &irctls[MINOR(inode->i_rdev)];

	dprintk(LOGHEAD "open called\n", ir->p.name, ir->p.minor);

	/* if the plugin has an open function use it instead */
	if(ir->p.fops && ir->p.fops->open)
		return ir->p.fops->open(inode, file);

	down_interruptible(&plugin_lock);

	if (ir->p.minor == NOPLUG) {
		up(&plugin_lock);
		dprintk(LOGHEAD "open result = -ENODEV\n",
			ir->p.name, ir->p.minor);
		return -ENODEV;
	}

	if (ir->open) {
		up(&plugin_lock);
		dprintk(LOGHEAD "open result = -EBUSY\n",
			ir->p.name, ir->p.minor);
		return -EBUSY;
	}

	/* there is no need for locking here because ir->open is 0
         * and lirc_thread isn't using buffer
	 * plugins which use irq's should allocate them on set_use_inc,
	 * so there should be no problem with those either.
         */
	ir->buf->head = ir->buf->tail;
	ir->buf->fill = 0;

	++ir->open;
	retval = ir->p.set_use_inc(ir->p.data);

	up(&plugin_lock);

	if (retval != SUCCESS) {
		--ir->open;
		return retval;
	}

	dprintk(LOGHEAD "open result = %d\n", ir->p.name, ir->p.minor, SUCCESS);

	return SUCCESS;
}

/*
 *
 */
static int irctl_close(struct inode *inode, struct file *file)
{
	struct irctl *ir = &irctls[MINOR(inode->i_rdev)];

	dprintk(LOGHEAD "close called\n", ir->p.name, ir->p.minor);

	/* if the plugin has a close function use it instead */
	if(ir->p.fops && ir->p.fops->release)
		return ir->p.fops->release(inode, file);

	down_interruptible(&plugin_lock);

	--ir->open;
	ir->p.set_use_dec(ir->p.data);

	up(&plugin_lock);

	return SUCCESS;
}

/*
 *
 */
static unsigned int irctl_poll(struct file *file, poll_table *wait)
{
	struct irctl *ir = &irctls[MINOR(file->f_dentry->d_inode->i_rdev)];

	dprintk(LOGHEAD "poll called\n", ir->p.name, ir->p.minor);

	/* if the plugin has a poll function use it instead */
	if(ir->p.fops && ir->p.fops->poll)
		return ir->p.fops->poll(file, wait);

	poll_wait(file, &ir->buf->wait_poll, wait);

	dprintk(LOGHEAD "poll result = %s\n",
		ir->p.name, ir->p.minor,
		lirc_buffer_empty(ir->buf) ? "0" : "POLLIN|POLLRDNORM");

	return lirc_buffer_empty(ir->buf) ? 0 : (POLLIN|POLLRDNORM);
}

/*
 *
 */
static int irctl_ioctl(struct inode *inode, struct file *file,
                       unsigned int cmd, unsigned long arg)
{
	unsigned long mode;
	int result;
	struct irctl *ir = &irctls[MINOR(inode->i_rdev)];

	dprintk(LOGHEAD "ioctl called (%u)\n",
		ir->p.name, ir->p.minor, cmd);

	/* if the plugin has a ioctl function use it instead */
	if(ir->p.fops && ir->p.fops->ioctl)
		return ir->p.fops->ioctl(inode, file, cmd, arg);

	if (ir->p.minor == NOPLUG) {
		dprintk(LOGHEAD "ioctl result = -ENODEV\n",
			ir->p.name, ir->p.minor);
		return -ENODEV;
	}

	/* Give the plugin a chance to handle the ioctl */
	if(ir->p.ioctl){
		result = ir->p.ioctl(inode, file, cmd, arg);
		if (result != -ENOIOCTLCMD)
			return result;
	}
	/* The plugin can't handle cmd */
	result = SUCCESS;

	switch(cmd)
	{
	case LIRC_GET_FEATURES:
		result = put_user(ir->p.features, (unsigned long*)arg);
		break;
	case LIRC_GET_REC_MODE:
		if(!(ir->p.features&LIRC_CAN_REC_MASK))
			return(-ENOSYS);

		result = put_user(LIRC_REC2MODE
				  (ir->p.features&LIRC_CAN_REC_MASK),
				  (unsigned long*)arg);
		break;
	case LIRC_SET_REC_MODE:
		if(!(ir->p.features&LIRC_CAN_REC_MASK))
			return(-ENOSYS);

		result = get_user(mode, (unsigned long*)arg);
		if(!result && !(LIRC_MODE2REC(mode) & ir->p.features)) {
			result = -EINVAL;
		}
		/* FIXME: We should actually set the mode somehow
		 * but for now, lirc_serial doesn't support mode changin
		 * eighter */
		break;
	case LIRC_GET_LENGTH:
		result = put_user((unsigned long)ir->p.code_length,
				  (unsigned long *)arg);
		break;
	default:
		result = -ENOIOCTLCMD;
	}

	dprintk(LOGHEAD "ioctl result = %d\n",
		ir->p.name, ir->p.minor, result);

	return result;
}

/*
 *
 */
static ssize_t irctl_read(struct file *file,
			  char *buffer,
			  size_t length,
			  loff_t *ppos)
{
	struct irctl *ir = &irctls[MINOR(file->f_dentry->d_inode->i_rdev)];
	unsigned char buf[ir->buf->chunk_size];
	int ret=0, written=0;
	DECLARE_WAITQUEUE(wait, current);

	dprintk(LOGHEAD "read called\n", ir->p.name, ir->p.minor);

	/* if the plugin has a specific read function use it instead */
	if(ir->p.fops && ir->p.fops->read)
		return ir->p.fops->read(file, buffer, length, ppos);

	if (length % ir->buf->chunk_size) {
		dprintk(LOGHEAD "read result = -EINVAL\n",
			ir->p.name, ir->p.minor);
		return -EINVAL;
	}

	/* we add ourselves to the task queue before buffer check
         * to avoid losing scan code (in case when queue is awaken somewhere
	 * beetwen while condition checking and scheduling)
	 */
	add_wait_queue(&ir->buf->wait_poll, &wait);
	current->state = TASK_INTERRUPTIBLE;

	/* while we did't provide 'length' bytes, device is opened in blocking
	 * mode and 'copy_to_user' is happy, wait for data.
	 */
	while (written < length && ret == 0) {
		if (lirc_buffer_empty(ir->buf)) {
			/* According to the read(2) man page, 'written' can be
			 * returned as less than 'length', instead of blocking
			 * again, returning -EWOULDBLOCK, or returning
			 * -ERESTARTSYS */
			if (written) break;
			if (file->f_flags & O_NONBLOCK) {
				dprintk(LOGHEAD "read result = -EWOULDBLOCK\n",
						ir->p.name, ir->p.minor);
				remove_wait_queue(&ir->buf->wait_poll, &wait);
				current->state = TASK_RUNNING;
				return -EWOULDBLOCK;
			}
			if (signal_pending(current)) {
				dprintk(LOGHEAD "read result = -ERESTARTSYS\n",
						ir->p.name, ir->p.minor);
				remove_wait_queue(&ir->buf->wait_poll, &wait);
				current->state = TASK_RUNNING;
				return -ERESTARTSYS;
			}
			schedule();
			current->state = TASK_INTERRUPTIBLE;
		} else {
			lirc_buffer_read_1(ir->buf, buf);
			ret = copy_to_user((void *)buffer+written, buf,
					   ir->buf->chunk_size);
			written += ir->buf->chunk_size;
		}
	}

	remove_wait_queue(&ir->buf->wait_poll, &wait);
	current->state = TASK_RUNNING;

	dprintk(LOGHEAD "read result = %s (%d)\n",
		ir->p.name, ir->p.minor, ret ? "-EFAULT" : "OK", ret);

	return ret ? -EFAULT : written;
}

static ssize_t irctl_write(struct file *file, const char *buffer,
			   size_t length, loff_t * ppos)
{
	struct irctl *ir = &irctls[MINOR(file->f_dentry->d_inode->i_rdev)];

	dprintk(LOGHEAD "write called\n", ir->p.name, ir->p.minor);

	/* if the plugin has a specific read function use it instead */
	if(ir->p.fops && ir->p.fops->write)
		return ir->p.fops->write(file, buffer, length, ppos);

	return -EINVAL;
}


static struct file_operations fops = {
	read:    irctl_read,
	write:   irctl_write,
	poll:    irctl_poll,
	ioctl:   irctl_ioctl,
	open:    irctl_open,
	release: irctl_close
};

static int __init lirc_dev_init(void)
{
	int i;

	for (i=0; i < CONFIG_LIRC_MAX_DEV; ++i) {
		init_irctl(&irctls[i]);
	}

	i = register_chrdev(IRCTL_DEV_MAJOR,
				   IRCTL_DEV_NAME,
				   &fops);

	if (i < 0) {
		printk ("lirc_dev: device registration failed with %d\n", i);
		return i;
	}

	printk("lirc_dev: IR Remote Control driver registered, at major %d \n",
	       IRCTL_DEV_MAJOR);

	return SUCCESS;
}

static void __exit lirc_dev_exit(void)
{
	int ret;

	ret = unregister_chrdev(IRCTL_DEV_MAJOR, IRCTL_DEV_NAME);

	if (0 > ret){
		printk("lirc_dev: error in module_unregister_chrdev: %d\n",
		       ret);
	} else {
		dprintk("lirc_dev: module successfully unloaded\n");
	}
}

/* ---------------------------------------------------------------------- */

/* For now dont try to use it as a static version !  */

MODULE_DESCRIPTION("LIRC base driver module");
MODULE_AUTHOR("Artur Lipowski");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(lirc_register_plugin);
EXPORT_SYMBOL(lirc_unregister_plugin);

module_init(lirc_dev_init);
module_exit(lirc_dev_exit);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 4
 * End:
 */
