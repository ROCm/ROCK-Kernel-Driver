/*
    lm85.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> 
    Copyright (c) 2002, 2003  Philip Pokorny <ppokorny@penguincomputing.com>
    Copyright (c) 2003        Margit Schubert-While <margitsw@t-online.de>

    Chip details at	      <http://www.national.com/ds/LM/LM85.pdf>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/i2c-sensor.h>
#include <linux/i2c-vid.h>
/*
#include <asm/io.h>
*/

#undef	LM85EXTENDEDFUNC	/* Extended functionality */

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };
static unsigned short normal_i2c_range[] = { I2C_CLIENT_END };
static unsigned int normal_isa[] = { I2C_CLIENT_ISA_END };
static unsigned int normal_isa_range[] = { I2C_CLIENT_ISA_END };

/* Insmod parameters */
SENSORS_INSMOD_4(lm85b, lm85c, adm1027, adt7463);

/* Enable debug if true */
static int	lm85debug = 0;

/* The LM85 registers */

#define	LM85_REG_IN(nr)			(0x20 + (nr))
#define	LM85_REG_IN_MIN(nr)		(0x44 + (nr) * 2)
#define	LM85_REG_IN_MAX(nr)		(0x45 + (nr) * 2)

#define	LM85_REG_TEMP(nr)		(0x25 + (nr))
#define	LM85_REG_TEMP_MIN(nr)		(0x4e + (nr) * 2)
#define	LM85_REG_TEMP_MAX(nr)		(0x4f + (nr) * 2)

/* Fan speeds are LSB, MSB (2 bytes) */
#define	LM85_REG_FAN(nr)		(0x28 + (nr) *2)
#define	LM85_REG_FAN_MIN(nr)		(0x54 + (nr) *2)

#define	LM85_REG_PWM(nr)		(0x30 + (nr))

#define	ADT7463_REG_OPPOINT(nr)		(0x33 + (nr))

#define	ADT7463_REG_TMIN_CTL1		0x36
#define	ADT7463_REG_TMIN_CTL2		0x37

#define	LM85_REG_DEVICE			0x3d
#define	LM85_REG_COMPANY		0x3e
#define	LM85_REG_VERSTEP		0x3f
/* These are the recognized values for the above regs */
#define	LM85_DEVICE_ADX			0x27
#define	LM85_COMPANY_NATIONAL		0x01
#define	LM85_COMPANY_ANALOG_DEV		0x41
#define	LM85_VERSTEP_GENERIC		0x60
#define	LM85_VERSTEP_LM85C		0x60
#define	LM85_VERSTEP_LM85B		0x62
#define	LM85_VERSTEP_ADM1027		0x60
#define	LM85_VERSTEP_ADT7463		0x62

#define	LM85_REG_CONFIG			0x40

#define	LM85_REG_ALARM1			0x41
#define	LM85_REG_ALARM2			0x42

#define	LM85_REG_VID			0x43

/* Automated FAN control */
#define	LM85_REG_AFAN_CONFIG(nr)	(0x5c + (nr))
#define	LM85_REG_AFAN_RANGE(nr)		(0x5f + (nr))
#define	LM85_REG_AFAN_SPIKE1		0x62
#define	LM85_REG_AFAN_SPIKE2		0x63
#define	LM85_REG_AFAN_MINPWM(nr)	(0x64 + (nr))
#define	LM85_REG_AFAN_LIMIT(nr)		(0x67 + (nr))
#define	LM85_REG_AFAN_CRITICAL(nr)	(0x6a + (nr))
#define	LM85_REG_AFAN_HYST1		0x6d
#define	LM85_REG_AFAN_HYST2		0x6e

#define	LM85_REG_TACH_MODE		0x74
#define	LM85_REG_SPINUP_CTL		0x75

#define	ADM1027_REG_TEMP_OFFSET(nr)	(0x70 + (nr))
#define	ADM1027_REG_CONFIG2		0x73
#define	ADM1027_REG_INTMASK1		0x74
#define	ADM1027_REG_INTMASK2		0x75
#define	ADM1027_REG_EXTEND_ADC1		0x76
#define	ADM1027_REG_EXTEND_ADC2		0x77
#define	ADM1027_REG_CONFIG3		0x78
#define	ADM1027_REG_FAN_PPR		0x7b

#define	ADT7463_REG_THERM		0x79
#define	ADT7463_REG_THERM_LIMIT		0x7A

#define	LM85_ALARM_IN0			0x0001
#define	LM85_ALARM_IN1			0x0002
#define	LM85_ALARM_IN2			0x0004
#define	LM85_ALARM_IN3			0x0008
#define	LM85_ALARM_TEMP1		0x0010
#define	LM85_ALARM_TEMP2		0x0020
#define	LM85_ALARM_TEMP3		0x0040
#define	LM85_ALARM_ALARM2		0x0080
#define	LM85_ALARM_IN4			0x0100
#define	LM85_ALARM_RESERVED		0x0200
#define	LM85_ALARM_FAN1			0x0400
#define	LM85_ALARM_FAN2			0x0800
#define	LM85_ALARM_FAN3			0x1000
#define	LM85_ALARM_FAN4			0x2000
#define	LM85_ALARM_TEMP1_FAULT		0x4000
#define	LM85_ALARM_TEMP3_FAULT		0x8000


/* Conversions. Rounding and limit checking is only done on the TO_REG 
   variants. Note that you should be a bit careful with which arguments
   these macros are called: arguments may be evaluated more than once.
 */

