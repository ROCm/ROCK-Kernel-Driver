/*
 *   arch/ppc64/kernel/rtas-proc.c
 *   Copyright (C) 2000 Tilmann Bitterberg
 *   (tilmann@bitterberg.de)
 *
 *   RTAS (Runtime Abstraction Services) stuff
 *   Intention is to provide a clean user interface
 *   to use the RTAS.
 *
 *   TODO:
 *   Split off a header file and maybe move it to a different
 *   location. Write Documentation on what the /proc/rtas/ entries
 *   actually do.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/machdep.h> /* for ppc_md */
#include <asm/time.h>

/* Token for Sensors */
#define KEY_SWITCH		0x0001
#define ENCLOSURE_SWITCH	0x0002
#define THERMAL_SENSOR		0x0003
#define LID_STATUS		0x0004
#define POWER_SOURCE		0x0005
#define BATTERY_VOLTAGE		0x0006
#define BATTERY_REMAINING	0x0007
#define BATTERY_PERCENTAGE	0x0008
#define EPOW_SENSOR		0x0009
#define BATTERY_CYCLESTATE	0x000a
#define BATTERY_CHARGING	0x000b

/* IBM specific sensors */
#define IBM_SURVEILLANCE	0x2328 /* 9000 */
#define IBM_FANRPM		0x2329 /* 9001 */
#define IBM_VOLTAGE		0x232a /* 9002 */
#define IBM_DRCONNECTOR		0x232b /* 9003 */
#define IBM_POWERSUPPLY		0x232c /* 9004 */
#define IBM_INTQUEUE		0x232d /* 9005 */

/* Status return values */
#define SENSOR_CRITICAL_HIGH	13
#define SENSOR_WARNING_HIGH	12
#define SENSOR_NORMAL		11
#define SENSOR_WARNING_LOW	10
#define SENSOR_CRITICAL_LOW	 9
#define SENSOR_SUCCESS		 0
#define SENSOR_HW_ERROR		-1
#define SENSOR_BUSY		-2
#define SENSOR_NOT_EXIST	-3
#define SENSOR_DR_ENTITY	-9000

/* Location Codes */
#define LOC_SCSI_DEV_ADDR	'A'
#define LOC_SCSI_DEV_LOC	'B'
#define LOC_CPU			'C'
#define LOC_DISKETTE		'D'
#define LOC_ETHERNET		'E'
#define LOC_FAN			'F'
#define LOC_GRAPHICS		'G'
/* reserved / not used		'H' */
#define LOC_IO_ADAPTER		'I'
/* reserved / not used		'J' */
#define LOC_KEYBOARD		'K'
#define LOC_LCD			'L'
#define LOC_MEMORY		'M'
#define LOC_NV_MEMORY		'N'
#define LOC_MOUSE		'O'
#define LOC_PLANAR		'P'
#define LOC_OTHER_IO		'Q'
#define LOC_PARALLEL		'R'
#define LOC_SERIAL		'S'
#define LOC_DEAD_RING		'T'
#define LOC_RACKMOUNTED		'U' /* for _u_nit is rack mounted */
#define LOC_VOLTAGE		'V'
#define LOC_SWITCH_ADAPTER	'W'
#define LOC_OTHER		'X'
#define LOC_FIRMWARE		'Y'
#define LOC_SCSI		'Z'

/* Tokens for indicators */
#define TONE_FREQUENCY		0x0001 /* 0 - 1000 (HZ)*/
#define TONE_VOLUME		0x0002 /* 0 - 100 (%) */
#define SYSTEM_POWER_STATE	0x0003 
#define WARNING_LIGHT		0x0004
#define DISK_ACTIVITY_LIGHT	0x0005
#define HEX_DISPLAY_UNIT	0x0006
#define BATTERY_WARNING_TIME	0x0007
#define CONDITION_CYCLE_REQUEST	0x0008
#define SURVEILLANCE_INDICATOR	0x2328 /* 9000 */
#define DR_ACTION		0x2329 /* 9001 */
#define DR_INDICATOR		0x232a /* 9002 */
/* 9003 - 9004: Vendor specific */
#define GLOBAL_INTERRUPT_QUEUE	0x232d /* 9005 */
/* 9006 - 9999: Vendor specific */

