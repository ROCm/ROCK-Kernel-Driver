#ifndef __MATROXFB_MAVEN_H__
#define __MATROXFB_MAVEN_H__

#include <linux/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include "matroxfb_base.h"

struct matroxfb_dh_maven_info;

struct i2c_bit_adapter {
	struct i2c_adapter		adapter;
	int				initialized;
	struct i2c_algo_bit_data	bac;
	struct matroxfb_dh_maven_info  *minfo;
};

struct matroxfb_dh_maven_info {
	struct matrox_fb_info*	primary_dev;

	struct i2c_bit_adapter	maven;
	struct i2c_bit_adapter	ddc1;
	struct i2c_bit_adapter	ddc2;
};

#endif /* __MATROXFB_MAVEN_H__ */