/* IN are scaled 1.000 == 0xc0, mag = 3 */
#define IN_TO_REG(val)		(SENSORS_LIMIT((((val)*0xc0+500)/1000),0,255))
#define INEXT_FROM_REG(val,ext) (((val)*1000 + (ext)*250 + 96)/0xc0)
#define IN_FROM_REG(val)	(INEXT_FROM_REG(val,0))

/* IN are scaled acording to built-in resistors */
static int lm85_scaling[] = {  /* .001 Volts */
		2500, 2250, 3300, 5000, 12000
	};
#define SCALE(val,from,to)		(((val)*(to) + ((from)/2))/(from))
#define INS_TO_REG(n,val)		(SENSORS_LIMIT(SCALE(val,lm85_scaling[n],192),0,255))
#define INSEXT_FROM_REG(n,val,ext)	(SCALE((val)*4 + (ext),192*4,lm85_scaling[n]))
#define INS_FROM_REG(n,val)		(INSEXT_FROM_REG(n,val,0))

/* FAN speed is measured using 90kHz clock */
#define FAN_TO_REG(val)		(SENSORS_LIMIT( (val)<=0?0: 5400000/(val),0,65534))
#define FAN_FROM_REG(val)	((val)==0?-1:(val)==0xffff?0:5400000/(val))

/* Temperature is reported in .001 degC increments */
#define TEMP_TO_REG(val)		(SENSORS_LIMIT(((val)+500)/1000,-127,127))
#define TEMPEXT_FROM_REG(val,ext)	((val)*1000 + (ext)*250)
#define TEMP_FROM_REG(val)		(TEMPEXT_FROM_REG(val,0))
#define EXTTEMP_TO_REG(val)		(SENSORS_LIMIT((val)/250,-127,127))

#define PWM_TO_REG(val)			(SENSORS_LIMIT(val,0,255))
#define PWM_FROM_REG(val)		(val)

#define EXT_FROM_REG(val,sensor)	(((val)>>(sensor * 2))&0x03)

#ifdef	LM85EXTENDEDFUNC	/* Extended functionality */

/* ZONEs have the following parameters:
 *    Limit (low) temp,           1. degC
 *    Hysteresis (below limit),   1. degC (0-15)
 *    Range of speed control,     .1 degC (2-80)
 *    Critical (high) temp,       1. degC
 *
 * FAN PWMs have the following parameters:
 *    Reference Zone,                 1, 2, 3, etc.
 *    Spinup time,                    .05 sec
 *    PWM value at limit/low temp,    1 count
 *    PWM Frequency,                  1. Hz
 *    PWM is Min or OFF below limit,  flag
 *    Invert PWM output,              flag
 *
 * Some chips filter the temp, others the fan.
 *    Filter constant (or disabled)   .1 seconds
 */

/* These are the zone temperature range encodings */
static int lm85_range_map[] = {   /* .1 degC */
		 20,  25,  33,  40,  50,  66,
		 80, 100, 133, 160, 200, 266,
		320, 400, 533, 800
	};
static int RANGE_TO_REG( int range )
{
	int i;

	if( range >= lm85_range_map[15] ) { return 15 ; }
	for( i = 0 ; i < 15 ; ++i )
		if( range <= lm85_range_map[i] )
			break ;
	return( i & 0x0f );
}
#define RANGE_FROM_REG(val) (lm85_range_map[(val)&0x0f])

/* These are the Acoustic Enhancement, or Temperature smoothing encodings
 * NOTE: The enable/disable bit is INCLUDED in these encodings as the
 *       MSB (bit 3, value 8).  If the enable bit is 0, the encoded value
 *       is ignored, or set to 0.
 */
static int lm85_smooth_map[] = {  /* .1 sec */
		350, 176, 118,  70,  44,   30,   16,    8
/*    35.4 *    1/1, 1/2, 1/3, 1/5, 1/8, 1/12, 1/24, 1/48  */
	};
static int SMOOTH_TO_REG( int smooth )
{
	int i;

	if( smooth <= 0 ) { return 0 ; }  /* Disabled */
	for( i = 0 ; i < 7 ; ++i )
		if( smooth >= lm85_smooth_map[i] )
			break ;
	return( (i & 0x07) | 0x08 );
}
#define SMOOTH_FROM_REG(val) ((val)&0x08?lm85_smooth_map[(val)&0x07]:0)

/* These are the fan spinup delay time encodings */
static int lm85_spinup_map[] = {  /* .1 sec */
		0, 1, 2, 4, 7, 10, 20, 40
	};
static int SPINUP_TO_REG( int spinup )
{
	int i;

	if( spinup >= lm85_spinup_map[7] ) { return 7 ; }
	for( i = 0 ; i < 7 ; ++i )
		if( spinup <= lm85_spinup_map[i] )
			break ;
	return( i & 0x07 );
}
#define SPINUP_FROM_REG(val) (lm85_spinup_map[(val)&0x07])

/* These are the PWM frequency encodings */
static int lm85_freq_map[] = { /* .1 Hz */
		100, 150, 230, 300, 380, 470, 620, 980
	};
static int FREQ_TO_REG( int freq )
{
	int i;

	if( freq >= lm85_freq_map[7] ) { return 7 ; }
	for( i = 0 ; i < 7 ; ++i )
		if( freq <= lm85_freq_map[i] )
			break ;
	return( i & 0x07 );
}
#define FREQ_FROM_REG(val) (lm85_freq_map[(val)&0x07])

