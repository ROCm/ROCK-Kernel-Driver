/* 
 *   Creation Date: <2003/03/14 20:54:13 samuel>
 *   Time-stamp: <2003/03/15 18:55:53 samuel>
 *   
 *	<therm_windtunnel.c>
 *	
 *	The G4 "windtunnel" has a single fan controlled by a
 *	DS1775 fan controller and an ADM1030 thermostat.
 *
 *	The fan controller is equipped with a temperature sensor
 *	which measures the case temperature. The ADM censor
 *	measures the CPU temperature. This driver tunes the
 *	behavior of the fan. It is based upon empirical observations
 *	of the 'AppleFan' driver under OSX.
 *
 *	WARNING: This driver has only been testen on Apple's
 *	1.25 MHz Dual G4 (March 03). Other machines might have
 *	a different thermal design. It is tuned for a CPU
 *	temperatur around 57 C.
 *
 *   Copyright (C) 2003 Samuel Rydh (samuel@ibrium.se)
 *
 *   Loosely based upon 'thermostat.c' written by Benjamin Herrenschmidt
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
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
#include <linux/workqueue.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sections.h>

MODULE_AUTHOR("Samuel Rydh <samuel@ibrium.se>");
MODULE_DESCRIPTION("Apple G4 (windtunnel) fan driver");
MODULE_LICENSE("GPL");

#define LOG_TEMP		0			/* continously log temperature */

/* scan 0x48-0x4f (DS1775) and 0x2c-2x2f (ADM1030) */
static unsigned short normal_i2c[] = { 0x49, 0x2c, I2C_CLIENT_END };
static unsigned short normal_i2c_range[] = { 0x48, 0x4f, 0x2c, 0x2f, I2C_CLIENT_END };
static struct work_struct poll_work;

I2C_CLIENT_INSMOD;

#define I2C_DRIVERID_G4FAN	0x9001			/* fixme */

#define THERMOSTAT_CLIENT_ID	1
#define FAN_CLIENT_ID		2

struct temp_range {
	u8			high;			/* start the fan */
	u8			low;			/* stop the fan */
};
struct apple_thermal_info {
	u8			id;			/* implementation ID */
	u8			fan_count;		/* number of fans */
	u8			thermostat_count;	/* number of thermostats */
	u8			unused[5];
	struct temp_range	ranges[4];		/* temperature ranges (may be [])*/
};

static int do_detect( struct i2c_adapter *adapter, int addr, int kind);

static struct {
	struct i2c_client	*thermostat;
	struct i2c_client	*fan;
	int			error;
	struct timer_list	timer;

	int			overheat_temp;		/* 100% fan at this temp */
	int			overheat_hyst;
	int			temp;
	int			casetemp;
	int			fan_level;		/* active fan_table setting */

	int			downind;
	int			upind;

	int			r0, r1, r20, r23, r25;	/* saved register */
} x;

static struct {
	int			temp;
	int			fan_setting;
} fan_up_table[] = {
	{ 0x0000, 11 },		/* min fan */
	{ 0x3900, 8 },		/* 57.0 C */
	{ 0x3a4a, 7 },		/* 58.3 C */
	{ 0x3ad3, 6 },		/* 58.8 C */
	{ 0x3b3c, 5 },		/* 59.2 C */
	{ 0x3b94, 4 },		/* 59.6 C */
	{ 0x3be3, 3 },		/* 58.9 C */
	{ 0x3c29, 2 },		/* 59.2 C */
	{ 0xffff, 1 }		/* on fire */
};
static struct {
	int			temp;
	int			fan_setting;
} fan_down_table[] = {
	{ 0x3700, 11 },		/* 55.0 C */
	{ 0x374a, 6 },
	{ 0x3800, 7 },		/* 56.0 C */
	{ 0x3900, 8 },		/* 57.0 C */
	{ 0x3a4a, 7 },		/* 58.3 C */
	{ 0x3ad3, 6 },		/* 58.8 C */
	{ 0x3b3c, 5 },		/* 59.2 C */
	{ 0x3b94, 4 },		/* 58.9 C */
	{ 0x3be3, 3 },		/* 58.9 C */
	{ 0x3c29, 2 },		/* 59.2 C */
	{ 0xffff, 1 }
};

static int
write_reg( struct i2c_client *cl, int reg, int data, int len )
{
	u8 tmp[3];

	if( len < 1 || len > 2 || data < 0 )
		return -EINVAL;

	tmp[0] = reg;
	tmp[1] = (len == 1) ? data : (data >> 8);
	tmp[2] = data;
	len++;
	
	if( i2c_master_send(cl, tmp, len) != len )
		return -ENODEV;
	return 0;
}

