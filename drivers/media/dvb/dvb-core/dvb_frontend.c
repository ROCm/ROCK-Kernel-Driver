/*
 * dvb-core.c: DVB core driver
 *
 * Copyright (C) 1999-2001 Ralph  Metzler
 *                         Marcus Metzler
 *                         Holger Waechtler 
 *                                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/list.h>
#include <asm/processor.h>
#include <asm/semaphore.h>

#include "dvb_frontend.h"
#include "dvbdev.h"
#include "dvb_functions.h"


static int dvb_frontend_debug = 0;
static int dvb_shutdown_timeout = 5;

#define dprintk if (dvb_frontend_debug) printk

#define MAX_EVENT 8

struct dvb_fe_events {
	struct dvb_frontend_event events[MAX_EVENT];
	int                       eventw;
	int                       eventr;
	int                       overflow;
	wait_queue_head_t         wait_queue;
	struct semaphore          sem;
};


struct dvb_frontend_data {
	struct dvb_frontend_info *info;
	struct dvb_frontend frontend;
	struct dvb_device *dvbdev;
	struct dvb_frontend_parameters parameters;
	struct dvb_fe_events events;
	struct semaphore sem;
	struct list_head list_head;
	wait_queue_head_t wait_queue;
	pid_t thread_pid;
	unsigned long release_jiffies;
	unsigned long lost_sync_jiffies;
	int bending;
	int lnb_drift;
	int timeout_count;
	int lost_sync_count;
	int exit;
        fe_status_t status;
};


struct dvb_frontend_ioctl_data {
	struct list_head list_head;
	struct dvb_adapter *adapter;
	int (*before_ioctl) (struct dvb_frontend *frontend,
			     unsigned int cmd, void *arg);
	int (*after_ioctl)  (struct dvb_frontend *frontend,
			     unsigned int cmd, void *arg);
	void *before_after_data;
};


struct dvb_frontend_notifier_data {
	struct list_head list_head;
	struct dvb_adapter *adapter;
	void (*callback) (fe_status_t s, void *data);
	void *data;
};


static LIST_HEAD(frontend_list);
static LIST_HEAD(frontend_ioctl_list);
static LIST_HEAD(frontend_notifier_list);

static DECLARE_MUTEX(frontend_mutex);


static int dvb_frontend_internal_ioctl (struct dvb_frontend *frontend, 
				 unsigned int cmd, void *arg)
{
	int err = -EOPNOTSUPP;

	dprintk ("%s\n", __FUNCTION__);

	if (frontend->before_ioctl)
		err = frontend->before_ioctl (frontend, cmd, arg);

	if (err == -EOPNOTSUPP) {
		err = frontend->ioctl (frontend, cmd, arg);

		if ((err == -EOPNOTSUPP) && frontend->after_ioctl)
			err = frontend->after_ioctl (frontend, cmd, arg);
	}

	return err;
}


/**
 *  if 2 tuners are located side by side you can get interferences when
 *  they try to tune to the same frequency, so both lose sync.
 *  We will slightly mistune in this case. The AFC of the demodulator
 *  should make it still possible to receive the requested transponder 
 *  on both tuners...
 */