/* other */
#define MAX_SENSORS		 17  /* I only know of 17 sensors */    
#define MAX_LINELENGTH          256
#define SENSOR_PREFIX		"ibm,sensor-"
#define cel_to_fahr(x)		((x*9/5)+32)


/* Globals */
static struct rtas_sensors sensors;
static struct device_node *rtas_node = NULL;
static unsigned long power_on_time = 0; /* Save the time the user set */
static char progress_led[MAX_LINELENGTH];

static unsigned long rtas_tone_frequency = 1000;
static unsigned long rtas_tone_volume = 0;

/* ****************STRUCTS******************************************* */
struct individual_sensor {
	unsigned int token;
	unsigned int quant;
};

struct rtas_sensors {
        struct individual_sensor sensor[MAX_SENSORS];
	unsigned int quant;
};

/* ****************************************************************** */
/* Declarations */
static int ppc_rtas_sensor_read(char * buf, char ** start, off_t off,
		int count, int *eof, void *data);
static ssize_t ppc_rtas_clock_read(struct file * file, char * buf, 
		size_t count, loff_t *ppos);
static ssize_t ppc_rtas_clock_write(struct file * file, const char * buf, 
		size_t count, loff_t *ppos);
static ssize_t ppc_rtas_progress_read(struct file * file, char * buf,
		size_t count, loff_t *ppos);
static ssize_t ppc_rtas_progress_write(struct file * file, const char * buf,
		size_t count, loff_t *ppos);
static ssize_t ppc_rtas_poweron_read(struct file * file, char * buf,
		size_t count, loff_t *ppos);
static ssize_t ppc_rtas_poweron_write(struct file * file, const char * buf,
		size_t count, loff_t *ppos);

static ssize_t ppc_rtas_tone_freq_write(struct file * file, const char * buf,
		size_t count, loff_t *ppos);
static ssize_t ppc_rtas_tone_freq_read(struct file * file, char * buf,
		size_t count, loff_t *ppos);
static ssize_t ppc_rtas_tone_volume_write(struct file * file, const char * buf,
		size_t count, loff_t *ppos);
static ssize_t ppc_rtas_tone_volume_read(struct file * file, char * buf,
		size_t count, loff_t *ppos);
static ssize_t ppc_rtas_rmo_buf_read(struct file *file, char *buf,
				    size_t count, loff_t *ppos);

struct file_operations ppc_rtas_poweron_operations = {
	.read =		ppc_rtas_poweron_read,
	.write =	ppc_rtas_poweron_write
};
struct file_operations ppc_rtas_progress_operations = {
	.read =		ppc_rtas_progress_read,
	.write =	ppc_rtas_progress_write
};

struct file_operations ppc_rtas_clock_operations = {
	.read =		ppc_rtas_clock_read,
	.write =	ppc_rtas_clock_write
};

struct file_operations ppc_rtas_tone_freq_operations = {
	.read =		ppc_rtas_tone_freq_read,
	.write =	ppc_rtas_tone_freq_write
};
struct file_operations ppc_rtas_tone_volume_operations = {
	.read =		ppc_rtas_tone_volume_read,
	.write =	ppc_rtas_tone_volume_write
};

static struct file_operations ppc_rtas_rmo_buf_ops = {
	.read =		ppc_rtas_rmo_buf_read,
};

int ppc_rtas_find_all_sensors (void);
int ppc_rtas_process_sensor(struct individual_sensor s, int state, 
		int error, char * buf);
char * ppc_rtas_process_error(int error);
int get_location_code(struct individual_sensor s, char * buf);
int check_location_string (char *c, char * buf);
int check_location (char *c, int idx, char * buf);

