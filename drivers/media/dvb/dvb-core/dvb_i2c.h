/*
 * dvb_i2c.h: i2c interface to get rid of i2c-core.c
 *
 * Copyright (C) 2002 Holger Waechtler for convergence integrated media GmbH
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _DVB_I2C_H_
#define _DVB_I2C_H_

#include <linux/list.h>
#include <linux/i2c.h>

#include "dvbdev.h"


struct dvb_i2c_bus {
	struct list_head list_head;
	int (*xfer) (struct dvb_i2c_bus *i2c, 
		     const struct i2c_msg msgs[],
		     int num);
	void *data;
	struct dvb_adapter *adapter;
	int id;
	struct list_head client_list;
};


extern struct dvb_i2c_bus*
dvb_register_i2c_bus (int (*xfer) (struct dvb_i2c_bus *i2c,
				   const struct i2c_msg *msgs, int num),
		      void *data,
		      struct dvb_adapter *adapter,
		      int id);

extern
void dvb_unregister_i2c_bus (int (*xfer) (struct dvb_i2c_bus *i2c,
					  const struct i2c_msg msgs[], int num),
			     struct dvb_adapter *adapter,
			     int id);


extern int dvb_register_i2c_device (struct module *owner,
				    int (*attach) (struct dvb_i2c_bus *i2c, void **data),
				    void (*detach) (struct dvb_i2c_bus *i2c, void *data));

extern int dvb_unregister_i2c_device (int (*attach) (struct dvb_i2c_bus *i2c, void **data));

#endif