static void dvb_bend_frequency (struct dvb_frontend_data *this_fe, int recursive)
{
	struct list_head *entry;
	int stepsize = this_fe->info->frequency_stepsize;
	int this_fe_adap_num = this_fe->frontend.i2c->adapter->num;
	int frequency;

	if (!stepsize || recursive > 10) {
		printk ("%s: too deep recursion, check frequency_stepsize "
			"in your frontend code!\n", __FUNCTION__);
		return;
	}

	dprintk ("%s\n", __FUNCTION__);

	if (!recursive) {
		if (down_interruptible (&frontend_mutex))
			return;

		this_fe->bending = 0;
	}

	list_for_each (entry, &frontend_list) {
		struct dvb_frontend_data *fe;
		int f;

		fe = list_entry (entry, struct dvb_frontend_data, list_head);

		if (fe->frontend.i2c->adapter->num != this_fe_adap_num)
			continue;

		f = fe->parameters.frequency;
		f += fe->lnb_drift;
		f += fe->bending;

		frequency = this_fe->parameters.frequency;
		frequency += this_fe->lnb_drift;
		frequency += this_fe->bending;

		if (this_fe != fe && fe->lost_sync_count != -1 &&
                    frequency > f - stepsize && frequency < f + stepsize)
		{
			if (recursive % 2)
				this_fe->bending += stepsize;
			else
				this_fe->bending = -this_fe->bending;

			dvb_bend_frequency (this_fe, recursive + 1);
			goto done;
		}
	}
done:
	if (!recursive)
		up (&frontend_mutex);
}


static void dvb_call_frontend_notifiers (struct dvb_frontend_data *fe,
				  fe_status_t s)
{
	dprintk ("%s\n", __FUNCTION__);

	if ((fe->status & FE_HAS_LOCK) && !(s & FE_HAS_LOCK))
		fe->lost_sync_jiffies = jiffies;

	if (((s ^ fe->status) & FE_HAS_LOCK) && (s & FE_HAS_LOCK))
		dvb_delay (fe->info->notifier_delay);

	fe->status = s;

	if (!(s & FE_HAS_LOCK) && (fe->info->caps & FE_CAN_MUTE_TS))
		return;

	/**
	 *   now tell the Demux about the TS status changes...
	 */
	if (fe->frontend.notifier_callback)
		fe->frontend.notifier_callback(fe->status, fe->frontend.notifier_data);
}


static void dvb_frontend_add_event (struct dvb_frontend_data *fe, fe_status_t status)
{
	struct dvb_fe_events *events = &fe->events;
	struct dvb_frontend_event *e;
	int wp;

	dprintk ("%s\n", __FUNCTION__);

	if (down_interruptible (&events->sem))
		return;

	wp = (events->eventw + 1) % MAX_EVENT;

	if (wp == events->eventr) {
		events->overflow = 1;
		events->eventr = (events->eventr + 1) % MAX_EVENT;
	}

	e = &events->events[events->eventw];

	memcpy (&e->parameters, &fe->parameters, 
		sizeof (struct dvb_frontend_parameters));

	if (status & FE_HAS_LOCK)
		dvb_frontend_internal_ioctl (&fe->frontend,
					     FE_GET_FRONTEND,
					     &e->parameters);
	events->eventw = wp;

	up (&events->sem);

	e->status = status;
	dvb_call_frontend_notifiers (fe, status);

	wake_up_interruptible (&events->wait_queue);
}


static int dvb_frontend_get_event (struct dvb_frontend_data *fe,
			    struct dvb_frontend_event *event, int flags)
{
        struct dvb_fe_events *events = &fe->events;

	dprintk ("%s\n", __FUNCTION__);

	if (events->overflow) {
                events->overflow = 0;
                return -EOVERFLOW;
        }

        if (events->eventw == events->eventr) {
		int ret;

                if (flags & O_NONBLOCK)
                        return -EWOULDBLOCK;

		up(&fe->sem);

                ret = wait_event_interruptible (events->wait_queue,
                                                events->eventw != events->eventr);

        	if (down_interruptible (&fe->sem))
			return -ERESTARTSYS;

                if (ret < 0)
                        return ret;
        }

        if (down_interruptible (&events->sem))
		return -ERESTARTSYS;

       	memcpy (event, &events->events[events->eventr],
		sizeof(struct dvb_frontend_event));

        events->eventr = (events->eventr + 1) % MAX_EVENT;

       	up (&events->sem);

        return 0;
}