static int __init proc_rtas_init(void)
{
	struct proc_dir_entry *entry;

	if (!(systemcfg->platform & PLATFORM_PSERIES))
		return 1;

	rtas_node = of_find_node_by_name(NULL, "rtas");
	if (rtas_node == NULL)
		return 1;

	entry = create_proc_entry("ppc64/rtas/progress", S_IRUGO|S_IWUSR, NULL);
	if (entry)
		entry->proc_fops = &ppc_rtas_progress_operations;

	entry = create_proc_entry("ppc64/rtas/clock", S_IRUGO|S_IWUSR, NULL);
	if (entry)
		entry->proc_fops = &ppc_rtas_clock_operations;

	entry = create_proc_entry("ppc64/rtas/poweron", S_IWUSR|S_IRUGO, NULL);
	if (entry)
		entry->proc_fops = &ppc_rtas_poweron_operations;

	create_proc_read_entry("ppc64/rtas/sensors", S_IRUGO, NULL,
			       ppc_rtas_sensor_read, NULL);

	entry = create_proc_entry("ppc64/rtas/frequency", S_IWUSR|S_IRUGO,
				  NULL);
	if (entry)
		entry->proc_fops = &ppc_rtas_tone_freq_operations;

	entry = create_proc_entry("ppc64/rtas/volume", S_IWUSR|S_IRUGO, NULL);
	if (entry)
		entry->proc_fops = &ppc_rtas_tone_volume_operations;

	entry = create_proc_entry("ppc64/rtas/rmo_buffer", S_IRUSR, NULL);
	if (entry)
		entry->proc_fops = &ppc_rtas_rmo_buf_ops;

	return 0;
}

__initcall(proc_rtas_init);

/* ****************************************************************** */
/* POWER-ON-TIME                                                      */
/* ****************************************************************** */
static ssize_t ppc_rtas_poweron_write(struct file * file, const char * buf,
		size_t count, loff_t *ppos)
{
	char stkbuf[40];  /* its small, its on stack */
	struct rtc_time tm;
	unsigned long nowtime;
	char *dest;
	int error;

	if (39 < count) count = 39;
	if (copy_from_user (stkbuf, buf, count)) {
		return -EFAULT;
	}
	stkbuf[count] = 0;
	nowtime = simple_strtoul(stkbuf, &dest, 10);
	if (*dest != '\0' && *dest != '\n') {
		printk("ppc_rtas_poweron_write: Invalid time\n");
		return count;
	}
	power_on_time = nowtime; /* save the time */

	to_tm(nowtime, &tm);

	error = rtas_call(rtas_token("set-time-for-power-on"), 7, 1, NULL, 
			tm.tm_year, tm.tm_mon, tm.tm_mday, 
			tm.tm_hour, tm.tm_min, tm.tm_sec, 0 /* nano */);
	if (error != 0)
		printk(KERN_WARNING "error: setting poweron time returned: %s\n", 
				ppc_rtas_process_error(error));
	return count;
}
/* ****************************************************************** */
static ssize_t ppc_rtas_poweron_read(struct file * file, char * buf,
		size_t count, loff_t *ppos)
{
	char stkbuf[40];  /* its small, its on stack */
	int n, sn;
	if (power_on_time == 0)
		n = scnprintf(stkbuf,sizeof(stkbuf),"Power on time not set\n");
	else
		n = scnprintf(stkbuf,sizeof(stkbuf),"%lu\n",power_on_time);

	sn = strlen (stkbuf) +1;
	if (*ppos >= sn)
		return 0;
	if (n > sn - *ppos)
		n = sn - *ppos;
	if (n > count)
		n = count;
	if (copy_to_user (buf, stkbuf + (*ppos), n)) {
		return -EFAULT;
	}
	*ppos += n;
	return n;
}

