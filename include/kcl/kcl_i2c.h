/* SPDX-License-Identifier: MIT */
#ifndef _KCL_KCL_I2C_H
#define _KCL_KCL_I2C_H

#include <linux/i2c.h>

#ifdef HAVE_I2C_NEW_CLIENT_DEVICE
extern struct i2c_client *
i2c_new_client_device(struct i2c_adapter *adap, struct i2c_board_info const *info);
#else
static inline struct i2c_client *
i2c_new_client_device(struct i2c_adapter *adap, struct i2c_board_info const *info)
{
	return i2c_new_device(adap, info);
}
#endif

#endif