static int dvb_frontend_set_parameters (struct dvb_frontend_data *fe,
				 struct dvb_frontend_parameters *param,
				 int first_trial)
{
	struct dvb_frontend *frontend = &fe->frontend;
	int err;

	if (first_trial) {
		fe->timeout_count = 0;
		fe->lost_sync_count = 0;
		fe->lost_sync_jiffies = jiffies;
		fe->lnb_drift = 0;
		if (fe->status & ~FE_TIMEDOUT)
			dvb_frontend_add_event (fe, 0);
		memcpy (&fe->parameters, param,
			sizeof (struct dvb_frontend_parameters));
	}

	dvb_bend_frequency (fe, 0);

	dprintk ("%s: f == %i, drift == %i\n",
		 __FUNCTION__, (int) param->frequency, (int) fe->lnb_drift);

	param->frequency += fe->lnb_drift + fe->bending;
	err = dvb_frontend_internal_ioctl (frontend, FE_SET_FRONTEND, param);
	param->frequency -= fe->lnb_drift + fe->bending;

	wake_up_interruptible (&fe->wait_queue);

	return err;
}

static void dvb_frontend_init (struct dvb_frontend_data *fe)
{
	struct dvb_frontend *frontend = &fe->frontend;

	dprintk ("DVB: initialising frontend %i:%i (%s)...\n",
		 frontend->i2c->adapter->num, frontend->i2c->id,
		 fe->info->name);

	dvb_frontend_internal_ioctl (frontend, FE_INIT, NULL);
}


static void update_delay (int *quality, int *delay, int locked)
{
	int q2;

	dprintk ("%s\n", __FUNCTION__);

	if (locked)
		(*quality) = (*quality * 220 + 36*256) / 256;
	else
		(*quality) = (*quality * 220 + 0) / 256;

	q2 = *quality - 128;
	q2 *= q2;

	*delay = HZ/20 + q2 * HZ / (128*128);
}


#define LNB_DRIFT 1024  /*  max. tolerated LNB drift, XXX FIXME: adjust! */
#define TIMEOUT 2*HZ

/**
 *  here we only come when we have lost the lock bit, 
 *  let's try to do something useful...
 */
static void dvb_frontend_recover (struct dvb_frontend_data *fe)
{
	dprintk ("%s\n", __FUNCTION__);

#if 0
	if (fe->timeout_count > 3) {
		printk ("%s: frontend seems dead, reinitializing...\n",
			__FUNCTION__);
		dvb_call_frontend_notifiers (fe, 0);
		dvb_frontend_internal_ioctl (&fe->frontend, FE_INIT, NULL);
		dvb_frontend_set_parameters (fe, &fe->parameters, 1);
		dvb_frontend_add_event (fe, FE_REINIT);
		fe->lost_sync_jiffies = jiffies;
		fe->timeout_count = 0;
		return;
	}
#endif

	/**
	 *  let's start a zigzag scan to compensate LNB drift...
	 */
	{
		int j = fe->lost_sync_count;
		int stepsize;
		
		if (fe->info->type == FE_QPSK)
			stepsize = fe->parameters.u.qpsk.symbol_rate / 16000;
		else if (fe->info->type == FE_QAM)
			stepsize = 0;
		else
			stepsize = fe->info->frequency_stepsize * 2;

		if (j % 32 == 0) {
			fe->lnb_drift = 0;
		} else {
			fe->lnb_drift = -fe->lnb_drift;
			if (j % 2)
				fe->lnb_drift += stepsize;
		}

		dvb_frontend_set_parameters (fe, &fe->parameters, 0);
	}

	dvb_frontend_internal_ioctl (&fe->frontend, FE_RESET, NULL);
}



static int dvb_frontend_is_exiting (struct dvb_frontend_data *fe)
{
	if (fe->exit)
		return 1;

	if (fe->dvbdev->writers == 1)
		if (jiffies - fe->release_jiffies > dvb_shutdown_timeout * HZ)
			return 1;

	return 0;
}