/* ****************************************************************** */
/* PROGRESS                                                           */
/* ****************************************************************** */
static ssize_t ppc_rtas_progress_write(struct file * file, const char * buf,
		size_t count, loff_t *ppos)
{
	unsigned long hex;

	if (count >= MAX_LINELENGTH) count = MAX_LINELENGTH -1;
	if (copy_from_user (progress_led, buf, count)) { /* save the string */
		return -EFAULT;
	}
	progress_led[count] = 0;

	/* Lets see if the user passed hexdigits */
	hex = simple_strtoul(progress_led, NULL, 10);

	ppc_md.progress ((char *)progress_led, hex);
	return count;

	/* clear the line */ /* ppc_md.progress("                   ", 0xffff);*/
}
/* ****************************************************************** */
static ssize_t ppc_rtas_progress_read(struct file * file, char * buf,
		size_t count, loff_t *ppos)
{
	int sn, n = 0;
	char *tmpbuf;

	if (progress_led == NULL) return 0;

	tmpbuf = kmalloc (MAX_LINELENGTH, GFP_KERNEL);
	if (!tmpbuf) {
		printk(KERN_ERR "error: kmalloc failed\n");
		return -ENOMEM;
	}
	n = sprintf (tmpbuf, "%s\n", progress_led);

	sn = strlen (tmpbuf) +1;
	if (*ppos >= sn) {
		kfree (tmpbuf);
		return 0;
	}
	if (n > sn - *ppos)
		n = sn - *ppos;
	if (n > count)
		n = count;
	if (copy_to_user (buf, tmpbuf + (*ppos), n)) {
		kfree (tmpbuf);
		return -EFAULT;
	}
	kfree (tmpbuf);
	*ppos += n;
	return n;
}

/* ****************************************************************** */
/* CLOCK                                                              */
/* ****************************************************************** */
static ssize_t ppc_rtas_clock_write(struct file * file, const char * buf, 
		size_t count, loff_t *ppos)
{
	char stkbuf[40];  /* its small, its on stack */
	struct rtc_time tm;
	unsigned long nowtime;
	char *dest;
	int error;

	if (39 < count) count = 39;
	if (copy_from_user (stkbuf, buf, count)) {
		return -EFAULT;
	}
	stkbuf[count] = 0;
	nowtime = simple_strtoul(stkbuf, &dest, 10);
	if (*dest != '\0' && *dest != '\n') {
		printk("ppc_rtas_clock_write: Invalid time\n");
		return count;
	}

	to_tm(nowtime, &tm);
	error = rtas_call(rtas_token("set-time-of-day"), 7, 1, NULL, 
			tm.tm_year, tm.tm_mon, tm.tm_mday, 
			tm.tm_hour, tm.tm_min, tm.tm_sec, 0);
	if (error != 0)
		printk(KERN_WARNING "error: setting the clock returned: %s\n", 
				ppc_rtas_process_error(error));
	return count;
}
/* ****************************************************************** */
static ssize_t ppc_rtas_clock_read(struct file * file, char * buf, 
		size_t count, loff_t *ppos)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned long *ret = kmalloc(4*8, GFP_KERNEL);
	int n, sn, error;
	char stkbuf[40];  /* its small, its on stack */

	error = rtas_call(rtas_token("get-time-of-day"), 0, 8, ret);
	
	year = ret[0]; mon  = ret[1]; day  = ret[2];
	hour = ret[3]; min  = ret[4]; sec  = ret[5];

	if (error != 0){
		printk(KERN_WARNING "error: reading the clock returned: %s\n", 
				ppc_rtas_process_error(error));
		n = scnprintf (stkbuf, sizeof(stkbuf), "0");
	} else { 
		n = scnprintf (stkbuf, sizeof(stkbuf), "%lu\n",
				mktime(year, mon, day, hour, min, sec));
	}
	kfree(ret);

	sn = strlen (stkbuf) +1;
	if (*ppos >= sn)
		return 0;
	if (n > sn - *ppos)
		n = sn - *ppos;
	if (n > count)
		n = count;
	if (copy_to_user (buf, stkbuf + (*ppos), n)) {
		return -EFAULT;
	}
	*ppos += n;
	return n;
}