/* Since we can't use strings, I'm abusing these numbers
 *   to stand in for the following meanings:
 *      1 -- PWM responds to Zone 1
 *      2 -- PWM responds to Zone 2
 *      3 -- PWM responds to Zone 3
 *     23 -- PWM responds to the higher temp of Zone 2 or 3
 *    123 -- PWM responds to highest of Zone 1, 2, or 3
 *      0 -- PWM is always at 0% (ie, off)
 *     -1 -- PWM is always at 100%
 *     -2 -- PWM responds to manual control
 */

#endif		/* Extended functionality */

static int lm85_zone_map[] = { 1, 2, 3, -1, 0, 23, 123, -2 };
#define ZONE_FROM_REG(val) (lm85_zone_map[((val)>>5)&0x07])

#ifdef	LM85EXTENDEDFUNC	/* Extended functionality */

static int ZONE_TO_REG( int zone )
{
	int i;

	for( i = 0 ; i <= 7 ; ++i )
		if( zone == lm85_zone_map[i] )
			break ;
	if( i > 7 )   /* Not found. */
		i = 3;  /* Always 100% */
	return( (i & 0x07)<<5 );
}

#endif		/* Extended functionality */

#define HYST_TO_REG(val) (SENSORS_LIMIT((-(val)+5)/10,0,15))
#define HYST_FROM_REG(val) (-(val)*10)

#define OFFSET_TO_REG(val) (SENSORS_LIMIT((val)/25,-127,127))
#define OFFSET_FROM_REG(val) ((val)*25)

#define PPR_MASK(fan) (0x03<<(fan *2))
#define PPR_TO_REG(val,fan) (SENSORS_LIMIT((val)-1,0,3)<<(fan *2))
#define PPR_FROM_REG(val,fan) ((((val)>>(fan * 2))&0x03)+1)

/* i2c-vid.h defines vid_from_reg() */
#define VID_FROM_REG(val,vrm) (vid_from_reg((val),(vrm)))

#define ALARMS_FROM_REG(val) (val)

/* Unlike some other drivers we DO NOT set initial limits.  Use
 * the config file to set limits.  Some users have reported
 * motherboards shutting down when we set limits in a previous
 * version of the driver.
 */

/* Typically used with Pentium 4 systems v9.1 VRM spec */
#define LM85_INIT_VRM  91

/* Chip sampling rates
 *
 * Some sensors are not updated more frequently than once per second
 *    so it doesn't make sense to read them more often than that.
 *    We cache the results and return the saved data if the driver
 *    is called again before a second has elapsed.
 *
 * Also, there is significant configuration data for this chip
 *    given the automatic PWM fan control that is possible.  There
 *    are about 47 bytes of config data to only 22 bytes of actual
 *    readings.  So, we keep the config data up to date in the cache
 *    when it is written and only sample it once every 1 *minute*
 */
#define LM85_DATA_INTERVAL  (HZ + HZ / 2)
#define LM85_CONFIG_INTERVAL  (1 * 60 * HZ)

/* For each registered LM85, we need to keep some data in memory. That
   data is pointed to by lm85_list[NR]->data. The structure itself is
   dynamically allocated, at the same time when a new lm85 client is
   allocated. */

/* LM85 can automatically adjust fan speeds based on temperature
 * This structure encapsulates an entire Zone config.  There are
 * three zones (one for each temperature input) on the lm85
 */
struct lm85_zone {
	s8 limit;	/* Low temp limit */
	u8 hyst;	/* Low limit hysteresis. (0-15) */
	u8 range;	/* Temp range, encoded */
	s8 critical;	/* "All fans ON" temp limit */
};

struct lm85_autofan {
	u8 config;	/* Register value */
	u8 freq;	/* PWM frequency, encoded */
	u8 min_pwm;	/* Minimum PWM value, encoded */
	u8 min_off;	/* Min PWM or OFF below "limit", flag */
};

struct lm85_data {
	struct semaphore lock;
	enum chips type;

	struct semaphore update_lock;
	int valid;		/* !=0 if following fields are valid */
	unsigned long last_reading;	/* In jiffies */
	unsigned long last_config;	/* In jiffies */

	u8 in[5];		/* Register value */
	u8 in_max[5];		/* Register value */
	u8 in_min[5];		/* Register value */
	s8 temp[3];		/* Register value */
	s8 temp_min[3];		/* Register value */
	s8 temp_max[3];		/* Register value */
	s8 temp_offset[3];	/* Register value */
	u16 fan[4];		/* Register value */
	u16 fan_min[4];		/* Register value */
	u8 pwm[3];		/* Register value */
	u8 spinup_ctl;		/* Register encoding, combined */
	u8 tach_mode;		/* Register encoding, combined */
	u16 extend_adc;		/* Register value */
	u8 fan_ppr;		/* Register value */
	u8 smooth[3];		/* Register encoding */
	u8 vid;			/* Register value */
	u8 vrm;			/* VRM version */
	u8 syncpwm3;		/* Saved PWM3 for TACH 2,3,4 config */
	u8 oppoint[3];		/* Register value */
	u16 tmin_ctl;		/* Register value */
	u32 therm_total;	/* Cummulative therm count */
	u8 therm_limit;		/* Register value */
	u16 alarms;		/* Register encoding, combined */
	struct lm85_autofan autofan[3];
	struct lm85_zone zone[3];
};

static int lm85_attach_adapter(struct i2c_adapter *adapter);
static int lm85_detect(struct i2c_adapter *adapter, int address,
			int kind);
static int lm85_detach_client(struct i2c_client *client);

static int lm85_read_value(struct i2c_client *client, u8 register);
static int lm85_write_value(struct i2c_client *client, u8 register, int value);
static void lm85_update_client(struct i2c_client *client);
static void lm85_init_client(struct i2c_client *client);