static int dvb_frontend_thread (void *data)
{
	struct dvb_frontend_data *fe = (struct dvb_frontend_data *) data;
	char name [15];
	int quality = 0, delay = 3*HZ;
	fe_status_t s;

	dprintk ("%s\n", __FUNCTION__);

	snprintf (name, sizeof(name), "kdvb-fe-%i:%i",
		  fe->frontend.i2c->adapter->num, fe->frontend.i2c->id);

	dvb_kernel_thread_setup (name);

	fe->lost_sync_count = -1;

	dvb_call_frontend_notifiers (fe, 0);
	dvb_frontend_init (fe);

	while (!dvb_frontend_is_exiting (fe)) {
		up (&fe->sem);      /* is locked when we enter the thread... */

		interruptible_sleep_on_timeout (&fe->wait_queue, delay);
		if (signal_pending(current))
			break;

		if (down_interruptible (&fe->sem))
			break;

		if (fe->lost_sync_count == -1)
			continue;

		if (dvb_frontend_is_exiting (fe))
			break;

		dvb_frontend_internal_ioctl (&fe->frontend, FE_READ_STATUS, &s);

		update_delay (&quality, &delay, s & FE_HAS_LOCK);

		s &= ~FE_TIMEDOUT;

		if (s & FE_HAS_LOCK) {
			fe->timeout_count = 0;
			fe->lost_sync_count = 0;
		} else {
			fe->lost_sync_count++;
			if (!(fe->info->caps & FE_CAN_RECOVER)) {
				if (!(fe->info->caps & FE_CAN_CLEAN_SETUP)) {
					if (fe->lost_sync_count < 10)
						continue;
				}
				dvb_frontend_recover (fe);
				delay = HZ/5;
			}
			if (jiffies - fe->lost_sync_jiffies > TIMEOUT) {
				s |= FE_TIMEDOUT;
				if ((fe->status & FE_TIMEDOUT) == 0)
					fe->timeout_count++;
			}
		}

		if (s != fe->status)
			dvb_frontend_add_event (fe, s);
	};

	if (dvb_shutdown_timeout)
		dvb_frontend_internal_ioctl (&fe->frontend, FE_SLEEP, NULL); 

	up (&fe->sem);

	fe->thread_pid = 0;
	mb();

	wake_up_interruptible (&fe->wait_queue);
	return 0;
}


static void dvb_frontend_stop (struct dvb_frontend_data *fe)
{
	dprintk ("%s\n", __FUNCTION__);

		fe->exit = 1;
	mb();

	if (!fe->thread_pid)
		return;

	/* check if the thread is really alive */
	if (kill_proc(fe->thread_pid, 0, 1) == -ESRCH) {
		printk("dvb_frontend_stop: thread PID %d already died\n",
				fe->thread_pid);
		/* make sure the mutex was not held by the thread */
		init_MUTEX (&fe->sem);
		return;
	}

		wake_up_interruptible (&fe->wait_queue);
	interruptible_sleep_on(&fe->wait_queue);

	/* paranoia check */
	if (fe->thread_pid)
		printk("dvb_frontend_stop: warning: thread PID %d won't exit\n",
				fe->thread_pid);
}


static int dvb_frontend_start (struct dvb_frontend_data *fe)
{
	int ret;

	dprintk ("%s\n", __FUNCTION__);

	if (fe->thread_pid) {
		if (!fe->exit)
			return 0;
		else
		dvb_frontend_stop (fe);
	}

	if (signal_pending(current))
		return -EINTR;
	if (down_interruptible (&fe->sem))
		return -EINTR;

	fe->exit = 0;
	fe->thread_pid = 0;
	mb();

	ret = kernel_thread (dvb_frontend_thread, fe, 0);
	if (ret < 0) {
		printk("dvb_frontend_start: failed to start kernel_thread (%d)\n", ret);
		up(&fe->sem);
		return ret;
	}
	fe->thread_pid = ret;

	return 0;
}