static int
read_reg( struct i2c_client *cl, int reg, int len )
{
	u8 buf[2];

	if( len != 1 && len != 2 )
		return -EINVAL;
	buf[0] = reg;
	if( i2c_master_send(cl, buf, 1) != 1 )
		return -ENODEV;
	if( i2c_master_recv(cl, buf, len) != len )
		return -ENODEV;
	return (len == 2)? ((unsigned int)buf[0] << 8) | buf[1] : buf[0];
}


static void
print_temp( const char *s, int temp )
{
	printk("%s%d.%d C", s ? s : "", temp>>8, (temp & 255)*10/256 );
}

static void
tune_fan( int fan_setting )
{
	int val = (fan_setting << 3) | 7;
	x.fan_level = fan_setting;
	
	//write_reg( x.fan, 0x24, val, 1 );
	write_reg( x.fan, 0x25, val, 1 );
	write_reg( x.fan, 0x20, 0, 1 );
	print_temp("CPU-temp: ", x.temp );
	if( x.casetemp )
		print_temp(", Case: ", x.casetemp );
	printk("  Tuning fan: %d (%02x)\n", fan_setting, val );
}

static void
poll_temp( void *param )
{
	int temp = read_reg( x.thermostat, 0, 2 );
	int i, level, casetemp;

	/* this actually occurs when the computer is loaded */
	if( temp < 0 )
		goto out;

	casetemp = read_reg(x.fan, 0x0b, 1) << 8;
	casetemp |= (read_reg(x.fan, 0x06, 1) & 0x7) << 5;

	if( LOG_TEMP && x.temp != temp ) {
		print_temp("CPU-temp: ", temp );
		print_temp(", Case: ", casetemp );
		printk(",  Fan: %d\n", x.fan_level );
	}
	x.temp = temp;
	x.casetemp = casetemp;

	level = -1;
	for( i=0; (temp & 0xffff) > fan_down_table[i].temp ; i++ )
		;
	if( i < x.downind )
		level = fan_down_table[i].fan_setting;
	x.downind = i;

	for( i=0; (temp & 0xfffe) >= fan_up_table[i+1].temp ; i++ )
		;
	if( x.upind < i )
		level = fan_up_table[i].fan_setting;
	x.upind = i;

	if( level >= 0 )
		tune_fan( level );
 out:
	x.timer.expires = jiffies + 8*HZ;
	add_timer( &x.timer );
}

static void
schedule_poll( unsigned long t )
{
	schedule_work(&poll_work);
}

/************************************************************************/
/*	i2c probing and setup						*/
/************************************************************************/

static int
do_attach( struct i2c_adapter *adapter )
{
	return i2c_probe( adapter, &addr_data, &do_detect );
}

static int
do_detach( struct i2c_client *client )
{
	int err;

	printk("do_detach: id %d\n", client->id );
	if( (err=i2c_detach_client(client)) ) {
		printk("failed to detach thermostat client\n");
		return err;
	}
	kfree( client );
	return 0;
}

static struct i2c_driver g4fan_driver = {  
	.name		= "Apple G4 Thermostat/Fan",
	.id		= I2C_DRIVERID_G4FAN,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter = &do_attach,
	.detach_client	= &do_detach,
	.command	= NULL,
};

static int
detect_fan( struct i2c_client *cl )
{
	/* check that this is an ADM1030 */
	if( read_reg(cl, 0x3d, 1) != 0x30 || read_reg(cl, 0x3e, 1) != 0x41 )
		goto out;
	printk("ADM1030 fan controller detected at %02x\n", cl->addr );

	if( x.fan ) {
		x.error |= 2;
		goto out;
	}
	x.fan = cl;
	cl->id = FAN_CLIENT_ID;
	strncpy( cl->name, "ADM1030 fan controller", sizeof(cl->name) );

	if( i2c_attach_client( cl ) )
		goto out;
	return 0;
 out:
	if( cl != x.fan )
		kfree( cl );
	return 0;
}

static int
detect_thermostat( struct i2c_client *cl ) 
{
	int hyst_temp, os_temp, temp;

	if( (temp=read_reg(cl, 0, 2)) < 0 )
		goto out;
	
	/* temperature sanity check */
	if( temp < 0x1600 || temp > 0x3c00 )
		goto out;
	hyst_temp = read_reg(cl, 2, 2);
	os_temp = read_reg(cl, 3, 2);
	if( hyst_temp < 0 || os_temp < 0 )
		goto out;

	printk("DS1775 digital thermometer detected at %02x\n", cl->addr );
	print_temp("Temp: ", temp );
	print_temp("  Hyst: ", hyst_temp );
	print_temp("  OS: ", os_temp );
	printk("\n");

	if( x.thermostat ) {
		x.error |= 1;
		goto out;
	}
	x.temp = temp;
	x.thermostat = cl;
	x.overheat_temp = os_temp;
	x.overheat_hyst = hyst_temp;
	
	cl->id = THERMOSTAT_CLIENT_ID;
	strncpy( cl->name, "DS1775 thermostat", sizeof(cl->name) );

	if( i2c_attach_client( cl ) )
		goto out;
	return 0;
out:
	kfree( cl );
	return 0;
}

