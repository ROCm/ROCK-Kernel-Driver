/*
 * dvb-core.c: DVB core driver
 *
 * Copyright (C) 1999-2001 Ralph  Metzler
 *                         Marcus Metzler
 *                         Holger Waechtler 
 *                                    for convergence integrated media GmbH
 *
 * Copyright (C) 2004 Andrew de Quincey (tuning thread cleanup)
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

#define FESTATE_IDLE 1
#define FESTATE_RETUNE 2
#define FESTATE_TUNING_FAST 4
#define FESTATE_TUNING_SLOW 8
#define FESTATE_TUNED 16
#define FESTATE_ZIGZAG_FAST 32
#define FESTATE_ZIGZAG_SLOW 64
#define FESTATE_DISEQC 128
#define FESTATE_WAITFORLOCK (FESTATE_TUNING_FAST | FESTATE_TUNING_SLOW | FESTATE_ZIGZAG_FAST | FESTATE_ZIGZAG_SLOW | FESTATE_DISEQC)
#define FESTATE_SEARCHING_FAST (FESTATE_TUNING_FAST | FESTATE_ZIGZAG_FAST)
#define FESTATE_SEARCHING_SLOW (FESTATE_TUNING_SLOW | FESTATE_ZIGZAG_SLOW)
#define FESTATE_LOSTLOCK (FESTATE_ZIGZAG_FAST | FESTATE_ZIGZAG_SLOW)
/*
 * FESTATE_IDLE. No tuning parameters have been supplied and the loop is idling.
 * FESTATE_RETUNE. Parameters have been supplied, but we have not yet performed the first tune.
 * FESTATE_TUNING_FAST. Tuning parameters have been supplied and fast zigzag scan is in progress.
 * FESTATE_TUNING_SLOW. Tuning parameters have been supplied. Fast zigzag failed, so we're trying again, but slower.
 * FESTATE_TUNED. The frontend has successfully locked on.
 * FESTATE_ZIGZAG_FAST. The lock has been lost, and a fast zigzag has been initiated to try and regain it.
 * FESTATE_ZIGZAG_SLOW. The lock has been lost. Fast zigzag has been failed, so we're trying again, but slower.
 * FESTATE_DISEQC. A DISEQC command has just been issued.
 * FESTATE_WAITFORLOCK. When we're waiting for a lock.
 * FESTATE_SEARCHING_FAST. When we're searching for a signal using a fast zigzag scan.
 * FESTATE_SEARCHING_SLOW. When we're searching for a signal using a slow zigzag scan.
 * FESTATE_LOSTLOCK. When the lock has been lost, and we're searching it again.
 */


static int dvb_frontend_debug = 0;
static int dvb_shutdown_timeout = 5;
static int dvb_override_frequency_bending = 0;
static int dvb_force_auto_inversion = 0;
static int dvb_override_tune_delay = 0;