/* ****************************************************************** */
/* SENSOR STUFF                                                       */
/* ****************************************************************** */
static int ppc_rtas_sensor_read(char * buf, char ** start, off_t off,
		int count, int *eof, void *data)
{
	int i,j,n;
	unsigned long ret;
	int state, error;
	char *buffer;
	int get_sensor_state = rtas_token("get-sensor-state");

	if (count < 0)
		return -EINVAL;

	/* May not be enough */
	buffer = kmalloc(MAX_LINELENGTH*MAX_SENSORS, GFP_KERNEL);

	if (!buffer)
		return -ENOMEM;

	memset(buffer, 0, MAX_LINELENGTH*MAX_SENSORS);

	n  = sprintf ( buffer  , "RTAS (RunTime Abstraction Services) Sensor Information\n");
	n += sprintf ( buffer+n, "Sensor\t\tValue\t\tCondition\tLocation\n");
	n += sprintf ( buffer+n, "********************************************************\n");

	if (ppc_rtas_find_all_sensors() != 0) {
		n += sprintf ( buffer+n, "\nNo sensors are available\n");
		goto return_string;
	}

	for (i=0; i<sensors.quant; i++) {
		j = sensors.sensor[i].quant;
		/* A sensor may have multiple instances */
		while (j >= 0) {

			error =	rtas_call(get_sensor_state, 2, 2, &ret, 
				  	  sensors.sensor[i].token, 
				  	  sensors.sensor[i].quant - j);

			state = (int) ret;
			n += ppc_rtas_process_sensor(sensors.sensor[i], state, 
					     	     error, buffer+n );
			n += sprintf (buffer+n, "\n");
			j--;
		} /* while */
	} /* for */

return_string:
	if (off >= strlen(buffer)) {
		*eof = 1;
		kfree(buffer);
		return 0;
	}
	if (n > strlen(buffer) - off)
		n = strlen(buffer) - off;
	if (n > count)
		n = count;
	else
		*eof = 1;

	memcpy(buf, buffer + off, n);
	*start = buf;
	kfree(buffer);
	return n;
}

/* ****************************************************************** */

int ppc_rtas_find_all_sensors (void)
{
	unsigned int *utmp;
	int len, i;

	utmp = (unsigned int *) get_property(rtas_node, "rtas-sensors", &len);
	if (utmp == NULL) {
		printk (KERN_ERR "error: could not get rtas-sensors\n");
		return 1;
	}

	sensors.quant = len / 8;      /* int + int */

	for (i=0; i<sensors.quant; i++) {
		sensors.sensor[i].token = *utmp++;
		sensors.sensor[i].quant = *utmp++;
	}
	return 0;
}

/* ****************************************************************** */
/*
 * Builds a string of what rtas returned
 */
char * ppc_rtas_process_error(int error)
{
	switch (error) {
		case SENSOR_CRITICAL_HIGH:
			return "(critical high)";
		case SENSOR_WARNING_HIGH:
			return "(warning high)";
		case SENSOR_NORMAL:
			return "(normal)";
		case SENSOR_WARNING_LOW:
			return "(warning low)";
		case SENSOR_CRITICAL_LOW:
			return "(critical low)";
		case SENSOR_SUCCESS:
			return "(read ok)";
		case SENSOR_HW_ERROR:
			return "(hardware error)";
		case SENSOR_BUSY:
			return "(busy)";
		case SENSOR_NOT_EXIST:
			return "(non existent)";
		case SENSOR_DR_ENTITY:
			return "(dr entity removed)";
		default:
			return "(UNKNOWN)";
	}
}

/* ****************************************************************** */
/*
 * Builds a string out of what the sensor said
 */

