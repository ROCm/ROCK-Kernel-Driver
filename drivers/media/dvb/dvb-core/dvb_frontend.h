/* 
 * dvb_frontend.h
 *
 * Copyright (C) 2001 Ralph Metzler for convergence integrated media GmbH
 *                    overhauled by Holger Waechtler for Convergence GmbH
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *

 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVB_FRONTEND_H_
#define _DVB_FRONTEND_H_

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/errno.h>

#include <linux/dvb/frontend.h>

#include "dvb_i2c.h"
#include "dvbdev.h"




/**
 *   when before_ioctl is registered and returns value 0, ioctl and after_ioctl
 *   are not executed.
 */

struct dvb_frontend {
	int (*before_ioctl) (struct dvb_frontend *frontend, unsigned int cmd, void *arg);
	int (*ioctl) (struct dvb_frontend *frontend, unsigned int cmd, void *arg);
	int (*after_ioctl) (struct dvb_frontend *frontend, unsigned int cmd, void *arg);
	void (*notifier_callback) (fe_status_t s, void *data);
	struct dvb_i2c_bus *i2c;
	void *before_after_data;   /*  can be used by hardware module... */
	void *notifier_data;       /*  can be used by hardware module... */
	void *data;                /*  can be used by hardware module... */
};

struct dvb_frontend_tune_settings {
        int min_delay_ms;
        int step_size;
        int max_drift;
        struct dvb_frontend_parameters parameters;
};


/**
 *   private frontend command ioctl's.
 *   keep them in sync with the public ones defined in linux/dvb/frontend.h
 * 
 *   FE_SLEEP. Ioctl used to put frontend into a low power mode.
 *   FE_INIT. Ioctl used to initialise the frontend.
 *   FE_GET_TUNE_SETTINGS. Get the frontend-specific tuning loop settings for the supplied set of parameters.
 */
#define FE_SLEEP              _IO('v', 80)
#define FE_INIT               _IO('v', 81)
#define FE_GET_TUNE_SETTINGS  _IOWR('v', 83, struct dvb_frontend_tune_settings)


extern int
dvb_register_frontend (int (*ioctl) (struct dvb_frontend *frontend,
				     unsigned int cmd, void *arg),
		       struct dvb_i2c_bus *i2c,
		       void *data,
		       struct dvb_frontend_info *info);

extern int
dvb_unregister_frontend (int (*ioctl) (struct dvb_frontend *frontend,
				       unsigned int cmd, void *arg),
			 struct dvb_i2c_bus *i2c);


/**
 *  Add special ioctl code performed before and after the main ioctl
 *  to all frontend devices on the specified DVB adapter.
 *  This is necessairy because the 22kHz/13V-18V/DiSEqC stuff depends
 *  heavily on the hardware around the frontend, the same tuner can create 
 *  these signals on about a million different ways...
 *
 *  Return value: number of frontends where the ioctl's were applied.
 */
extern int
dvb_add_frontend_ioctls (struct dvb_adapter *adapter,
			 int (*before_ioctl) (struct dvb_frontend *frontend,
					      unsigned int cmd, void *arg),
			 int (*after_ioctl)  (struct dvb_frontend *frontend,
					      unsigned int cmd, void *arg),
			 void *before_after_data);


extern void
dvb_remove_frontend_ioctls (struct dvb_adapter *adapter,
			    int (*before_ioctl) (struct dvb_frontend *frontend,
					         unsigned int cmd, void *arg),
			    int (*after_ioctl)  (struct dvb_frontend *frontend,
					         unsigned int cmd, void *arg));

extern int
dvb_add_frontend_notifier (struct dvb_adapter *adapter,
			   void (*callback) (fe_status_t s, void *data),
			   void *data);
extern void
dvb_remove_frontend_notifier (struct dvb_adapter *adapter,
			      void (*callback) (fe_status_t s, void *data));

#endif