static int
do_detect( struct i2c_adapter *adapter, int addr, int kind )
{
	struct i2c_client *cl;

	if( strncmp(adapter->name, "uni-n", 5) )
		return 0;
	if( !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA
				     | I2C_FUNC_SMBUS_WRITE_BYTE) )
		return 0;

	if( !(cl=kmalloc( sizeof(struct i2c_client), GFP_KERNEL )) )
		return -ENOMEM;
	memset( cl, 0, sizeof(struct i2c_client) );

	cl->addr = addr;
	cl->adapter = adapter;
	cl->driver = &g4fan_driver;
	cl->flags = 0;

	if( addr < 0x48 )
		return detect_fan( cl );
	return detect_thermostat( cl );
}

#define PRINT_REG( r )	printk("reg %02x = %02x\n", r, read_reg(x.fan, r, 1) )

static int __init
g4fan_init( void )
{
	struct apple_thermal_info *info;
	struct device_node *np;
	int ret, val;
	
	np = of_find_node_by_name(NULL, "power-mgt");
	if (np == NULL)
		return -ENODEV;
	info = (struct apple_thermal_info*)get_property(np, "thermal-info", NULL);
	of_node_put(np);
	if (info == NULL)
		return -ENODEV;
	
	/* check for G4 "Windtunnel" SMP */
	if( machine_is_compatible("PowerMac3,6") ) {
		if( info->id != 3 ) {
			printk(KERN_ERR "g4fan: design id %d unknown\n", info->id);
			return -ENODEV;
		}
	} else {
		printk(KERN_ERR "g4fan: unsupported machine type\n");
		return -ENODEV;
	}
	if( (ret=i2c_add_driver(&g4fan_driver)) )
		return ret;

	if( !x.thermostat || !x.fan ) {
		i2c_del_driver(&g4fan_driver );
		return -ENODEV;
	}

	/* save registers (if we unload the module) */
	x.r0 = read_reg( x.fan, 0x00, 1 );
	x.r1 = read_reg( x.fan, 0x01, 1 );
	x.r20 = read_reg( x.fan, 0x20, 1 );
	x.r23 = read_reg( x.fan, 0x23, 1 );
	x.r25 = read_reg( x.fan, 0x25, 1 );

	/* improve measurement resolution (convergence time 1.5s) */
	if( (val=read_reg( x.thermostat, 1, 1 )) >= 0 ) {
		val |= 0x60;
		if( write_reg( x.thermostat, 1, val, 1 ) )
			printk("Failed writing config register\n");
	}
	/* disable interrupts and TAC input */
	write_reg( x.fan, 0x01, 0x01, 1 );
	/* enable filter */
	write_reg( x.fan, 0x23, 0x91, 1 );
	/* remote temp. controls fan */
	write_reg( x.fan, 0x00, 0x95, 1 );

	/* The thermostat (which besides measureing temperature controls
	 * has a THERM output which puts the fan on 100%) is usually
	 * set to kick in at 80 C (chip default). We reduce this a bit
	 * to be on the safe side (OSX doesn't)...
	 */
	if( x.overheat_temp == (80 << 8) ) {
		x.overheat_temp = 65 << 8;
		x.overheat_hyst = 60 << 8;
		write_reg( x.thermostat, 2, x.overheat_hyst, 2 );
		write_reg( x.thermostat, 3, x.overheat_temp, 2 );

		print_temp("Reducing overheating limit to ", x.overheat_temp );
		print_temp(" (Hyst: ", x.overheat_hyst );
		printk(")\n");
	}

	/* set an initial fan setting */
	x.upind = x.downind = 1;
	tune_fan( fan_up_table[x.upind].fan_setting );

	INIT_WORK(&poll_work, poll_temp, NULL);

	init_timer( &x.timer );
	x.timer.expires = jiffies + 8*HZ;
	x.timer.function = schedule_poll;
	add_timer( &x.timer );
	return 0;
}

static void __exit
g4fan_exit( void )
{
	del_timer( &x.timer );

	write_reg( x.fan, 0x01, x.r1, 1 );
	write_reg( x.fan, 0x20, x.r20, 1 );
	write_reg( x.fan, 0x23, x.r23, 1 );
	write_reg( x.fan, 0x25, x.r25, 1 );
	write_reg( x.fan, 0x00, x.r0, 1 );

	i2c_del_driver( &g4fan_driver );
}

module_init(g4fan_init);
module_exit(g4fan_exit);