int ppc_rtas_process_sensor(struct individual_sensor s, int state, 
		int error, char * buf) 
{
	/* Defined return vales */
	const char * key_switch[]        = { "Off\t", "Normal\t", "Secure\t", 
						"Maintenance" };
	const char * enclosure_switch[]  = { "Closed", "Open" };
	const char * lid_status[]        = { " ", "Open", "Closed" };
	const char * power_source[]      = { "AC\t", "Battery", 
		  				"AC & Battery" };
	const char * battery_remaining[] = { "Very Low", "Low", "Mid", "High" };
	const char * epow_sensor[]       = { 
		"EPOW Reset", "Cooling warning", "Power warning",
		"System shutdown", "System halt", "EPOW main enclosure",
		"EPOW power off" };
	const char * battery_cyclestate[]  = { "None", "In progress", 
						"Requested" };
	const char * battery_charging[]    = { "Charging", "Discharching", 
						"No current flow" };
	const char * ibm_drconnector[]     = { "Empty", "Present", "Unusable", 
						"Exchange" };
	const char * ibm_intqueue[]        = { "Disabled", "Enabled" };

	int have_strings = 0;
	int num_states = 0;
	int temperature = 0;
	int unknown = 0;
	int n = 0;

	/* What kind of sensor do we have here? */
	
	switch (s.token) {
		case KEY_SWITCH:
			n += sprintf(buf+n, "Key switch:\t");
			num_states = sizeof(key_switch) / sizeof(char *);
			if (state < num_states) {
				n += sprintf(buf+n, "%s\t", key_switch[state]);
				have_strings = 1;
			}
			break;
		case ENCLOSURE_SWITCH:
			n += sprintf(buf+n, "Enclosure switch:\t");
			num_states = sizeof(enclosure_switch) / sizeof(char *);
			if (state < num_states) {
				n += sprintf(buf+n, "%s\t", 
						enclosure_switch[state]);
				have_strings = 1;
			}
			break;
		case THERMAL_SENSOR:
			n += sprintf(buf+n, "Temp. (°C/°F):\t");
			temperature = 1;
			break;
		case LID_STATUS:
			n += sprintf(buf+n, "Lid status:\t");
			num_states = sizeof(lid_status) / sizeof(char *);
			if (state < num_states) {
				n += sprintf(buf+n, "%s\t", lid_status[state]);
				have_strings = 1;
			}
			break;
		case POWER_SOURCE:
			n += sprintf(buf+n, "Power source:\t");
			num_states = sizeof(power_source) / sizeof(char *);
			if (state < num_states) {
				n += sprintf(buf+n, "%s\t", 
						power_source[state]);
				have_strings = 1;
			}
			break;
		case BATTERY_VOLTAGE:
			n += sprintf(buf+n, "Battery voltage:\t");
			break;
		case BATTERY_REMAINING:
			n += sprintf(buf+n, "Battery remaining:\t");
			num_states = sizeof(battery_remaining) / sizeof(char *);
			if (state < num_states)
			{
				n += sprintf(buf+n, "%s\t", 
						battery_remaining[state]);
				have_strings = 1;
			}
			break;
		case BATTERY_PERCENTAGE:
			n += sprintf(buf+n, "Battery percentage:\t");
			break;
		case EPOW_SENSOR:
			n += sprintf(buf+n, "EPOW Sensor:\t");
			num_states = sizeof(epow_sensor) / sizeof(char *);
			if (state < num_states) {
				n += sprintf(buf+n, "%s\t", epow_sensor[state]);
				have_strings = 1;
			}
			break;
		case BATTERY_CYCLESTATE:
			n += sprintf(buf+n, "Battery cyclestate:\t");
			num_states = sizeof(battery_cyclestate) / 
				     	sizeof(char *);
			if (state < num_states) {
				n += sprintf(buf+n, "%s\t", 
						battery_cyclestate[state]);
				have_strings = 1;
			}
			break;
		case BATTERY_CHARGING:
			n += sprintf(buf+n, "Battery Charging:\t");
			num_states = sizeof(battery_charging) / sizeof(char *);
			if (state < num_states) {
				n += sprintf(buf+n, "%s\t", 
						battery_charging[state]);
				have_strings = 1;
			}
			break;
		case IBM_SURVEILLANCE:
			n += sprintf(buf+n, "Surveillance:\t");
			break;
		case IBM_FANRPM:
			n += sprintf(buf+n, "Fan (rpm):\t");
			break;
		case IBM_VOLTAGE:
			n += sprintf(buf+n, "Voltage (mv):\t");
			break;
		case IBM_DRCONNECTOR:
			n += sprintf(buf+n, "DR connector:\t");
			num_states = sizeof(ibm_drconnector) / sizeof(char *);
			if (state < num_states) {
				n += sprintf(buf+n, "%s\t", 
						ibm_drconnector[state]);
				have_strings = 1;
			}
			break;
		case IBM_POWERSUPPLY:
			n += sprintf(buf+n, "Powersupply:\t");
			break;
		case IBM_INTQUEUE:
			n += sprintf(buf+n, "Interrupt queue:\t");
			num_states = sizeof(ibm_intqueue) / sizeof(char *);
			if (state < num_states) {
				n += sprintf(buf+n, "%s\t", 
						ibm_intqueue[state]);
				have_strings = 1;
			}
			break;
		default:
			n += sprintf(buf+n,  "Unknown sensor (type %d), ignoring it\n",
					s.token);
			unknown = 1;
			have_strings = 1;
			break;
	}
	if (have_strings == 0) {
		if (temperature) {
			n += sprintf(buf+n, "%4d /%4d\t", state, cel_to_fahr(state));
		} else
			n += sprintf(buf+n, "%10d\t", state);
	}
	if (unknown == 0) {
		n += sprintf ( buf+n, "%s\t", ppc_rtas_process_error(error));
		n += get_location_code(s, buf+n);
	}
	return n;
}