static struct i2c_driver lm85_driver = {
	.owner          = THIS_MODULE,
	.name           = "lm85",
	.id             = I2C_DRIVERID_LM85,
	.flags          = I2C_DF_NOTIFY,
	.attach_adapter = lm85_attach_adapter,
	.detach_client  = lm85_detach_client,
};

/* Unique ID assigned to each LM85 detected */
static int lm85_id = 0;


/* 4 Fans */
static ssize_t show_fan(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf,"%d\n", FAN_FROM_REG(data->fan[nr]) );
}
static ssize_t show_fan_min(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf,"%d\n", FAN_FROM_REG(data->fan_min[nr]) );
}
static ssize_t set_fan_min(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int	val;

	down(&data->update_lock);
	val = simple_strtol(buf, NULL, 10);
	data->fan_min[nr] = FAN_TO_REG(val);
	lm85_write_value(client, LM85_REG_FAN_MIN(nr), data->fan_min[nr]);
	up(&data->update_lock);
	return count;
}

#define show_fan_offset(offset)						\
static ssize_t show_fan_##offset (struct device *dev, char *buf)	\
{									\
	return show_fan(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t show_fan_##offset##_min (struct device *dev, char *buf)	\
{									\
	return show_fan_min(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t set_fan_##offset##_min (struct device *dev, 		\
	const char *buf, size_t count) 					\
{									\
	return set_fan_min(dev, buf, count, 0x##offset - 1);		\
}									\
static DEVICE_ATTR(fan_input##offset, S_IRUGO, show_fan_##offset, NULL) \
static DEVICE_ATTR(fan_min##offset, S_IRUGO | S_IWUSR, 			\
		show_fan_##offset##_min, set_fan_##offset##_min)

show_fan_offset(1);
show_fan_offset(2);
show_fan_offset(3);
show_fan_offset(4);

/* vid, vrm, alarms */

static ssize_t show_vid_reg(struct device *dev, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf, "%ld\n", (long) vid_from_reg(data->vid, data->vrm));
}

static DEVICE_ATTR(vid, S_IRUGO, show_vid_reg, NULL)

static ssize_t show_vrm_reg(struct device *dev, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf, "%ld\n", (long) data->vrm);
}

static ssize_t store_vrm_reg(struct device *dev, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);
	data->vrm = val;
	return count;
}

static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm_reg, store_vrm_reg)

static ssize_t show_alarms_reg(struct device *dev, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf, "%ld\n", (long) ALARMS_FROM_REG(data->alarms));
}

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms_reg, NULL)

/* pwm */

static ssize_t show_pwm(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf,"%d\n", PWM_FROM_REG(data->pwm[nr]) );
}
static ssize_t set_pwm(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int	val;

	down(&data->update_lock);
	val = simple_strtol(buf, NULL, 10);
	data->pwm[nr] = PWM_TO_REG(val);
	lm85_write_value(client, LM85_REG_PWM(nr), data->pwm[nr]);
	up(&data->update_lock);
	return count;
}
static ssize_t show_pwm_enable(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int	pwm_zone;

	lm85_update_client(client);
	pwm_zone = ZONE_FROM_REG(data->autofan[nr].config);
	return sprintf(buf,"%d\n", (pwm_zone != 0 && pwm_zone != -1) );
}