static int dvb_frontend_ioctl (struct inode *inode, struct file *file,
			unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend_data *fe = dvbdev->priv;
	int err = 0;

	dprintk ("%s\n", __FUNCTION__);

	if (!fe || !fe->frontend.ioctl || fe->exit)
		return -ENODEV;

	if (down_interruptible (&fe->sem))
		return -ERESTARTSYS;

	switch (cmd) {
	case FE_DISEQC_SEND_MASTER_CMD:
	case FE_DISEQC_SEND_BURST:
	case FE_SET_TONE:
		if (fe->status)
			dvb_call_frontend_notifiers (fe, 0);
		dvb_frontend_internal_ioctl (&fe->frontend, cmd, parg);
		break;
	case FE_SET_FRONTEND:
		err = dvb_frontend_set_parameters (fe, parg, 1);
		break;
	case FE_GET_EVENT:
		err = dvb_frontend_get_event (fe, parg, file->f_flags);
		break;
	case FE_GET_FRONTEND:
		memcpy (parg, &fe->parameters,
			sizeof (struct dvb_frontend_parameters));
		/*  fall-through... */
	default:
		dvb_frontend_internal_ioctl (&fe->frontend, cmd, parg);
	};

	up (&fe->sem);

	return err;
}


static unsigned int dvb_frontend_poll (struct file *file, struct poll_table_struct *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend_data *fe = dvbdev->priv;

	dprintk ("%s\n", __FUNCTION__);

	poll_wait (file, &fe->events.wait_queue, wait);

	if (fe->events.eventw != fe->events.eventr)
		return (POLLIN | POLLRDNORM | POLLPRI);

	return 0;
}


static int dvb_frontend_open (struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend_data *fe = dvbdev->priv;
	int ret;

	dprintk ("%s\n", __FUNCTION__);

	if ((ret = dvb_generic_open (inode, file)) < 0)
		return ret;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		ret = dvb_frontend_start (fe);
		if (ret)
			dvb_generic_release (inode, file);

		/*  empty event queue */
		fe->events.eventr = fe->events.eventw = 0;
	}
	
	return ret;
}


static int dvb_frontend_release (struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend_data *fe = dvbdev->priv;

	dprintk ("%s\n", __FUNCTION__);

	if ((file->f_flags & O_ACCMODE) != O_RDONLY)
		fe->release_jiffies = jiffies;

	return dvb_generic_release (inode, file);
}



int
dvb_add_frontend_ioctls (struct dvb_adapter *adapter,
                         int (*before_ioctl) (struct dvb_frontend *frontend,
                                              unsigned int cmd, void *arg),
                         int (*after_ioctl)  (struct dvb_frontend *frontend,
                                              unsigned int cmd, void *arg),
			 void *before_after_data)
{
	struct dvb_frontend_ioctl_data *ioctl;
        struct list_head *entry;

	dprintk ("%s\n", __FUNCTION__);

	if (down_interruptible (&frontend_mutex))
		return -ERESTARTSYS;

	ioctl = kmalloc (sizeof(struct dvb_frontend_ioctl_data), GFP_KERNEL);

	if (!ioctl) {
		up (&frontend_mutex);
		return -ENOMEM;
	}

	ioctl->adapter = adapter;
	ioctl->before_ioctl = before_ioctl;
	ioctl->after_ioctl = after_ioctl;
	ioctl->before_after_data = before_after_data;

	list_add_tail (&ioctl->list_head, &frontend_ioctl_list);

	list_for_each (entry, &frontend_list) {
		struct dvb_frontend_data *fe;

		fe = list_entry (entry, struct dvb_frontend_data, list_head);

		if (fe->frontend.i2c->adapter == adapter &&
		    fe->frontend.before_ioctl == NULL &&
		    fe->frontend.after_ioctl == NULL)
		{
			fe->frontend.before_ioctl = before_ioctl;
			fe->frontend.after_ioctl = after_ioctl;
			fe->frontend.before_after_data = before_after_data;
		}
	}

	up (&frontend_mutex);

	return 0;
}


