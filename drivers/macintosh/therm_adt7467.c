/*
 * Device driver for the i2c thermostat found on the iBook G4
 *
 * Copyright (C) 2003 Colin Leroy, Benjamin Herrenschmidt
 *
 * Documentation from
 * http://www.analog.com/UploadedFiles/Data_Sheets/115254175ADT7467_pra.pdf
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <asm/of_device.h>

#undef DEBUG

#define TEMP_LOCAL   0x26
#define TEMP_REMOTE1 0x25
#define TEMP_REMOTE2 0x27
#define LIM_LOCAL    0x6a
#define LIM_REMOTE1  0x6b
#define LIM_REMOTE2  0x6c
#define FAN0_SPEED   0x28

#define MANUAL_MODE  0x5c
#define MANUAL_MASK  0xe0
#define AUTO_MASK    0x20
#define FAN_SPD_SET  0x30

static int limit_decrease = 0;
static int fan_speed = -1;

MODULE_AUTHOR("Colin Leroy <colin@colino.net>");
MODULE_DESCRIPTION("Driver for ADT7467 thermostat in iBook G4");
MODULE_LICENSE("GPL");

MODULE_PARM(limit_decrease,"i");
MODULE_PARM_DESC(limit_decrease,"Decrease maximum temperatures (50°C cpu, 70°C gpu) by N °C.");
MODULE_PARM(fan_speed,"i");
MODULE_PARM_DESC(fan_speed,"Specify fan speed (0-255) when lim < temp < lim+8 (dangerous !), default is automatic");

struct thermostat {
	struct i2c_client	clt;
	u8			cached_temp[3];
	u8			initial_limits[3];
	u8			limits[3];
	int			last_speed;
	int			overriding;
};

static int therm_bus, therm_address;
static struct of_device * of_dev;
static struct thermostat* thermostat;
static pid_t monitor_thread_id;
static int monitor_running;
static struct completion monitor_task_compl;

static int attach_one_thermostat(struct i2c_adapter *adapter, int addr, int busno);
static void write_fan_speed(struct thermostat *th, int speed);

static int
write_reg(struct thermostat* th, int reg, u8 data)
{
	u8 tmp[2];
	int rc;
	
	tmp[0] = reg;
	tmp[1] = data;
	rc = i2c_master_send(&th->clt, (const char *)tmp, 2);
	if (rc < 0)
		return rc;
	if (rc != 2)
		return -ENODEV;
	return 0;
}

static int
read_reg(struct thermostat* th, int reg)
{
	u8 reg_addr, data;
	int rc;

	reg_addr = (u8)reg;
	rc = i2c_master_send(&th->clt, &reg_addr, 1);
	if (rc < 0)
		return rc;
	if (rc != 1)
		return -ENODEV;
	rc = i2c_master_recv(&th->clt, (char *)&data, 1);
	if (rc < 0)
		return rc;
	return data;
}

static int
attach_thermostat(struct i2c_adapter *adapter)
{
	unsigned long bus_no;

	if (strncmp(adapter->name, "uni-n", 5))
		return -ENODEV;
	bus_no = simple_strtoul(adapter->name + 6, NULL, 10);
	if (bus_no != therm_bus)
		return -ENODEV;
	return attach_one_thermostat(adapter, therm_address, bus_no);
}

static int
detach_thermostat(struct i2c_adapter *adapter)
{
	struct thermostat* th;
	
	if (thermostat == NULL)
		return 0;

	th = thermostat;

	if (monitor_running) {
		monitor_running = 0;
		wait_for_completion(&monitor_task_compl);
	}
		
	printk(KERN_INFO "adt7467: Putting max temperatures back from %d, %d, %d,"
		" to %d, %d, %d, (°C)\n", 
		th->limits[0], th->limits[1], th->limits[2],
		th->initial_limits[0], th->initial_limits[1], th->initial_limits[2]);
	write_reg(th, LIM_LOCAL,   th->initial_limits[0]);
	write_reg(th, LIM_REMOTE1, th->initial_limits[1]);
	write_reg(th, LIM_REMOTE2, th->initial_limits[2]);
	write_fan_speed(th, -1);

	i2c_detach_client(&th->clt);

	thermostat = NULL;

	kfree(th);

	return 0;
}

static struct i2c_driver thermostat_driver = {  
	.name		="Apple Thermostat ADT7467",
	.id		=0xDEAD7467,
	.flags		=I2C_DF_NOTIFY,
	.attach_adapter	=&attach_thermostat,
	.detach_adapter	=&detach_thermostat,
};

static int read_fan_speed(struct thermostat *th, u8 addr)
{
	u8 tmp[2];
	u16 res;
	
	/* should start with low byte */
	tmp[1] = read_reg(th, addr);
	tmp[0] = read_reg(th, addr + 1);
	
	res = tmp[1] + (tmp[0] << 8);
	return (90000*60)/res;
}