#define show_pwm_reg(offset)						\
static ssize_t show_pwm_##offset (struct device *dev, char *buf)	\
{									\
	return show_pwm(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t set_pwm_##offset (struct device *dev,			\
				 const char *buf, size_t count)		\
{									\
	return set_pwm(dev, buf, count, 0x##offset - 1);		\
}									\
static ssize_t show_pwm_enable##offset (struct device *dev, char *buf)	\
{									\
	return show_pwm_enable(dev, buf, 0x##offset - 1);			\
}									\
static DEVICE_ATTR(pwm##offset, S_IRUGO | S_IWUSR, 			\
		show_pwm_##offset, set_pwm_##offset)			\
static DEVICE_ATTR(pwm_enable##offset, S_IRUGO, show_pwm_enable##offset, NULL)

show_pwm_reg(1);
show_pwm_reg(2);
show_pwm_reg(3);

/* Voltages */

static ssize_t show_in(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf,"%d\n", INS_FROM_REG(nr, data->in[nr]) );
}
static ssize_t show_in_min(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf,"%d\n", INS_FROM_REG(nr, data->in_min[nr]) );
}
static ssize_t set_in_min(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int	val;

	down(&data->update_lock);
	val = simple_strtol(buf, NULL, 10);
	data->in_min[nr] = INS_TO_REG(nr, val);
	lm85_write_value(client, LM85_REG_IN_MIN(nr), data->in_min[nr]);
	up(&data->update_lock);
	return count;
}
static ssize_t show_in_max(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf,"%d\n", INS_FROM_REG(nr, data->in_max[nr]) );
}
static ssize_t set_in_max(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int	val;

	down(&data->update_lock);
	val = simple_strtol(buf, NULL, 10);
	data->in_max[nr] = INS_TO_REG(nr, val);
	lm85_write_value(client, LM85_REG_IN_MAX(nr), data->in_max[nr]);
	up(&data->update_lock);
	return count;
}
#define show_in_reg(offset)						\
static ssize_t show_in_##offset (struct device *dev, char *buf)		\
{									\
	return show_in(dev, buf, 0x##offset);				\
}									\
static ssize_t show_in_##offset##_min (struct device *dev, char *buf)	\
{									\
	return show_in_min(dev, buf, 0x##offset);			\
}									\
static ssize_t show_in_##offset##_max (struct device *dev, char *buf)	\
{									\
	return show_in_max(dev, buf, 0x##offset);			\
}									\
static ssize_t set_in_##offset##_min (struct device *dev, 		\
	const char *buf, size_t count) 					\
{									\
	return set_in_min(dev, buf, count, 0x##offset);			\
}									\
static ssize_t set_in_##offset##_max (struct device *dev, 		\
	const char *buf, size_t count) 					\
{									\
	return set_in_max(dev, buf, count, 0x##offset);			\
}									\
static DEVICE_ATTR(in_input##offset, S_IRUGO, show_in_##offset, NULL)	\
static DEVICE_ATTR(in_min##offset, S_IRUGO | S_IWUSR, 			\
		show_in_##offset##_min, set_in_##offset##_min)		\
static DEVICE_ATTR(in_max##offset, S_IRUGO | S_IWUSR, 			\
		show_in_##offset##_max, set_in_##offset##_max)

show_in_reg(0);
show_in_reg(1);
show_in_reg(2);
show_in_reg(3);
show_in_reg(4);

/* Temps */

static ssize_t show_temp(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf,"%d\n", TEMP_FROM_REG(data->temp[nr]) );
}
static ssize_t show_temp_min(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf,"%d\n", TEMP_FROM_REG(data->temp_min[nr]) );
}
static ssize_t set_temp_min(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int	val;

	down(&data->update_lock);
	val = simple_strtol(buf, NULL, 10);
	data->temp_min[nr] = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_TEMP_MIN(nr), data->temp_min[nr]);
	up(&data->update_lock);
	return count;
}
static ssize_t show_temp_max(struct device *dev, char *buf, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);

	lm85_update_client(client);
	return sprintf(buf,"%d\n", TEMP_FROM_REG(data->temp_max[nr]) );
}
static ssize_t set_temp_max(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int	val;

	down(&data->update_lock);
	val = simple_strtol(buf, NULL, 10);
	data->temp_max[nr] = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_TEMP_MAX(nr), data->temp_max[nr]);
	up(&data->update_lock);
	return count;
}
#define show_temp_reg(offset)						\
static ssize_t show_temp_##offset (struct device *dev, char *buf)	\
{									\
	return show_temp(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t show_temp_##offset##_min (struct device *dev, char *buf)	\
{									\
	return show_temp_min(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t show_temp_##offset##_max (struct device *dev, char *buf)	\
{									\
	return show_temp_max(dev, buf, 0x##offset - 1);			\
}									\
static ssize_t set_temp_##offset##_min (struct device *dev, 		\
	const char *buf, size_t count) 					\
{									\
	return set_temp_min(dev, buf, count, 0x##offset - 1);		\
}									\
static ssize_t set_temp_##offset##_max (struct device *dev, 		\
	const char *buf, size_t count) 					\
{									\
	return set_temp_max(dev, buf, count, 0x##offset - 1);		\
}									\
static DEVICE_ATTR(temp_input##offset, S_IRUGO, show_temp_##offset, NULL)	\
static DEVICE_ATTR(temp_min##offset, S_IRUGO | S_IWUSR, 		\
		show_temp_##offset##_min, set_temp_##offset##_min)	\
static DEVICE_ATTR(temp_max##offset, S_IRUGO | S_IWUSR, 		\
		show_temp_##offset##_max, set_temp_##offset##_max)

show_temp_reg(1);
show_temp_reg(2);
show_temp_reg(3);


int lm85_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_detect(adapter, &addr_data, lm85_detect);
}

int lm85_detect(struct i2c_adapter *adapter, int address,
		int kind)
{
	int company, verstep ;
	struct i2c_client *new_client = NULL;
	struct lm85_data *data;
	int err = 0;
	const char *type_name = "";

	if (i2c_is_isa_adapter(adapter)) {
		/* This chip has no ISA interface */
		goto ERROR0 ;
	};

	if (!i2c_check_functionality(adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		/* We need to be able to do byte I/O */
		goto ERROR0 ;
	};

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access lm85_{read,write}_value. */

	if (!(new_client = kmalloc((sizeof(struct i2c_client)) +
				    sizeof(struct lm85_data),
				    GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR0;
	}

	memset(new_client, 0, sizeof(struct i2c_client) +
			      sizeof(struct lm85_data));
	data = (struct lm85_data *) (new_client + 1);
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &lm85_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. */

	company = lm85_read_value(new_client, LM85_REG_COMPANY);
	verstep = lm85_read_value(new_client, LM85_REG_VERSTEP);

	if (lm85debug) {
		printk("lm85: Detecting device at %d,0x%02x with"
		" COMPANY: 0x%02x and VERSTEP: 0x%02x\n",
		i2c_adapter_id(new_client->adapter), new_client->addr,
		company, verstep);
	}

	/* If auto-detecting, Determine the chip type. */
	if (kind <= 0) {
		if (lm85debug) {
			printk("lm85: Autodetecting device at %d,0x%02x ...\n",
			i2c_adapter_id(adapter), address );
		}
		if( company == LM85_COMPANY_NATIONAL
		    && verstep == LM85_VERSTEP_LM85C ) {
			kind = lm85c ;
		} else if( company == LM85_COMPANY_NATIONAL
		    && verstep == LM85_VERSTEP_LM85B ) {
			kind = lm85b ;
		} else if( company == LM85_COMPANY_NATIONAL
		    && (verstep & 0xf0) == LM85_VERSTEP_GENERIC ) {
			printk("lm85: Unrecgonized version/stepping 0x%02x"
			    " Defaulting to LM85.\n", verstep );
			kind = any_chip ;
		} else if( company == LM85_COMPANY_ANALOG_DEV
		    && verstep == LM85_VERSTEP_ADM1027 ) {
			kind = adm1027 ;
		} else if( company == LM85_COMPANY_ANALOG_DEV
		    && verstep == LM85_VERSTEP_ADT7463 ) {
			kind = adt7463 ;
		} else if( company == LM85_COMPANY_ANALOG_DEV
		    && (verstep & 0xf0) == LM85_VERSTEP_GENERIC ) {
			printk("lm85: Unrecgonized version/stepping 0x%02x"
			    " Defaulting to ADM1027.\n", verstep );
			kind = adm1027 ;
		} else if( kind == 0 && (verstep & 0xf0) == 0x60) {
			printk("lm85: Generic LM85 Version 6 detected\n");
			/* Leave kind as "any_chip" */
		} else {
			if (lm85debug) {
				printk("lm85: Autodetection failed\n");
			}
			/* Not an LM85 ... */
			if( kind == 0 ) {  /* User used force=x,y */
			    printk("lm85: Generic LM85 Version 6 not"
				" found at %d,0x%02x. Try force_lm85c.\n",
				i2c_adapter_id(adapter), address );
			}
			err = 0 ;
			goto ERROR1;
		}
	}

	/* Fill in the chip specific driver values */
	if ( kind == any_chip ) {
		type_name = "lm85";
	} else if ( kind == lm85b ) {
		type_name = "lm85b";
	} else if ( kind == lm85c ) {
		type_name = "lm85c";
	} else if ( kind == adm1027 ) {
		type_name = "adm1027";
	} else if ( kind == adt7463 ) {
		type_name = "adt7463";
	} else {
		dev_dbg(&adapter->dev, "Internal error, invalid kind (%d)!", kind);
		err = -EFAULT ;
		goto ERROR1;
	}
	strlcpy(new_client->name, type_name, I2C_NAME_SIZE);

	/* Fill in the remaining client fields */
	new_client->id = lm85_id++;
	data->type = kind;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	if (lm85debug) {
		printk("lm85: Assigning ID %d to %s at %d,0x%02x\n",
		new_client->id, new_client->name,
		i2c_adapter_id(new_client->adapter),
		new_client->addr);
	}

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR1;

	/* Set the VRM version */
	data->vrm = LM85_INIT_VRM ;

	/* Initialize the LM85 chip */
	lm85_init_client(new_client);

	/* Register sysfs hooks */
	device_create_file(&new_client->dev, &dev_attr_fan_input1);
	device_create_file(&new_client->dev, &dev_attr_fan_input2);
	device_create_file(&new_client->dev, &dev_attr_fan_input3);
	device_create_file(&new_client->dev, &dev_attr_fan_input4);
	device_create_file(&new_client->dev, &dev_attr_fan_min1);
	device_create_file(&new_client->dev, &dev_attr_fan_min2);
	device_create_file(&new_client->dev, &dev_attr_fan_min3);
	device_create_file(&new_client->dev, &dev_attr_fan_min4);
	device_create_file(&new_client->dev, &dev_attr_pwm1);
	device_create_file(&new_client->dev, &dev_attr_pwm2);
	device_create_file(&new_client->dev, &dev_attr_pwm3);
	device_create_file(&new_client->dev, &dev_attr_pwm_enable1);
	device_create_file(&new_client->dev, &dev_attr_pwm_enable2);
	device_create_file(&new_client->dev, &dev_attr_pwm_enable3);
	device_create_file(&new_client->dev, &dev_attr_in_input0);
	device_create_file(&new_client->dev, &dev_attr_in_input1);
	device_create_file(&new_client->dev, &dev_attr_in_input2);
	device_create_file(&new_client->dev, &dev_attr_in_input3);
	device_create_file(&new_client->dev, &dev_attr_in_input4);
	device_create_file(&new_client->dev, &dev_attr_in_min0);
	device_create_file(&new_client->dev, &dev_attr_in_min1);
	device_create_file(&new_client->dev, &dev_attr_in_min2);
	device_create_file(&new_client->dev, &dev_attr_in_min3);
	device_create_file(&new_client->dev, &dev_attr_in_min4);
	device_create_file(&new_client->dev, &dev_attr_in_max0);
	device_create_file(&new_client->dev, &dev_attr_in_max1);
	device_create_file(&new_client->dev, &dev_attr_in_max2);
	device_create_file(&new_client->dev, &dev_attr_in_max3);
	device_create_file(&new_client->dev, &dev_attr_in_max4);
	device_create_file(&new_client->dev, &dev_attr_temp_input1);
	device_create_file(&new_client->dev, &dev_attr_temp_input2);
	device_create_file(&new_client->dev, &dev_attr_temp_input3);
	device_create_file(&new_client->dev, &dev_attr_temp_min1);
	device_create_file(&new_client->dev, &dev_attr_temp_min2);
	device_create_file(&new_client->dev, &dev_attr_temp_min3);
	device_create_file(&new_client->dev, &dev_attr_temp_max1);
	device_create_file(&new_client->dev, &dev_attr_temp_max2);
	device_create_file(&new_client->dev, &dev_attr_temp_max3);
	device_create_file(&new_client->dev, &dev_attr_vrm);
	device_create_file(&new_client->dev, &dev_attr_vid);
	device_create_file(&new_client->dev, &dev_attr_alarms);

	return 0;

	/* Error out and cleanup code */
    ERROR1:
	kfree(new_client);
    ERROR0:
	return err;
}

int lm85_detach_client(struct i2c_client *client)
{
	i2c_detach_client(client);
	kfree(client);
	return 0;
}


int lm85_read_value(struct i2c_client *client, u8 reg)
{
	int res;

	/* What size location is it? */
	switch( reg ) {
	case LM85_REG_FAN(0) :  /* Read WORD data */
	case LM85_REG_FAN(1) :
	case LM85_REG_FAN(2) :
	case LM85_REG_FAN(3) :
	case LM85_REG_FAN_MIN(0) :
	case LM85_REG_FAN_MIN(1) :
	case LM85_REG_FAN_MIN(2) :
	case LM85_REG_FAN_MIN(3) :
	case LM85_REG_ALARM1 :	/* Read both bytes at once */
	case ADM1027_REG_EXTEND_ADC1 :  /* Read two bytes at once */
		res = i2c_smbus_read_byte_data(client, reg) & 0xff ;
		res |= i2c_smbus_read_byte_data(client, reg+1) << 8 ;
		break ;
	case ADT7463_REG_TMIN_CTL1 :  /* Read WORD MSB, LSB */
		res = i2c_smbus_read_byte_data(client, reg) << 8 ;
		res |= i2c_smbus_read_byte_data(client, reg+1) & 0xff ;
		break ;
	default:	/* Read BYTE data */
		res = i2c_smbus_read_byte_data(client, reg);
		break ;
	}

	return res ;
}

int lm85_write_value(struct i2c_client *client, u8 reg, int value)
{
	int res ;

	switch( reg ) {
	case LM85_REG_FAN(0) :  /* Write WORD data */
	case LM85_REG_FAN(1) :
	case LM85_REG_FAN(2) :
	case LM85_REG_FAN(3) :
	case LM85_REG_FAN_MIN(0) :
	case LM85_REG_FAN_MIN(1) :
	case LM85_REG_FAN_MIN(2) :
	case LM85_REG_FAN_MIN(3) :
	/* NOTE: ALARM is read only, so not included here */
		res = i2c_smbus_write_byte_data(client, reg, value & 0xff) ;
		res |= i2c_smbus_write_byte_data(client, reg+1, (value>>8) & 0xff) ;
		break ;
	case ADT7463_REG_TMIN_CTL1 :  /* Write WORD MSB, LSB */
		res = i2c_smbus_write_byte_data(client, reg, (value>>8) & 0xff);
		res |= i2c_smbus_write_byte_data(client, reg+1, value & 0xff) ;
		break ;
	default:	/* Write BYTE data */
		res = i2c_smbus_write_byte_data(client, reg, value);
		break ;
	}

	return res ;
}

void lm85_init_client(struct i2c_client *client)
{
	int value;
	struct lm85_data *data = i2c_get_clientdata(client);

	if (lm85debug) {
		printk("lm85(%d): Initializing device\n", client->id);
	}

	/* Warn if part was not "READY" */
	value = lm85_read_value(client, LM85_REG_CONFIG);
	if (lm85debug) {
		printk("lm85(%d): LM85_REG_CONFIG is: 0x%02x\n", client->id, value );
	}
	if( value & 0x02 ) {
		printk("lm85(%d): Client (%d,0x%02x) config is locked.\n",
			    client->id,
			    i2c_adapter_id(client->adapter), client->addr );
	};
	if( ! (value & 0x04) ) {
		printk("lm85(%d): Client (%d,0x%02x) is not ready.\n",
			    client->id,
			    i2c_adapter_id(client->adapter), client->addr );
	};
	if( value & 0x10
	    && ( data->type == adm1027
		|| data->type == adt7463 ) ) {
		printk("lm85(%d): Client (%d,0x%02x) VxI mode is set.  "
			"Please report this to the lm85 maintainer.\n",
			    client->id,
			    i2c_adapter_id(client->adapter), client->addr );
	};

	/* WE INTENTIONALLY make no changes to the limits,
	 *   offsets, pwms, fans and zones.  If they were
	 *   configured, we don't want to mess with them.
	 *   If they weren't, the default is 100% PWM, no
	 *   control and will suffice until 'sensors -s'
	 *   can be run by the user.
	 */

	/* Start monitoring */
	value = lm85_read_value(client, LM85_REG_CONFIG);
	/* Try to clear LOCK, Set START, save everything else */
	value = (value & ~ 0x02) | 0x01 ;
	if (lm85debug) {
		printk("lm85(%d): Setting CONFIG to: 0x%02x\n", client->id, value );
	}
	lm85_write_value(client, LM85_REG_CONFIG, value);

}

void lm85_update_client(struct i2c_client *client)
{
	struct lm85_data *data = i2c_get_clientdata(client);
	int i;

	down(&data->update_lock);

	if ( !data->valid ||
	     (jiffies - data->last_reading > LM85_DATA_INTERVAL ) ) {
		/* Things that change quickly */

		if (lm85debug) {
			printk("lm85(%d): Reading sensor values\n", client->id);
		}
		/* Have to read extended bits first to "freeze" the
		 * more significant bits that are read later.
		 */
		if ( (data->type == adm1027) || (data->type == adt7463) ) {
			data->extend_adc =
			    lm85_read_value(client, ADM1027_REG_EXTEND_ADC1);
		}

		for (i = 0; i <= 4; ++i) {
			data->in[i] =
			    lm85_read_value(client, LM85_REG_IN(i));
		}

		for (i = 0; i <= 3; ++i) {
			data->fan[i] =
			    lm85_read_value(client, LM85_REG_FAN(i));
		}

		for (i = 0; i <= 2; ++i) {
			data->temp[i] =
			    lm85_read_value(client, LM85_REG_TEMP(i));
		}

		for (i = 0; i <= 2; ++i) {
			data->pwm[i] =
			    lm85_read_value(client, LM85_REG_PWM(i));
		}

		if ( data->type == adt7463 ) {
			if( data->therm_total < ULONG_MAX - 256 ) {
			    data->therm_total +=
				lm85_read_value(client, ADT7463_REG_THERM );
			}
		}

		data->alarms = lm85_read_value(client, LM85_REG_ALARM1);

		data->last_reading = jiffies ;
	};  /* last_reading */

	if ( !data->valid ||
	     (jiffies - data->last_config > LM85_CONFIG_INTERVAL) ) {
		/* Things that don't change often */

		if (lm85debug) {
			printk("lm85(%d): Reading config values\n", client->id);
		}
		for (i = 0; i <= 4; ++i) {
			data->in_min[i] =
			    lm85_read_value(client, LM85_REG_IN_MIN(i));
			data->in_max[i] =
			    lm85_read_value(client, LM85_REG_IN_MAX(i));
		}

		for (i = 0; i <= 3; ++i) {
			data->fan_min[i] =
			    lm85_read_value(client, LM85_REG_FAN_MIN(i));
		}

		for (i = 0; i <= 2; ++i) {
			data->temp_min[i] =
			    lm85_read_value(client, LM85_REG_TEMP_MIN(i));
			data->temp_max[i] =
			    lm85_read_value(client, LM85_REG_TEMP_MAX(i));
		}

		data->vid = lm85_read_value(client, LM85_REG_VID);

		for (i = 0; i <= 2; ++i) {
			int val ;
			data->autofan[i].config =
			    lm85_read_value(client, LM85_REG_AFAN_CONFIG(i));
			val = lm85_read_value(client, LM85_REG_AFAN_RANGE(i));
			data->autofan[i].freq = val & 0x07 ;
			data->zone[i].range = (val >> 4) & 0x0f ;
			data->autofan[i].min_pwm =
			    lm85_read_value(client, LM85_REG_AFAN_MINPWM(i));
			data->zone[i].limit =
			    lm85_read_value(client, LM85_REG_AFAN_LIMIT(i));
			data->zone[i].critical =
			    lm85_read_value(client, LM85_REG_AFAN_CRITICAL(i));
		}

		i = lm85_read_value(client, LM85_REG_AFAN_SPIKE1);
		data->smooth[0] = i & 0x0f ;
		data->syncpwm3 = i & 0x10 ;  /* Save PWM3 config */
		data->autofan[0].min_off = (i & 0x20) != 0 ;
		data->autofan[1].min_off = (i & 0x40) != 0 ;
		data->autofan[2].min_off = (i & 0x80) != 0 ;
		i = lm85_read_value(client, LM85_REG_AFAN_SPIKE2);
		data->smooth[1] = (i>>4) & 0x0f ;
		data->smooth[2] = i & 0x0f ;

		i = lm85_read_value(client, LM85_REG_AFAN_HYST1);
		data->zone[0].hyst = (i>>4) & 0x0f ;
		data->zone[1].hyst = i & 0x0f ;

		i = lm85_read_value(client, LM85_REG_AFAN_HYST2);
		data->zone[2].hyst = (i>>4) & 0x0f ;

		if ( (data->type == lm85b) || (data->type == lm85c) ) {
			data->tach_mode = lm85_read_value(client,
				LM85_REG_TACH_MODE );
			data->spinup_ctl = lm85_read_value(client,
				LM85_REG_SPINUP_CTL );
		} else if ( (data->type == adt7463) || (data->type == adm1027) ) {
			if ( data->type == adt7463 ) {
				for (i = 0; i <= 2; ++i) {
				    data->oppoint[i] = lm85_read_value(client,
					ADT7463_REG_OPPOINT(i) );
				}
				data->tmin_ctl = lm85_read_value(client,
					ADT7463_REG_TMIN_CTL1 );
				data->therm_limit = lm85_read_value(client,
					ADT7463_REG_THERM_LIMIT );
			}
			for (i = 0; i <= 2; ++i) {
			    data->temp_offset[i] = lm85_read_value(client,
				ADM1027_REG_TEMP_OFFSET(i) );
			}
			data->tach_mode = lm85_read_value(client,
				ADM1027_REG_CONFIG3 );
			data->fan_ppr = lm85_read_value(client,
				ADM1027_REG_FAN_PPR );
		}
	
		data->last_config = jiffies;
	};  /* last_config */

	data->valid = 1;

	up(&data->update_lock);
}


static int __init sm_lm85_init(void)
{
	return i2c_add_driver(&lm85_driver);
}

static void  __exit sm_lm85_exit(void)
{
	i2c_del_driver(&lm85_driver);
}

/* Thanks to Richard Barrington for adding the LM85 to sensors-detect.
 * Thanks to Margit Schubert-While <margitsw@t-online.de> for help with
 *     post 2.7.0 CVS changes
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Philip Pokorny <ppokorny@penguincomputing.com>, Margit Schubert-While <margitsw@t-online.de>");
MODULE_DESCRIPTION("LM85-B, LM85-C driver");
MODULE_PARM(lm85debug, "i");
MODULE_PARM_DESC(lm85debug, "Enable debugging statements");

module_init(sm_lm85_init);
module_exit(sm_lm85_exit);