/* ****************************************************************** */

int check_location (char *c, int idx, char * buf)
{
	int n = 0;

	switch (*(c+idx)) {
		case LOC_PLANAR:
			n += sprintf ( buf, "Planar #%c", *(c+idx+1));
			break;
		case LOC_CPU:
			n += sprintf ( buf, "CPU #%c", *(c+idx+1));
			break;
		case LOC_FAN:
			n += sprintf ( buf, "Fan #%c", *(c+idx+1));
			break;
		case LOC_RACKMOUNTED:
			n += sprintf ( buf, "Rack #%c", *(c+idx+1));
			break;
		case LOC_VOLTAGE:
			n += sprintf ( buf, "Voltage #%c", *(c+idx+1));
			break;
		case LOC_LCD:
			n += sprintf ( buf, "LCD #%c", *(c+idx+1));
			break;
		case '.':
			n += sprintf ( buf, "- %c", *(c+idx+1));
		default:
			n += sprintf ( buf, "Unknown location");
			break;
	}
	return n;
}


/* ****************************************************************** */
/* 
 * Format: 
 * ${LETTER}${NUMBER}[[-/]${LETTER}${NUMBER} [ ... ] ]
 * the '.' may be an abbrevation
 */
int check_location_string (char *c, char *buf)
{
	int n=0,i=0;

	while (c[i]) {
		if (isalpha(c[i]) || c[i] == '.') {
			 n += check_location(c, i, buf+n);
		}
		else if (c[i] == '/' || c[i] == '-')
			n += sprintf(buf+n, " at ");
		i++;
	}
	return n;
}


/* ****************************************************************** */