static void write_fan_speed(struct thermostat *th, int speed)
{
	u8 manual;
	
	if (speed > 0xff) 
		speed = 0xff;
	else if (speed < -1) 
		speed = 0;
	
	if (speed >= 0) {
		manual = read_reg(th, MANUAL_MODE);
		write_reg(th, MANUAL_MODE, manual|MANUAL_MASK);
		if (th->last_speed != speed)
			printk(KERN_INFO "adt7467: Setting speed to: %d\n", speed);
		th->last_speed = speed;
		write_reg(th, FAN_SPD_SET, speed);
	} else {
		/* back to automatic */
		manual = read_reg(th, MANUAL_MODE);
		if (th->last_speed != -1)
			printk(KERN_INFO "adt7467: Setting speed to: automatic\n");
		th->last_speed = -1;
		write_reg(th, MANUAL_MODE, manual&(~AUTO_MASK));
	}
}

static int monitor_task(void *arg)
{
	struct thermostat* th = arg;
	u8 temps[3];
	u8 lims[3];
#ifdef DEBUG
	int mfan_speed;
#endif
	
	lock_kernel();
	daemonize("kfand");
	unlock_kernel();
	strcpy(current->comm, "thermostat");
	monitor_running = 1;

	while(monitor_running)
	{
		set_task_state(current, TASK_UNINTERRUPTIBLE);
		schedule_timeout(2*HZ);

		/* Check status */
		/* local   : chip */
		/* remote 1: CPU ?*/
		/* remote 2: GPU ?*/
#ifndef DEBUG
		if (fan_speed != -1) {
#endif
			temps[0]  = read_reg(th, TEMP_LOCAL);
			temps[1]  = read_reg(th, TEMP_REMOTE1);
			temps[2]  = read_reg(th, TEMP_REMOTE2);
			lims[0]   = th->limits[0];
			lims[1]   = th->limits[1];
			lims[2]   = th->limits[2];
#ifndef DEBUG
		}
#endif		
		if (fan_speed != -1) {
			if (temps[0] > lims[0]
			||  temps[1] > lims[1]
			||  temps[2] > lims[2]) {
				int var = 0;
				var = (temps[0] - lims[0] > var) ? temps[0] - lims[0] : var;
				var = (temps[1] - lims[1] > var) ? temps[1] - lims[1] : var;
				var = (temps[2] - lims[2] > var) ? temps[2] - lims[2] : var;
				if (var > 8) {
					if (th->overriding == 0)
						printk(KERN_INFO "adt7467: Limit exceeded by %d°C, overriding specified fan speed.\n",
							var);
					th->overriding = 1;
					write_fan_speed(th, 255);
				} else if (!th->overriding || var < 6) {
					if (th->overriding == 1)
						printk(KERN_INFO "adt7467: Limit exceeded by %d°C, setting speed to specified.\n",
							var);					
					th->overriding = 0;
					write_fan_speed(th, fan_speed);
				}
			} else {
				int var = 10;
				var = (lims[0] - temps[0] < var) ? lims[0] - temps[0] : var;
				var = (lims[1] - temps[1] < var) ? lims[1] - temps[1] : var;
				var = (lims[2] - temps[2] < var) ? lims[2] - temps[2] : var;
				if (var >= 2) /* pseudo hysteresis */
					write_fan_speed(th, 0);
			}
		}
#ifdef DEBUG
		mfan_speed = read_fan_speed(th, FAN0_SPEED);
		/* only one fan in the iBook G4 */
				
		if (temps[0] != th->cached_temp[0]
		||  temps[1] != th->cached_temp[1]
		||  temps[2] != th->cached_temp[2]) {
			printk(KERN_INFO "adt7467: Temperature infos:"
					 " thermostats: %d,%d,%d °C;"
					 " limits: %d,%d,%d °C;"
					 " fan speed: %d RPM\n",
				temps[0], temps[1], temps[2],
				lims[0],  lims[1],  lims[2],
				mfan_speed);
		}
		th->cached_temp[0] = temps[0];
		th->cached_temp[1] = temps[1];
		th->cached_temp[2] = temps[2];
#endif		
	}

	complete_and_exit(&monitor_task_compl, 0);
	return 0;
}