void
dvb_remove_frontend_ioctls (struct dvb_adapter *adapter,
			    int (*before_ioctl) (struct dvb_frontend *frontend,
                                                 unsigned int cmd, void *arg),
                            int (*after_ioctl)  (struct dvb_frontend *frontend,
                                                 unsigned int cmd, void *arg))
{
	struct list_head *entry, *n;

	dprintk ("%s\n", __FUNCTION__);

	down (&frontend_mutex);

	list_for_each (entry, &frontend_list) {
		struct dvb_frontend_data *fe;

		fe = list_entry (entry, struct dvb_frontend_data, list_head);

		if (fe->frontend.i2c->adapter == adapter &&
		    fe->frontend.before_ioctl == before_ioctl &&
		    fe->frontend.after_ioctl == after_ioctl)
		{
			fe->frontend.before_ioctl = NULL;
			fe->frontend.after_ioctl = NULL;

		}
	}

	list_for_each_safe (entry, n, &frontend_ioctl_list) {
		struct dvb_frontend_ioctl_data *ioctl;

		ioctl = list_entry (entry, struct dvb_frontend_ioctl_data, list_head);

		if (ioctl->adapter == adapter &&
		    ioctl->before_ioctl == before_ioctl &&
		    ioctl->after_ioctl == after_ioctl)
		{
			list_del (&ioctl->list_head);
			kfree (ioctl);
			
			break;
		}
	}

	up (&frontend_mutex);
}


int
dvb_add_frontend_notifier (struct dvb_adapter *adapter,
			   void (*callback) (fe_status_t s, void *data),
			   void *data)
{
	struct dvb_frontend_notifier_data *notifier;
	struct list_head *entry;

	dprintk ("%s\n", __FUNCTION__);

	if (down_interruptible (&frontend_mutex))
		return -ERESTARTSYS;

	notifier = kmalloc (sizeof(struct dvb_frontend_notifier_data), GFP_KERNEL);

	if (!notifier) {
		up (&frontend_mutex);
		return -ENOMEM;
	}

	notifier->adapter = adapter;
	notifier->callback = callback;
	notifier->data = data;

	list_add_tail (&notifier->list_head, &frontend_notifier_list);

	list_for_each (entry, &frontend_list) {
		struct dvb_frontend_data *fe;

		fe = list_entry (entry, struct dvb_frontend_data, list_head);

		if (fe->frontend.i2c->adapter == adapter &&
		    fe->frontend.notifier_callback == NULL)
		{
			fe->frontend.notifier_callback = callback;
			fe->frontend.notifier_data = data;
		}
	}

	up (&frontend_mutex);

	return 0;
}


void
dvb_remove_frontend_notifier (struct dvb_adapter *adapter,
			      void (*callback) (fe_status_t s, void *data))
{
	struct list_head *entry, *n;

	dprintk ("%s\n", __FUNCTION__);

	down (&frontend_mutex);

	list_for_each (entry, &frontend_list) {
		struct dvb_frontend_data *fe;

		fe = list_entry (entry, struct dvb_frontend_data, list_head);

		if (fe->frontend.i2c->adapter == adapter &&
		    fe->frontend.notifier_callback == callback)
		{
			fe->frontend.notifier_callback = NULL;

		}
	}

	list_for_each_safe (entry, n, &frontend_notifier_list) {
		struct dvb_frontend_notifier_data *notifier;

		notifier = list_entry (entry, struct dvb_frontend_notifier_data, list_head);

		if (notifier->adapter == adapter &&
		    notifier->callback == callback)
		{
			list_del (&notifier->list_head);
			kfree (notifier);
			
			break;
		}
	}

	up (&frontend_mutex);
}


static struct file_operations dvb_frontend_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= dvb_generic_ioctl,
	.poll		= dvb_frontend_poll,
	.open		= dvb_frontend_open,
	.release	= dvb_frontend_release
};



