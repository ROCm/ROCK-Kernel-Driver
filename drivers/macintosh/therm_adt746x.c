/*
 * Device driver for the i2c thermostat found on the iBook G4, Albook G4
 *
 * Copyright (C) 2003, 2004 Colin Leroy, Rasmus Rohde, Benjamin Herrenschmidt
 *
 * Documentation from
 * http://www.analog.com/UploadedFiles/Data_Sheets/115254175ADT7467_pra.pdf
 * http://www.analog.com/UploadedFiles/Data_Sheets/3686221171167ADT7460_b.pdf
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

#define CONFIG_REG   0x40
#define MANUAL_MASK  0xe0
#define AUTO_MASK    0x20

static u8 TEMP_REG[3]    = {0x26, 0x25, 0x27}; /* local, cpu, gpu */
static u8 LIMIT_REG[3]   = {0x6b, 0x6a, 0x6c}; /* local, cpu, gpu */
static u8 MANUAL_MODE[2] = {0x5c, 0x5d};       
static u8 REM_CONTROL[2] = {0x00, 0x40};
static u8 FAN_SPEED[2]   = {0x28, 0x2a};
static u8 FAN_SPD_SET[2] = {0x30, 0x31};

static u8 default_limits_local[3] = {70, 50, 70};    /* local, cpu, gpu */
static u8 default_limits_chip[3] = {80, 65, 80};    /* local, cpu, gpu */

static int limit_adjust = 0;
static int fan_speed = -1;

MODULE_AUTHOR("Colin Leroy <colin@colino.net>");
MODULE_DESCRIPTION("Driver for ADT746x thermostat in iBook G4 and Powerbook G4 Alu");
MODULE_LICENSE("GPL");

MODULE_PARM(limit_adjust,"i");
MODULE_PARM_DESC(limit_adjust,"Adjust maximum temperatures (50°C cpu, 70°C gpu) by N °C.");
MODULE_PARM(fan_speed,"i");
MODULE_PARM_DESC(fan_speed,"Specify fan speed (0-255) when lim < temp < lim+8 (default 128)");

struct thermostat {
	struct i2c_client	clt;
	u8			cached_temp[3];
	u8			initial_limits[3];
	u8			limits[3];
	int			last_speed[2];
	int			overriding[2];
};

static enum {ADT7460, ADT7467} therm_type;
static int therm_bus, therm_address;
static struct of_device * of_dev;
static struct thermostat* thermostat;
static pid_t monitor_thread_id;
static int monitor_running;
static struct completion monitor_task_compl;

static int attach_one_thermostat(struct i2c_adapter *adapter, int addr, int busno);
static void write_both_fan_speed(struct thermostat *th, int speed);
static void write_fan_speed(struct thermostat *th, int speed, int fan);

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
	int i;
	
	if (thermostat == NULL)
		return 0;

	th = thermostat;

	if (monitor_running) {
		monitor_running = 0;
		wait_for_completion(&monitor_task_compl);
	}
		
	printk(KERN_INFO "adt746x: Putting max temperatures back from %d, %d, %d,"
		" to %d, %d, %d, (°C)\n", 
		th->limits[0], th->limits[1], th->limits[2],
		th->initial_limits[0], th->initial_limits[1], th->initial_limits[2]);
	
	for (i = 0; i < 3; i++)
		write_reg(th, LIMIT_REG[i], th->initial_limits[i]);

	write_both_fan_speed(th, -1);

	i2c_detach_client(&th->clt);

	thermostat = NULL;

	kfree(th);

	return 0;
}