static int
attach_one_thermostat(struct i2c_adapter *adapter, int addr, int busno)
{
	struct thermostat* th;
	int rc;

	if (thermostat)
		return 0;
	th = (struct thermostat *)kmalloc(sizeof(struct thermostat), GFP_KERNEL);
	if (!th)
		return -ENOMEM;
	memset(th, 0, sizeof(*th));
	th->clt.addr = addr;
	th->clt.adapter = adapter;
	th->clt.driver = &thermostat_driver;
	th->clt.id = 0xDEAD7467;
	strcpy(th->clt.name, "thermostat");

	rc = read_reg(th, 0);
	if (rc < 0) {
		printk(KERN_ERR "adt7467: Thermostat failed to read config from bus %d !\n",
			busno);
		kfree(th);
		return -ENODEV;
	}
	printk(KERN_INFO "adt7467: ADT7467 initializing\n");

	th->initial_limits[0] = read_reg(th, LIM_LOCAL);
	th->initial_limits[1] = read_reg(th, LIM_REMOTE1);
	th->initial_limits[2] = read_reg(th, LIM_REMOTE2);
	th->limits[0] = 70 - limit_decrease;	/* Local */
	th->limits[1] = 50 - limit_decrease;	/* CPU */
	th->limits[2] = 70 - limit_decrease;	/* GPU */
	
	printk(KERN_INFO "adt7467: Lowering max temperatures from %d, %d, %d"
		" to %d, %d, %d (°C)\n", 
		th->initial_limits[0], th->initial_limits[1], th->initial_limits[2], 
		th->limits[0], th->limits[1], th->limits[2]);
	write_reg(th, LIM_LOCAL,   th->limits[0]);
	write_reg(th, LIM_REMOTE1, th->limits[1]);
	write_reg(th, LIM_REMOTE2, th->limits[2]);

	thermostat = th;

	if (i2c_attach_client(&th->clt)) {
		printk("adt7467: Thermostat failed to attach client !\n");
		thermostat = NULL;
		kfree(th);
		return -ENODEV;
	}

	if (fan_speed != -1) {
		write_fan_speed(th, 0);
	} else {
		write_fan_speed(th, -1);
	}
	
	init_completion(&monitor_task_compl);
	
	monitor_thread_id = kernel_thread(monitor_task, th,
		SIGCHLD | CLONE_KERNEL);

	return 0;
}

/* 
 * Now, unfortunately, sysfs doesn't give us a nice void * we could
 * pass around to the attribute functions, so we don't really have
 * choice but implement a bunch of them...
 *
 */
#define BUILD_SHOW_FUNC_DEG(name, data)				\
static ssize_t show_##name(struct device *dev, char *buf)	\
{								\
	return sprintf(buf, "%d°C", data);			\
}
#define BUILD_SHOW_FUNC_INT(name, data)				\
static ssize_t show_##name(struct device *dev, char *buf)	\
{								\
	return sprintf(buf, "%d", data);			\
}

BUILD_SHOW_FUNC_DEG(cpu_temperature, (read_reg(thermostat, TEMP_REMOTE1)))
BUILD_SHOW_FUNC_DEG(gpu_temperature, (read_reg(thermostat, TEMP_REMOTE2)))
BUILD_SHOW_FUNC_DEG(cpu_limit, thermostat->limits[1])
BUILD_SHOW_FUNC_DEG(gpu_limit, thermostat->limits[2])
BUILD_SHOW_FUNC_INT(fan_speed, (read_fan_speed(thermostat, FAN0_SPEED)))
		

static DEVICE_ATTR(cpu_temperature,S_IRUGO,show_cpu_temperature,NULL);
static DEVICE_ATTR(gpu_temperature,S_IRUGO,show_gpu_temperature,NULL);
static DEVICE_ATTR(cpu_limit,S_IRUGO,show_cpu_limit,NULL);
static DEVICE_ATTR(gpu_limit,S_IRUGO,show_gpu_limit,NULL);
static DEVICE_ATTR(fan_speed,S_IRUGO,show_fan_speed,NULL);

static int __init
thermostat_init(void)
{
	struct device_node* np;
	u32 *prop;
	
	/* Currently, we only deal with the iBook G4, we will support
	 * all "2003" powerbooks later on
	 */
	np = of_find_node_by_name(NULL, "fan");
	if (!np)
		return -ENODEV;
	if (!device_is_compatible(np, "adt7467"))
		return -ENODEV;

	prop = (u32 *)get_property(np, "reg", NULL);
	if (!prop)
		return -ENODEV;
	therm_bus = ((*prop) >> 8) & 0x0f;
	therm_address = ((*prop) & 0xff) >> 1;

	printk(KERN_INFO "adt7467: Thermostat bus: %d, address: 0x%02x, limit_decrease: %d, fan_speed: %d\n",
		therm_bus, therm_address, limit_decrease, fan_speed);

	of_dev = of_platform_device_create(np, "temperatures");
	
	if (of_dev == NULL) {
		printk(KERN_ERR "Can't register temperatures device !\n");
		return -ENODEV;
	}
	
	device_create_file(&of_dev->dev, &dev_attr_cpu_temperature);
	device_create_file(&of_dev->dev, &dev_attr_gpu_temperature);
	device_create_file(&of_dev->dev, &dev_attr_cpu_limit);
	device_create_file(&of_dev->dev, &dev_attr_gpu_limit);
	device_create_file(&of_dev->dev, &dev_attr_fan_speed);
	
#ifndef CONFIG_I2C_KEYWEST
	request_module("i2c-keywest");
#endif

	return i2c_add_driver(&thermostat_driver);
}

static void __exit
thermostat_exit(void)
{
	if (of_dev) {
		device_remove_file(&of_dev->dev, &dev_attr_cpu_temperature);
		device_remove_file(&of_dev->dev, &dev_attr_gpu_temperature);
		device_remove_file(&of_dev->dev, &dev_attr_cpu_limit);
		device_remove_file(&of_dev->dev, &dev_attr_gpu_limit);
		device_remove_file(&of_dev->dev, &dev_attr_fan_speed);
		of_device_unregister(of_dev);
	}
	i2c_del_driver(&thermostat_driver);
}

module_init(thermostat_init);
module_exit(thermostat_exit);