int
dvb_register_frontend (int (*ioctl) (struct dvb_frontend *frontend,
				     unsigned int cmd, void *arg),
		       struct dvb_i2c_bus *i2c,
		       void *data,
		       struct dvb_frontend_info *info)
{
	struct list_head *entry;
	struct dvb_frontend_data *fe;
	static const struct dvb_device dvbdev_template = {
		.users = ~0,
		.writers = 1,
		.readers = (~0)-1,
		.fops = &dvb_frontend_fops,
		.kernel_ioctl = dvb_frontend_ioctl
	};

	dprintk ("%s\n", __FUNCTION__);

	if (down_interruptible (&frontend_mutex))
		return -ERESTARTSYS;

	if (!(fe = kmalloc (sizeof (struct dvb_frontend_data), GFP_KERNEL))) {
		up (&frontend_mutex);
		return -ENOMEM;
	}

	memset (fe, 0, sizeof (struct dvb_frontend_data));

	init_MUTEX (&fe->sem);
	init_waitqueue_head (&fe->wait_queue);
	init_waitqueue_head (&fe->events.wait_queue);
	init_MUTEX (&fe->events.sem);
	fe->events.eventw = fe->events.eventr = 0;
	fe->events.overflow = 0;

	fe->frontend.ioctl = ioctl;
	fe->frontend.i2c = i2c;
	fe->frontend.data = data;
	fe->info = info;

	list_for_each (entry, &frontend_ioctl_list) {
		struct dvb_frontend_ioctl_data *ioctl;

		ioctl = list_entry (entry,
				    struct dvb_frontend_ioctl_data,
				    list_head);

		if (ioctl->adapter == i2c->adapter) {
			fe->frontend.before_ioctl = ioctl->before_ioctl;
			fe->frontend.after_ioctl = ioctl->after_ioctl;
			fe->frontend.before_after_data = ioctl->before_after_data;
			break;
		}
	}

	list_for_each (entry, &frontend_notifier_list) {
		struct dvb_frontend_notifier_data *notifier;

		notifier = list_entry (entry,
				       struct dvb_frontend_notifier_data,
				       list_head);

		if (notifier->adapter == i2c->adapter) {
			fe->frontend.notifier_callback = notifier->callback;
			fe->frontend.notifier_data = notifier->data;
			break;
		}
	}

	list_add_tail (&fe->list_head, &frontend_list);

	printk ("DVB: registering frontend %i:%i (%s)...\n",
		fe->frontend.i2c->adapter->num, fe->frontend.i2c->id,
		fe->info->name);

	dvb_register_device (i2c->adapter, &fe->dvbdev, &dvbdev_template,
			     fe, DVB_DEVICE_FRONTEND);

	up (&frontend_mutex);

	return 0;
}


int dvb_unregister_frontend (int (*ioctl) (struct dvb_frontend *frontend,
					   unsigned int cmd, void *arg),
			     struct dvb_i2c_bus *i2c)
{
        struct list_head *entry, *n;

	dprintk ("%s\n", __FUNCTION__);

	down (&frontend_mutex);

	list_for_each_safe (entry, n, &frontend_list) {
		struct dvb_frontend_data *fe;

		fe = list_entry (entry, struct dvb_frontend_data, list_head);

		if (fe->frontend.ioctl == ioctl && fe->frontend.i2c == i2c) {
			dvb_unregister_device (fe->dvbdev);
			list_del (entry);
			up (&frontend_mutex);
			dvb_frontend_stop (fe);
			kfree (fe);
			return 0;
		}
	}

	up (&frontend_mutex);
	return -EINVAL;
}

MODULE_PARM(dvb_frontend_debug,"i");
MODULE_PARM(dvb_shutdown_timeout,"i");
MODULE_PARM_DESC(dvb_frontend_debug, "enable verbose debug messages");
MODULE_PARM_DESC(dvb_shutdown_timeout, "wait <shutdown_timeout> seconds after close() before suspending hardware");

