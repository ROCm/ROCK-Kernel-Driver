/* 
 * dvb_frontend.h
 *
 * Copyright (C) 2001 convergence integrated media GmbH
 * Copyright (C) 2004 convergence GmbH
 *
 * Written by Ralph Metzler
 * Overhauled by Holger Waechtler
 * Kernel I2C stuff by Michael Hunold <hunold@convergence.de>
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
#include <linux/delay.h>

#include <linux/dvb/frontend.h>

#include "dvbdev.h"

/* FIXME: Move to i2c-id.h */
#define I2C_DRIVERID_DVBFE_ALPS_TDLB7	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_ALPS_TDMB7	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_AT76C651	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_CX24110	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_CX22702	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_DIB3000MB	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_DST		I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_DUMMY	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_L64781	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_MT312	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_MT352	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_NXT6000	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_SP887X	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_STV0299	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_TDA1004X	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_TDA8083	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_VES1820	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_VES1X93	I2C_DRIVERID_EXP2

/**
 *   when before_ioctl is registered and returns value 0, ioctl and after_ioctl
 *   are not executed.
 */

struct dvb_frontend {
	int (*before_ioctl) (struct dvb_frontend *frontend, unsigned int cmd, void *arg);
	int (*ioctl) (struct dvb_frontend *frontend, unsigned int cmd, void *arg);
	int (*after_ioctl) (struct dvb_frontend *frontend, unsigned int cmd, void *arg);
	void (*notifier_callback) (fe_status_t s, void *data);
	struct dvb_adapter *dvb_adapter;
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
#define FE_SLEEP              _IO  ('v', 80)
#define FE_INIT               _IO  ('v', 81)
#define FE_GET_TUNE_SETTINGS  _IOWR('v', 83, struct dvb_frontend_tune_settings)
#define FE_REGISTER	      _IO  ('v', 84)
#define FE_UNREGISTER	      _IO  ('v', 85)

extern int
dvb_register_frontend (int (*ioctl) (struct dvb_frontend *frontend,
				     unsigned int cmd, void *arg),
		       struct dvb_adapter *dvb_adapter,
		       void *data,
		       struct dvb_frontend_info *info,
		       struct module *module);

extern int
dvb_unregister_frontend (int (*ioctl) (struct dvb_frontend *frontend,
				       unsigned int cmd, void *arg),
			 struct dvb_adapter *dvb_adapter);


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