static int do_frequency_bending = 0;

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
	int state;
	int bending;
	int lnb_drift;
	int inversion;
	int auto_step;
	int auto_sub_step;
	int started_auto_step;
	int min_delay;
	int max_drift;
	int step_size;
	int exit;
	int wakeup;
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

		if (this_fe != fe && (fe->state != FESTATE_IDLE) &&
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

static void dvb_frontend_init (struct dvb_frontend_data *fe)
{
	struct dvb_frontend *frontend = &fe->frontend;

	dprintk ("DVB: initialising frontend %i:%i (%s)...\n",
		 frontend->i2c->adapter->num, frontend->i2c->id,
		 fe->info->name);

	dvb_frontend_internal_ioctl (frontend, FE_INIT, NULL);
}

static void update_delay (int *quality, int *delay, int min_delay, int locked)
{
	int q2;

	dprintk ("%s\n", __FUNCTION__);

	if (locked)
		(*quality) = (*quality * 220 + 36*256) / 256;
	else
		(*quality) = (*quality * 220 + 0) / 256;

	q2 = *quality - 128;
	q2 *= q2;

	    *delay = min_delay + q2 * HZ / (128*128);
}

/**
 * Performs automatic twiddling of frontend parameters.
 * 
 * @param fe The frontend concerned.
 * @param check_wrapped Checks if an iteration has completed. DO NOT SET ON THE FIRST ATTEMPT
 * @returns Number of complete iterations that have been performed.
 */
static int dvb_frontend_autotune(struct dvb_frontend_data *fe, int check_wrapped)
{
	int autoinversion;
	int ready = 0;
	int original_inversion = fe->parameters.inversion;
	u32 original_frequency = fe->parameters.frequency;

	// are we using autoinversion?
	autoinversion = ((!(fe->info->caps & FE_CAN_INVERSION_AUTO)) && (fe->parameters.inversion == INVERSION_AUTO));

	// setup parameters correctly
	while(!ready) {
		// calculate the lnb_drift
		fe->lnb_drift = fe->auto_step * fe->step_size;

		// wrap the auto_step if we've exceeded the maximum drift
		if (fe->lnb_drift > fe->max_drift) {
			fe->auto_step = 0;
			fe->auto_sub_step = 0;
			fe->lnb_drift = 0;
		}

		// perform inversion and +/- zigzag
		switch(fe->auto_sub_step) {
		case 0:
			// try with the current inversion and current drift setting
			ready = 1;
			break;

		case 1:
			if (!autoinversion) break;

			fe->inversion = (fe->inversion == INVERSION_OFF) ? INVERSION_ON : INVERSION_OFF;
			ready = 1;
			break;

		case 2:
			if (fe->lnb_drift == 0) break;
		    
			fe->lnb_drift = -fe->lnb_drift;
			ready = 1;
			break;
	    
		case 3:
			if (fe->lnb_drift == 0) break;
			if (!autoinversion) break;
		    
			fe->inversion = (fe->inversion == INVERSION_OFF) ? INVERSION_ON : INVERSION_OFF;
			fe->lnb_drift = -fe->lnb_drift;
			ready = 1;
			break;
		    
		default:
			fe->auto_step++;
			fe->auto_sub_step = -1; // it'll be incremented to 0 in a moment
			break;
		}
	    
		if (!ready) fe->auto_sub_step++;
	}

	// if this attempt would hit where we started, indicate a complete iteration has occurred
	if ((fe->auto_step == fe->started_auto_step) && (fe->auto_sub_step == 0) && check_wrapped) {
		return 1;
		}

	// perform frequency bending if necessary
	if ((dvb_override_frequency_bending != 1) && do_frequency_bending)
		dvb_bend_frequency(fe, 0);

	// instrumentation
	dprintk("%s: drift:%i bending:%i inversion:%i auto_step:%i auto_sub_step:%i started_auto_step:%i\n", 
		__FUNCTION__, fe->lnb_drift, fe->bending, fe->inversion, fe->auto_step, fe->auto_sub_step,
		fe->started_auto_step);
    
	// set the frontend itself
	fe->parameters.frequency += fe->lnb_drift + fe->bending;
	if (autoinversion) fe->parameters.inversion = fe->inversion;
	dvb_frontend_internal_ioctl (&fe->frontend, FE_SET_FRONTEND, &fe->parameters);
	fe->parameters.frequency = original_frequency;
	fe->parameters.inversion = original_inversion;

	// normal return
	fe->auto_sub_step++;
	return 0;
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

static int dvb_frontend_should_wakeup (struct dvb_frontend_data *fe)
{
	if (fe->wakeup) {
		fe->wakeup = 0;
		return 1;
	}
	return dvb_frontend_is_exiting(fe);
}

static void dvb_frontend_wakeup (struct dvb_frontend_data *fe) {
	fe->wakeup = 1;
	wake_up_interruptible(&fe->wait_queue);
}

static int dvb_frontend_thread (void *data)
{
	struct dvb_frontend_data *fe = (struct dvb_frontend_data *) data;
	unsigned long timeout;
	char name [15];
	int quality = 0, delay = 3*HZ;
	fe_status_t s;
	int check_wrapped = 0;

	dprintk ("%s\n", __FUNCTION__);

	snprintf (name, sizeof(name), "kdvb-fe-%i:%i",
		  fe->frontend.i2c->adapter->num, fe->frontend.i2c->id);

	dvb_kernel_thread_setup (name);

	dvb_call_frontend_notifiers (fe, 0);
	dvb_frontend_init (fe);
	fe->wakeup = 0;

	while (1) {
		up (&fe->sem);      /* is locked when we enter the thread... */

		timeout = wait_event_interruptible_timeout(fe->wait_queue,0 != dvb_frontend_should_wakeup (fe), delay);
		if (-ERESTARTSYS == timeout || 0 != dvb_frontend_is_exiting (fe)) {
			/* got signal or quitting */
			break;
		}

		if (down_interruptible (&fe->sem))
			break;

		// if we've got no parameters, just keep idling
		if (fe->state & FESTATE_IDLE) {
			delay = 3*HZ;
			quality = 0;
			continue;
		}

		// get the frontend status
		dvb_frontend_internal_ioctl (&fe->frontend, FE_READ_STATUS, &s);
		if (s != fe->status)
			dvb_frontend_add_event (fe, s);

		// if we're not tuned, and we have a lock, move to the TUNED state
		if ((fe->state & FESTATE_WAITFORLOCK) && (s & FE_HAS_LOCK)) {
			update_delay(&quality, &delay, fe->min_delay, s & FE_HAS_LOCK);
			fe->state = FESTATE_TUNED;

			// if we're tuned, then we have determined the correct inversion
			if ((!(fe->info->caps & FE_CAN_INVERSION_AUTO)) && (fe->parameters.inversion == INVERSION_AUTO)) {
				fe->parameters.inversion = fe->inversion;
			}
			continue;
		}

		// if we are tuned already, check we're still locked
		if (fe->state & FESTATE_TUNED) {
			update_delay(&quality, &delay, fe->min_delay, s & FE_HAS_LOCK);

			// we're tuned, and the lock is still good...
		if (s & FE_HAS_LOCK) {
				continue;
		} else {
				// if we _WERE_ tuned, but now don't have a lock, need to zigzag
				fe->state = FESTATE_ZIGZAG_FAST;
				fe->started_auto_step = fe->auto_step;
				check_wrapped = 0;
				// fallthrough
			}
		}

		// don't actually do anything if we're in the LOSTLOCK state, the frontend is set to
		// FE_CAN_RECOVER, and the max_drift is 0
		if ((fe->state & FESTATE_LOSTLOCK) && 
		    (fe->info->caps & FE_CAN_RECOVER) && (fe->max_drift == 0)) {
			update_delay(&quality, &delay, fe->min_delay, s & FE_HAS_LOCK);
						continue;
				}
	    
		// don't do anything if we're in the DISEQC state, since this might be someone
		// with a motorized dish controlled by DISEQC. If its actually a re-tune, there will
		// be a SET_FRONTEND soon enough.
		if (fe->state & FESTATE_DISEQC) {
			update_delay(&quality, &delay, fe->min_delay, s & FE_HAS_LOCK);
			continue;
				}

		// if we're in the RETUNE state, set everything up for a brand new scan,
		// keeping the current inversion setting, as the next tune is _very_ likely
		// to require the same
		if (fe->state & FESTATE_RETUNE) {
			fe->lnb_drift = 0;
			fe->auto_step = 0;
			fe->auto_sub_step = 0;
			fe->started_auto_step = 0;
			check_wrapped = 0;
		}

		// fast zigzag.
		if ((fe->state & FESTATE_SEARCHING_FAST) || (fe->state & FESTATE_RETUNE)) {
			delay = fe->min_delay;

			// peform a tune
			if (dvb_frontend_autotune(fe, check_wrapped)) {
				// OK, if we've run out of trials at the fast speed. Drop back to
				// slow for the _next_ attempt
				fe->state = FESTATE_SEARCHING_SLOW;
				fe->started_auto_step = fe->auto_step;
				continue;
			}
			check_wrapped = 1;

			// if we've just retuned, enter the ZIGZAG_FAST state. This ensures
			// we cannot return from an FE_SET_FRONTEND ioctl before the first frontend
			// tune occurs
			if (fe->state & FESTATE_RETUNE) {
				fe->state = FESTATE_TUNING_FAST;
				wake_up_interruptible(&fe->wait_queue);
			}
		}

		// slow zigzag
		if (fe->state & FESTATE_SEARCHING_SLOW) {
			update_delay(&quality, &delay, fe->min_delay, s & FE_HAS_LOCK);
		    
			// Note: don't bother checking for wrapping; we stay in this state 
			// until we get a lock
			dvb_frontend_autotune(fe, 0);
		}
	};

	if (dvb_shutdown_timeout)
		dvb_frontend_internal_ioctl (&fe->frontend, FE_SLEEP, NULL); 

	up (&fe->sem);

	fe->thread_pid = 0;
	mb();

	dvb_frontend_wakeup(fe);
	return 0;
}


static void dvb_frontend_stop (struct dvb_frontend_data *fe)
{
	unsigned long ret;

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

	/* wake up the frontend thread, so it notices that fe->exit == 1 */
	dvb_frontend_wakeup(fe);

	/* wait until the frontend thread has exited */
	ret = wait_event_interruptible(fe->wait_queue,0 == fe->thread_pid);
	if (-ERESTARTSYS != ret) {
		fe->state = FESTATE_IDLE;
		return;
	}
	fe->state = FESTATE_IDLE;

	/* paranoia check in case a signal arrived */
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

	fe->state = FESTATE_IDLE;
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
	struct dvb_frontend_tune_settings fetunesettings;
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
		fe->state = FESTATE_DISEQC;
		break;

	case FE_SET_FRONTEND:
		fe->state = FESTATE_RETUNE;
	    
		memcpy (&fe->parameters, parg,
			sizeof (struct dvb_frontend_parameters));

		memset(&fetunesettings, 0, sizeof(struct dvb_frontend_tune_settings));
		memcpy(&fetunesettings.parameters, parg,
		       sizeof (struct dvb_frontend_parameters));
		    
		// force auto frequency inversion if requested
		if (dvb_force_auto_inversion) {
			fe->parameters.inversion = INVERSION_AUTO;
			fetunesettings.parameters.inversion = INVERSION_AUTO;
		}

		// get frontend-specific tuning settings
		if (dvb_frontend_internal_ioctl(&fe->frontend, FE_GET_TUNE_SETTINGS, &fetunesettings) == 0) {
			fe->min_delay = (fetunesettings.min_delay_ms * HZ) / 1000;
			fe->max_drift = fetunesettings.max_drift;
			fe->step_size = fetunesettings.step_size;
		} else {
			// default values
			switch(fe->info->type) {
			case FE_QPSK:
				fe->min_delay = HZ/20; // default mindelay of 50ms
				fe->step_size = fe->parameters.u.qpsk.symbol_rate / 16000;
				fe->max_drift = fe->parameters.u.qpsk.symbol_rate / 2000;
		break;
			    
			case FE_QAM:
				fe->min_delay = HZ/20; // default mindelay of 50ms
				fe->step_size = 0;
				fe->max_drift = 0; // don't want any zigzagging under DVB-C frontends
				break;
			    
			case FE_OFDM:
				fe->min_delay = HZ/20; // default mindelay of 50ms
				fe->step_size = fe->info->frequency_stepsize * 2;
				fe->max_drift = (fe->info->frequency_stepsize * 2) + 1;
				break;
			}
		}
		if (dvb_override_tune_delay > 0) {
		       fe->min_delay = (dvb_override_tune_delay * HZ) / 1000;
		}

		dvb_frontend_add_event (fe, 0);	    
		break;

	case FE_GET_EVENT:
		err = dvb_frontend_get_event (fe, parg, file->f_flags);
		break;
	case FE_GET_FRONTEND:
		memcpy (parg, &fe->parameters,
			sizeof (struct dvb_frontend_parameters));
		/*  fall-through... */
	default:
		err = dvb_frontend_internal_ioctl (&fe->frontend, cmd, parg);
	};

	up (&fe->sem);
	if (err < 0)
		return err;

	// Force the CAN_INVERSION_AUTO bit on. If the frontend doesn't do it, it is done for it.
	if ((cmd == FE_GET_INFO) && (err == 0)) {
		struct dvb_frontend_info* tmp = (struct dvb_frontend_info*) parg;
		tmp->caps |= FE_CAN_INVERSION_AUTO;
	}

	// if the frontend has just been set, wait until the first tune has finished.
	// This ensures the app doesn't start reading data too quickly, perhaps from the
	// previous lock, which is REALLY CONFUSING TO DEBUG!
	if ((cmd == FE_SET_FRONTEND) && (err == 0)) {
		dvb_frontend_wakeup(fe);
		err = wait_event_interruptible(fe->wait_queue, fe->state & ~FESTATE_RETUNE);
	}

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
	fe->inversion = INVERSION_OFF;

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

	if ((info->caps & FE_NEEDS_BENDING) || (dvb_override_frequency_bending == 2))
		do_frequency_bending = 1;
    
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
MODULE_PARM(dvb_override_frequency_bending,"i");
MODULE_PARM(dvb_force_auto_inversion,"i");
MODULE_PARM(dvb_override_tune_delay,"i");

MODULE_PARM_DESC(dvb_frontend_debug, "enable verbose debug messages");
MODULE_PARM_DESC(dvb_shutdown_timeout, "wait <shutdown_timeout> seconds after close() before suspending hardware");
MODULE_PARM_DESC(dvb_override_frequency_bending, "0: normal (default), 1: never use frequency bending, 2: always use frequency bending");
MODULE_PARM_DESC(dvb_force_auto_inversion, "0: normal (default), 1: INVERSION_AUTO forced always");
MODULE_PARM_DESC(dvb_override_tune_delay, "0: normal (default), >0 => delay in milliseconds to wait for lock after a tune attempt");