int get_location_code(struct individual_sensor s, char * buffer)
{
	char rstr[512], tmp[10], tmp2[10];
	int n=0, i=0, llen, len;
	/* char *buf = kmalloc(MAX_LINELENGTH, GFP_KERNEL); */
	char *ret;

	static int pos = 0; /* remember position where buffer was */

	/* construct the sensor number like 0003 */
	/* fill with zeros */
	n = sprintf(tmp, "%d", s.token);
	len = strlen(tmp);
	while (strlen(tmp) < 4)
		n += sprintf (tmp+n, "0");
	
	/* invert the string */
	while (tmp[i]) {
		if (i<len)
			tmp2[4-len+i] = tmp[i];
		else
			tmp2[3-i] = tmp[i];
		i++;
	}
	tmp2[4] = '\0';

	sprintf (rstr, SENSOR_PREFIX"%s", tmp2);

	ret = (char *) get_property(rtas_node, rstr, &llen);

	n=0;
	if (ret == NULL || ret[0] == '\0') {
		n += sprintf ( buffer+n, "--- ");/* does not have a location */
	} else {
		char t[50];
		ret += pos;

		n += check_location_string(ret, buffer + n);
		n += sprintf ( buffer+n, " ");
		/* see how many characters we have printed */
		scnprintf(t, sizeof(t), "%s ", ret);

		pos += strlen(t);
		if (pos >= llen) pos=0;
	}
	return n;
}
/* ****************************************************************** */
/* INDICATORS - Tone Frequency                                        */
/* ****************************************************************** */
static ssize_t ppc_rtas_tone_freq_write(struct file * file, const char * buf,
		size_t count, loff_t *ppos)
{
	char stkbuf[40];  /* its small, its on stack */
	unsigned long freq;
	char *dest;
	int error;

	if (39 < count) count = 39;
	if (copy_from_user (stkbuf, buf, count)) {
		return -EFAULT;
	}
	stkbuf[count] = 0;
	freq = simple_strtoul(stkbuf, &dest, 10);
	if (*dest != '\0' && *dest != '\n') {
		printk("ppc_rtas_tone_freq_write: Invalid tone freqency\n");
		return count;
	}
	if (freq < 0) freq = 0;
	rtas_tone_frequency = freq; /* save it for later */
	error = rtas_call(rtas_token("set-indicator"), 3, 1, NULL,
			TONE_FREQUENCY, 0, freq);
	if (error != 0)
		printk(KERN_WARNING "error: setting tone frequency returned: %s\n", 
				ppc_rtas_process_error(error));
	return count;
}
/* ****************************************************************** */
static ssize_t ppc_rtas_tone_freq_read(struct file * file, char * buf,
		size_t count, loff_t *ppos)
{
	int n, sn;
	char stkbuf[40];  /* its small, its on stack */

	n = scnprintf(stkbuf, 40, "%lu\n", rtas_tone_frequency);

	sn = strlen (stkbuf) +1;
	if (*ppos >= sn)
		return 0;
	if (n > sn - *ppos)
		n = sn - *ppos;
	if (n > count)
		n = count;
	if (copy_to_user (buf, stkbuf + (*ppos), n)) {
		return -EFAULT;
	}
	*ppos += n;
	return n;
}
/* ****************************************************************** */
/* INDICATORS - Tone Volume                                           */
/* ****************************************************************** */
static ssize_t ppc_rtas_tone_volume_write(struct file * file, const char * buf,
		size_t count, loff_t *ppos)
{
	char stkbuf[40];  /* its small, its on stack */
	unsigned long volume;
	char *dest;
	int error;

	if (39 < count) count = 39;
	if (copy_from_user (stkbuf, buf, count)) {
		return -EFAULT;
	}
	stkbuf[count] = 0;
	volume = simple_strtoul(stkbuf, &dest, 10);
	if (*dest != '\0' && *dest != '\n') {
		printk("ppc_rtas_tone_volume_write: Invalid tone volume\n");
		return count;
	}
	if (volume < 0) volume = 0;
	if (volume > 100) volume = 100;
	
        rtas_tone_volume = volume; /* save it for later */
	error = rtas_call(rtas_token("set-indicator"), 3, 1, NULL,
			TONE_VOLUME, 0, volume);
	if (error != 0)
		printk(KERN_WARNING "error: setting tone volume returned: %s\n", 
				ppc_rtas_process_error(error));
	return count;
}
/* ****************************************************************** */
static ssize_t ppc_rtas_tone_volume_read(struct file * file, char * buf,
		size_t count, loff_t *ppos)
{
	int n, sn;
	char stkbuf[40];  /* its small, its on stack */

	n = scnprintf(stkbuf, 40, "%lu\n", rtas_tone_volume);

	sn = strlen (stkbuf) +1;
	if (*ppos >= sn)
		return 0;
	if (n > sn - *ppos)
		n = sn - *ppos;
	if (n > count)
		n = count;
	if (copy_to_user (buf, stkbuf + (*ppos), n)) {
		return -EFAULT;
	}
	*ppos += n;
	return n;
}

#define RMO_READ_BUF_MAX 30

/* RTAS Userspace access */
static ssize_t ppc_rtas_rmo_buf_read(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	char kbuf[RMO_READ_BUF_MAX];
	int n;

	n = sprintf(kbuf, "%016lx %x\n", rtas_rmo_buf, RTAS_RMOBUF_MAX);
	if (n > count)
		n = count;

	if (ppos && *ppos != 0)
		return 0;

	if (copy_to_user(buf, kbuf, n))
		return -EFAULT;

	if (ppos)
		*ppos = n;
	
	return n;
}