static struct i2c_driver thermostat_driver = {  
	.name		="Apple Thermostat ADT746x",
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

static void write_both_fan_speed(struct thermostat *th, int speed)
{
	write_fan_speed(th, speed, 0);
	if (therm_type == ADT7460)
		write_fan_speed(th, speed, 1);
}

static void write_fan_speed(struct thermostat *th, int speed, int fan)
{
	u8 manual;
	
	if (speed > 0xff) 
		speed = 0xff;
	else if (speed < -1) 
		speed = 0;
	
	if (therm_type == ADT7467 && fan == 1)
		return;
	
	if (th->last_speed[fan] != speed) {
		if (speed == -1)
			printk(KERN_INFO "adt746x: Setting speed to: automatic for %s fan.\n",
				fan?"GPU":"CPU");
		else
			printk(KERN_INFO "adt746x: Setting speed to: %d for %s fan.\n",
				speed, fan?"GPU":"CPU");
	} else
		return;
	
	if (speed >= 0) {
		manual = read_reg(th, MANUAL_MODE[fan]);
		write_reg(th, MANUAL_MODE[fan], manual|MANUAL_MASK);
		write_reg(th, FAN_SPD_SET[fan], speed);
	} else {
		/* back to automatic */
		if(therm_type == ADT7460) {
			manual = read_reg(th, MANUAL_MODE[fan]) & (~MANUAL_MASK);
			write_reg(th, MANUAL_MODE[fan], manual|REM_CONTROL[fan]);
		} else {
			manual = read_reg(th, MANUAL_MODE[fan]);
			write_reg(th, MANUAL_MODE[fan], manual&(~AUTO_MASK));
		}
	}
	
	th->last_speed[fan] = speed;			
}

static int monitor_task(void *arg)
{
	struct thermostat* th = arg;
	u8 temps[3];
	u8 lims[3];
	int i;
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
		msleep(2000);

		/* Check status */
		/* local   : chip */
		/* remote 1: CPU ?*/
		/* remote 2: GPU ?*/
#ifndef DEBUG
		if (fan_speed != -1) {
#endif
			for (i = 0; i < 3; i++) {
				temps[i]  = read_reg(th, TEMP_REG[i]);
				lims[i]   = th->limits[i];
			}
#ifndef DEBUG
		}
#endif		
		if (fan_speed != -1) {
			int lastvar = 0;		/* for iBook */
			for (i = 1; i < 3; i++) {	/* we don't care about local sensor */
				int started = 0;
				int fan_number = (therm_type == ADT7460 && i == 2);
				int var = temps[i] - lims[i];
				if (var > 8) {
					if (th->overriding[fan_number] == 0)
						printk(KERN_INFO "adt746x: Limit exceeded by %d°C, overriding specified fan speed for %s.\n",
							var, fan_number?"GPU":"CPU");
					th->overriding[fan_number] = 1;
					write_fan_speed(th, 255, fan_number);
					started = 1;
				} else if ((!th->overriding[fan_number] || var < 6) && var > 0) {
					if (th->overriding[fan_number] == 1)
						printk(KERN_INFO "adt746x: Limit exceeded by %d°C, setting speed to specified for %s.\n",
							var, fan_number?"GPU":"CPU");					
					th->overriding[fan_number] = 0;
					write_fan_speed(th, fan_speed, fan_number);
					started = 1;
				} else if (var < -1) {
					/* don't stop iBook fan if GPU is cold and CPU is not
					 * so cold (lastvar >= -1) */
					if (therm_type == ADT7460 || lastvar < -1 || i == 1) {
						if (th->last_speed[fan_number] != 0)
							printk(KERN_INFO "adt746x: Stopping %s fan.\n",
								fan_number?"GPU":"CPU");
						write_fan_speed(th, 0, fan_number);
					}
				}
				
				lastvar = var;
				
				if (started && therm_type == ADT7467)
					break; /* we don't want to re-stop the fan
						* if CPU is heating and GPU is not */
			}
		}
#ifdef DEBUG
		mfan_speed = read_fan_speed(th, FAN_SPEED[0]);
		/* only one fan in the iBook G4 */
				
		if (temps[0] != th->cached_temp[0]
		||  temps[1] != th->cached_temp[1]
		||  temps[2] != th->cached_temp[2]) {
			printk(KERN_INFO "adt746x: Temperature infos:"
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

static void
set_limit(struct thermostat *th, int i)
{
		/* Set CPU limit higher to avoid powerdowns */ 
		th->limits[i] = default_limits_chip[i] + limit_adjust;
		write_reg(th, LIMIT_REG[i], th->limits[i]);
		
		/* set our limits to normal */
		th->limits[i] = default_limits_local[i] + limit_adjust;
}
	
static int
attach_one_thermostat(struct i2c_adapter *adapter, int addr, int busno)
{
	struct thermostat* th;
	int rc;
	int i;

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
		printk(KERN_ERR "adt746x: Thermostat failed to read config from bus %d !\n",
			busno);
		kfree(th);
		return -ENODEV;
	}
	/* force manual control to start the fan quieter */
	
	if (fan_speed == -1)
		fan_speed=128;
	
	if(therm_type == ADT7460) {
		printk(KERN_INFO "adt746x: ADT7460 initializing\n");
		/* The 7460 needs to be started explicitly */
		write_reg(th, CONFIG_REG, 1);
	} else
		printk(KERN_INFO "adt746x: ADT7467 initializing\n");

	for (i = 0; i < 3; i++) {
		th->initial_limits[i] = read_reg(th, LIMIT_REG[i]);
		set_limit(th, i);
	}
	
	printk(KERN_INFO "adt746x: Lowering max temperatures from %d, %d, %d"
		" to %d, %d, %d (°C)\n", 
		th->initial_limits[0], th->initial_limits[1], th->initial_limits[2], 
		th->limits[0], th->limits[1], th->limits[2]);

	thermostat = th;

	if (i2c_attach_client(&th->clt)) {
		printk("adt746x: Thermostat failed to attach client !\n");
		thermostat = NULL;
		kfree(th);
		return -ENODEV;
	}

	/* be sure to really write fan speed the first time */
	th->last_speed[0] = -2;
	th->last_speed[1] = -2;
	
	if (fan_speed != -1) {
		write_both_fan_speed(th, 0);
	} else {
		write_both_fan_speed(th, -1);
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
	return sprintf(buf, "%d°C\n", data);			\
}
#define BUILD_SHOW_FUNC_INT(name, data)				\
static ssize_t show_##name(struct device *dev, char *buf)	\
{								\
	return sprintf(buf, "%d\n", data);			\
}

#define BUILD_STORE_FUNC_DEG(name, data)			\
static ssize_t store_##name(struct device *dev, const char *buf, size_t n) \
{								\
	int val;						\
	int i;							\
	val = simple_strtol(buf, NULL, 10);			\
	printk(KERN_INFO "Adjusting limits by %d°C\n", val);	\
	limit_adjust = val;					\
	for (i=0; i < 3; i++)					\
		set_limit(thermostat, i);			\
	return n;						\
}

#define BUILD_STORE_FUNC_INT(name, data)			\
static ssize_t store_##name(struct device *dev, const char *buf, size_t n) \
{								\
	u32 val;						\
	val = simple_strtoul(buf, NULL, 10);			\
	if (val < 0 || val > 255)				\
		return -EINVAL;					\
	printk(KERN_INFO "Setting fan speed to %d\n", val);	\
	data = val;						\
	return n;						\
}

BUILD_SHOW_FUNC_DEG(cpu_temperature,	 (read_reg(thermostat, TEMP_REG[1])))
BUILD_SHOW_FUNC_DEG(gpu_temperature,	 (read_reg(thermostat, TEMP_REG[2])))
BUILD_SHOW_FUNC_DEG(cpu_limit,		 thermostat->limits[1])
BUILD_SHOW_FUNC_DEG(gpu_limit,		 thermostat->limits[2])

BUILD_SHOW_FUNC_INT(specified_fan_speed, fan_speed)
BUILD_SHOW_FUNC_INT(cpu_fan_speed,	 (read_fan_speed(thermostat, FAN_SPEED[0])))
BUILD_SHOW_FUNC_INT(gpu_fan_speed,	 (read_fan_speed(thermostat, FAN_SPEED[1])))

BUILD_STORE_FUNC_INT(specified_fan_speed,fan_speed)
BUILD_SHOW_FUNC_INT(limit_adjust,	 limit_adjust)
BUILD_STORE_FUNC_DEG(limit_adjust,	 thermostat)
		
static DEVICE_ATTR(cpu_temperature,	S_IRUGO,
		   show_cpu_temperature,NULL);
static DEVICE_ATTR(gpu_temperature,	S_IRUGO,
		   show_gpu_temperature,NULL);
static DEVICE_ATTR(cpu_limit,		S_IRUGO,
		   show_cpu_limit,	NULL);
static DEVICE_ATTR(gpu_limit,		S_IRUGO,
		   show_gpu_limit,	NULL);

static DEVICE_ATTR(specified_fan_speed,	S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH,
		   show_specified_fan_speed,store_specified_fan_speed);

static DEVICE_ATTR(cpu_fan_speed,	S_IRUGO,
		   show_cpu_fan_speed,	NULL);
static DEVICE_ATTR(gpu_fan_speed,	S_IRUGO,
		   show_gpu_fan_speed,	NULL);

static DEVICE_ATTR(limit_adjust,	S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH,
		   show_limit_adjust,	store_limit_adjust);


static int __init
thermostat_init(void)
{
	struct device_node* np;
	u32 *prop;
	
	np = of_find_node_by_name(NULL, "fan");
	if (!np)
		return -ENODEV;
	if (device_is_compatible(np, "adt7460"))
		therm_type = ADT7460;
	else if (device_is_compatible(np, "adt7467"))
		therm_type = ADT7467;
	else
		return -ENODEV;

	prop = (u32 *)get_property(np, "reg", NULL);
	if (!prop)
		return -ENODEV;
	therm_bus = ((*prop) >> 8) & 0x0f;
	therm_address = ((*prop) & 0xff) >> 1;

	printk(KERN_INFO "adt746x: Thermostat bus: %d, address: 0x%02x, limit_adjust: %d, fan_speed: %d\n",
		therm_bus, therm_address, limit_adjust, fan_speed);

	of_dev = of_platform_device_create(np, "temperatures");
	
	if (of_dev == NULL) {
		printk(KERN_ERR "Can't register temperatures device !\n");
		return -ENODEV;
	}
	
	device_create_file(&of_dev->dev, &dev_attr_cpu_temperature);
	device_create_file(&of_dev->dev, &dev_attr_gpu_temperature);
	device_create_file(&of_dev->dev, &dev_attr_cpu_limit);
	device_create_file(&of_dev->dev, &dev_attr_gpu_limit);
	device_create_file(&of_dev->dev, &dev_attr_limit_adjust);
	device_create_file(&of_dev->dev, &dev_attr_specified_fan_speed);
	device_create_file(&of_dev->dev, &dev_attr_cpu_fan_speed);
	if(therm_type == ADT7460)
		device_create_file(&of_dev->dev, &dev_attr_gpu_fan_speed);

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
		device_remove_file(&of_dev->dev, &dev_attr_limit_adjust);
		device_remove_file(&of_dev->dev, &dev_attr_specified_fan_speed);
		device_remove_file(&of_dev->dev, &dev_attr_cpu_fan_speed);
		if(therm_type == ADT7460)
			device_remove_file(&of_dev->dev, &dev_attr_gpu_fan_speed);
		of_device_unregister(of_dev);
	}
	i2c_del_driver(&thermostat_driver);
}

module_init(thermostat_init);
module_exit(thermostat_exit);
