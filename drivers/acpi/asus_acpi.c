/*
 *  asus_acpi.c - Asus Laptop ACPI Extras
 *
 *
 *  Copyright (C) 2002, 2003 Julien Lerouge, Karol Kozimor
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *  The development page for this driver is located at
 *  http://sourceforge.net/projects/acpi4asus/
 *
 *  Credits:
 *  Johann Wiesner - Small compile fixes
 *  John Belmonte  - ACPI code for Toshiba laptop was a good starting point.
 *
 *  TODO:
 *  add Fn key status
 *  Add mode selection on module loading (parameter) -> still necessary?
 *  Complete display switching -- may require dirty hacks?
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>

#define ASUS_ACPI_VERSION "0.26"

#define PROC_ASUS       "asus"	//the directory
#define PROC_MLED       "mled"
#define PROC_WLED       "wled"
#define PROC_INFOS      "info"
#define PROC_LCD        "lcd"
#define PROC_BRN        "brn"
#define PROC_DISP       "disp"

#define ACPI_HOTK_NAME          "Asus Laptop ACPI Extras Driver"
#define ACPI_HOTK_CLASS         "hotkey"
#define ACPI_HOTK_DEVICE_NAME   "Hotkey"
#define ACPI_HOTK_HID           "ATK0100"

/*
 * Some events we use, same for all Asus
 */
#define BR_UP       0x10      		
#define BR_DOWN     0x20

/*
 * Flags for hotk status
 */
#define MLED_ON     0x01	//is MLED ON ?
#define WLED_ON     0x02

MODULE_AUTHOR("Julien Lerouge, Karol Kozimor");
MODULE_DESCRIPTION(ACPI_HOTK_NAME);
MODULE_LICENSE("GPL");


static uid_t asus_uid = 0;
static gid_t asus_gid = 0;
MODULE_PARM(asus_uid, "i");
MODULE_PARM_DESC(uid, "UID for entries in /proc/acpi/asus.\n");
MODULE_PARM(asus_gid, "i");
MODULE_PARM_DESC(gid, "GID for entries in /proc/acpi/asus.\n");


/* For each model, all features implemented */
struct model_data {
	char *name;		//name of the laptop
	char *mt_mled;		//method to handle mled
	char *mled_status;	//node to handle mled reading
	char *mt_wled;		//method to handle wled
	char *wled_status;	//node to handle wled reading
	char *mt_lcd_switch;	//method to turn LCD ON/OFF
	char *lcd_status;	//node to read LCD panel state
	char *brightness_up;	//method to set brightness up
	char *brightness_down;	//guess what ?
	char *brightness_set;	//method to set absolute brightness
	char *brightness_get;	//method to get absolute brightness
	char *brightness_status;//node to get brightness
	char *display_set;	//method to set video output
	char *display_get;	//method to get video output
};

/*
 * This is the main structure, we can use it to store anything interesting
 * about the hotk device
 */
struct asus_hotk {
	struct acpi_device *device;	//the device we are in
	acpi_handle handle;		//the handle of the hotk device
	char status;			//status of the hotk, for LEDs, ...
	struct model_data *methods;	//methods available on the laptop
	u8 brightness;			//brighness level
	enum {
		A1X=0,  	//A1340D, A1300F
		A2X,		//A2500H
		D1X,		//D1
		L1X,		//L1400B
		L2X,		//L2000D -> TODO check Q11 (Fn+F8)
				//	   Calling this method simply hangs the
				//	   computer, ISMI method hangs the laptop.
		L3D,		//L3400D
		L3X,		//L3C
		L5X,		//L5C TODO this model seems to have one more
		                //         LED, add support
		M2X,		//M2400E
		M3N,		//M3700N, but also S1300N -> TODO WLED
		S1X,		//S1300A -> TODO special keys do not work ?
		S2X,		//S200 (J1 reported), Victor MP-XP7210
				//TODO  A1370D does not seem to have an ATK device 
				//	L8400 model doesn't have ATK
		END_MODEL
	} model;		//Models currently supported
	u16 event_count[128];	//count for each event TODO make this better
};

/* Here we go */
#define L3X_PREFIX "\\_SB.PCI0.PX40.ECD0."
#define S1X_PREFIX "\\_SB.PCI0.PX40."
#define L1X_PREFIX S1X_PREFIX
#define A1X_PREFIX "\\_SB.PCI0.ISA.EC0."
#define S2X_PREFIX A1X_PREFIX
#define M3N_PREFIX "\\_SB.PCI0.SBRG.EC0."

static struct model_data model_conf[END_MODEL] = {
        /*
	 * name|  mled |mled read|  wled |wled read| lcd sw |lcd read | 
	 * br up|br down | br set | br read | br status|set disp | get disp
	 *
	 * br set and read shall be in hotk device !
	 * same for set disp
	 *
	 * TODO I have seen a SWBX and AIBX method on some models, like L1400B,
	 * it seems to be a kind of switch, but what for ?
	 *
	 */
	{"A1X", "MLED", "\\MAIL", NULL, NULL, A1X_PREFIX "_Q10", "\\BKLI",
	 A1X_PREFIX "_Q0E", A1X_PREFIX "_Q0F", NULL, NULL, NULL, NULL, NULL},

	{"A2X", "MLED", NULL, "WLED", "\\SG66", "\\Q10", "\\BAOF",
	 "\\Q0E", "\\Q0F", "SPLV", "GPLV", "\\CMOD", "SDSP", "\\INFB"},

	{"D1X", "MLED", NULL, NULL, NULL, "\\Q0D", "\\GP11", 
	 "\\Q0C", "\\Q0B", NULL, NULL, "\\BLVL", "SDSP","\\INFB"},

	{"L1X", "MLED", NULL, "WLED", NULL, L1X_PREFIX "Q10", "\\PNOF", 
	 L1X_PREFIX "Q0F", L1X_PREFIX "Q0E", "SPLV", "GPLV", "\\BRIT", NULL, NULL},
	 
	{"L2X", "MLED", "\\SGP6", "WLED", "\\RCP3", "\\Q10", "\\SGP0", 
	 "\\Q0E", "\\Q0F", NULL, NULL, NULL, "SDSP", "\\INFB"},

	{"L3D", "MLED", "\\MALD", "WLED", NULL, "\\Q10", "\\BKLG",
	 "\\Q0E", "\\Q0F", "SPLV", "GPLV", "\\BLVL", "SDSP", "\\INFB"},

	{"L3X", "MLED", NULL, "WLED", NULL, L3X_PREFIX "_Q10", "\\GL32", 
	 L3X_PREFIX "_Q0F", L3X_PREFIX "_Q0E", "SPLV", "GPLV", "\\BLVL", "SDSP", 
	 "\\_SB.PCI0.PCI1.VGAC.NMAP"},

	{"L5X", "MLED", NULL, "WLED", "WRED", "\\Q0D", "\\BAOF", 
	 "\\Q0C","\\Q0B", "SPLV", "GPLV", NULL, "SDSP", "\\INFB"},
	 
	{"M2X", "MLED", NULL, "WLED", NULL, "\\Q10", "\\GP06", 
	 "\\Q0E","\\Q0F", "SPLV", "GPLV", NULL, "SDSP", "\\INFB"},

	{"M3N", "MLED", NULL, "WLED", "\\PO33", M3N_PREFIX "_Q10", "\\BKLT", 
	 M3N_PREFIX "_Q0F", M3N_PREFIX "_Q0E", "SPLV", "GPLV", "\\LBTN", "SDSP", 
	 "\\ADVG"},
	
	{"S1X", "MLED", "\\EMLE", "WLED", NULL, S1X_PREFIX "Q10", "\\PNOF", 
	 S1X_PREFIX "Q0F", S1X_PREFIX "Q0E", "SPLV", "GPLV", "\\BRIT", NULL, NULL},
	
	{"S2X", "MLED", "\\MAIL", NULL, NULL, S2X_PREFIX "_Q10", "\\BKLI",
	 S2X_PREFIX "_Q0B", S2X_PREFIX "_Q0A", NULL, NULL, NULL, NULL, NULL}
};

/* procdir we use */
static struct proc_dir_entry *asus_proc_dir = NULL;

/*
 * This header is made available to allow proper configuration given model,
 * revision number , ... this info cannot go in struct asus_hotk because it is
 * available before the hotk
 */
static struct acpi_table_header *asus_info = NULL;

/*
 * The hotkey driver declaration
 */
static int asus_hotk_add(struct acpi_device *device);
static int asus_hotk_remove(struct acpi_device *device, int type);
static struct acpi_driver asus_hotk_driver = {
	.name = 	ACPI_HOTK_NAME,
	.class = 	ACPI_HOTK_CLASS,
	.ids = 		ACPI_HOTK_HID,
	.ops = 		{
				.add = 		asus_hotk_add,
				.remove = 	asus_hotk_remove,
			},
};

/* 
 * This function evaluates an ACPI method, given an int as parameter, the
 * method is searched within the scope of the handle, can be NULL. The output
 * of the method is written is output, which can also be NULL
 *
 * returns 1 if write is successful, 0 else. 
 */
static int write_acpi_int(acpi_handle handle, const char *method, int val,
			  struct acpi_buffer *output)
{
	struct acpi_object_list params;	//list of input parameters (an int here)
	union acpi_object in_obj;	//the only param we use
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = val;

	status = acpi_evaluate_object(handle, (char *) method, &params, output);
	return (status == AE_OK);
}


static int read_acpi_int(acpi_handle handle, const char *method, int *val)
{
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, (char *) method, NULL, &output);
	*val = out_obj.integer.value;
	return (status == AE_OK) && (out_obj.type == ACPI_TYPE_INTEGER);
}

/*
 * We write our info in page, we begin at offset off and cannot write more
 * than count bytes. We set eof to 1 if we handle those 2 values. We return the
 * number of bytes written in page
 */
static int
proc_read_info(char *page, char **start, off_t off, int count, int *eof,
		void *data)
{
	int len = 0;
	int sfun;
	struct asus_hotk *hotk = (struct asus_hotk *) data;
	char buf[16];		//enough for all info
	/*
	 * We use the easy way, we don't care of off and count, so we don't set eof
	 * to 1
	 */

	len += sprintf(page, ACPI_HOTK_NAME " " ASUS_ACPI_VERSION "\n");
	len += sprintf(page + len, "Model reference    : %s\n", 
		       hotk->methods->name);
	if(read_acpi_int(hotk->handle, "SFUN", &sfun))
		len += sprintf(page + len, "SFUN value         : 0x%04x\n", sfun);
	if (asus_info) {
		snprintf(buf, 16, "%d", asus_info->length);
		len += sprintf(page + len, "DSDT length        : %s\n", buf);
		snprintf(buf, 16, "%d", asus_info->checksum);
		len += sprintf(page + len, "DSDT checksum      : %s\n", buf);
		snprintf(buf, 16, "%d", asus_info->revision);
		len += sprintf(page + len, "DSDT revision      : %s\n", buf);
		snprintf(buf, 7, "%s", asus_info->oem_id);
		len += sprintf(page + len, "OEM id             : %s\n", buf);
		snprintf(buf, 9, "%s", asus_info->oem_table_id);
		len += sprintf(page + len, "OEM table id       : %s\n", buf);
		snprintf(buf, 16, "%x", asus_info->oem_revision);
		len += sprintf(page + len, "OEM revision       : 0x%s\n", buf);
		snprintf(buf, 5, "%s", asus_info->asl_compiler_id);
		len += sprintf(page + len, "ASL comp vendor id : %s\n", buf);
		snprintf(buf, 16, "%x", asus_info->asl_compiler_revision);
		len += sprintf(page + len, "ASL comp revision  : 0x%s\n", buf);
	}

	return len;
}


/* 
 * proc file handlers
 */
static int
proc_read_mled(char *page, char **start, off_t off, int count, int *eof,
	       void *data)
{
	int len = 0;
	struct asus_hotk *hotk = (struct asus_hotk *) data;
	int led_status = 0;
	/*
	 * We use the easy way, we don't care of off and count, so we don't set eof
	 * to 1
	 */
	if (hotk->methods->mled_status) {
		if (read_acpi_int(NULL, hotk->methods->mled_status, 
				  &led_status))
			len =  sprintf(page, "%d\n", led_status);
		else
			printk(KERN_WARNING "Asus ACPI: Error reading MLED "
			       "status\n");
	} else {
		len = sprintf(page, "%d\n", (hotk->status & MLED_ON) ? 1 : 0);
	}

	return len;
}


static int
proc_write_mled(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
	int value;
	int led_out = 0;
	struct asus_hotk *hotk = (struct asus_hotk *) data;



	/* scan expression.  Multiple expressions may be delimited with ; */
	if (sscanf(buffer, "%i", &value) == 1)
		led_out = ~value & 1;

	hotk->status =
	    (value) ? (hotk->status | MLED_ON) : (hotk->status & ~MLED_ON);

	/* We don't have to check mt_mled exists if we are here :) */
	if (!write_acpi_int(hotk->handle, hotk->methods->mt_mled, led_out,
			    NULL))
		printk(KERN_WARNING "Asus ACPI: MLED write failed\n");



	return count;
}

/*
 * We write our info in page, we begin at offset off and cannot write more
 * than count bytes. We set eof to 1 if we handle those 2 values. We return the
 * number of bytes written in page
 */
static int
proc_read_wled(char *page, char **start, off_t off, int count, int *eof,
	       void *data)
{
	int len = 0;
	struct asus_hotk *hotk = (struct asus_hotk *) data;
	int led_status;

	if (hotk->methods->wled_status) {
		if (read_acpi_int(NULL, hotk->methods->wled_status, 
				  &led_status))
			len = sprintf(page, "%d\n", led_status);
		else
			printk(KERN_WARNING "Asus ACPI: Error reading WLED "
			       "status\n");
	} else {
		len = sprintf(page, "%d\n", (hotk->status & WLED_ON) ? 1 : 0);
	}

	return len;
}

static int
proc_write_wled(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
	int value;
	int led_out = 0;
	struct asus_hotk *hotk = (struct asus_hotk *) data;

	/* scan expression.  Multiple expressions may be delimited with ; */
	if (sscanf(buffer, "%i", &value) == 1)
		led_out = value & 1;

	hotk->status =
	    (value) ? (hotk->status | WLED_ON) : (hotk->status & ~WLED_ON);

	/* We don't have to check if mt_wled exists if we are here :) */
	if (!write_acpi_int(hotk->handle, hotk->methods->mt_wled, led_out,
			    NULL))
		printk(KERN_WARNING "Asus ACPI: WLED write failed\n");


	return count;
}


static int get_lcd_state(struct asus_hotk *hotk)
{
	int lcd = 0;

	/* We don't have to check anything, if we are here */
	if (!read_acpi_int(NULL, hotk->methods->lcd_status, &lcd))
		printk(KERN_WARNING "Asus ACPI: Error reading LCD status\n");
	
	if (hotk->model == L2X)
		lcd = ~lcd;
	
	return (lcd & 1);
}


static int
proc_read_lcd(char *page, char **start, off_t off, int count, int *eof,
	      void *data)
{
	return sprintf(page, "%d\n", get_lcd_state((struct asus_hotk *) data));
}


static int
proc_write_lcd(struct file *file, const char *buffer,
	       unsigned long count, void *data)
{
	int value;
	int lcd = 0;
	acpi_status status = 0;
	int lcd_status = 0;
	struct asus_hotk *hotk = (struct asus_hotk *) data;

	/* scan expression.  Multiple expressions may be delimited with ; */
	if (sscanf(buffer, "%i", &value) == 1)
		lcd = value & 1;

	lcd_status = get_lcd_state(hotk);

	if (lcd_status != lcd) {
		/* switch */
		status =
		    acpi_evaluate_object(NULL, hotk->methods->mt_lcd_switch,
					 NULL, NULL);
		if (ACPI_FAILURE(status))
			printk(KERN_WARNING "Asus ACPI: Error switching LCD\n");
	}

	return count;
}


/*
 * Change the brightness level
 */
static void set_brightness(int value, struct asus_hotk *hotk)
{
	acpi_status status = 0;

	/* SPLV laptop */
	if(hotk->methods->brightness_set) {
		if (!write_acpi_int(hotk->handle, hotk->methods->brightness_set, 
				    value, NULL))
			printk(KERN_WARNING "Asus ACPI: Error changing brightness\n");
		return;
	}

	/* No SPLV method if we are here, act as appropriate */
	value -= hotk->brightness;
	while (value != 0) {
		status = acpi_evaluate_object(NULL, (value > 0) ? 
					      hotk->methods->brightness_up : 
					      hotk->methods->brightness_down,
					      NULL, NULL);
		(value > 0) ? value-- : value++;
		if (ACPI_FAILURE(status))
			printk(KERN_WARNING "Asus ACPI: Error changing brightness\n");
	}
	return;
}

static int read_brightness(struct asus_hotk *hotk)
{
	int value;
	
	if(hotk->methods->brightness_get) { /* SPLV/GPLV laptop */
		if (!read_acpi_int(hotk->handle, hotk->methods->brightness_get, 
				   &value))
			printk(KERN_WARNING "Asus ACPI: Error reading brightness\n");
	} else if (hotk->methods->brightness_status) { /* For D1 for example */
		if (!read_acpi_int(NULL, hotk->methods->brightness_status, 
				   &value))
			printk(KERN_WARNING "Asus ACPI: Error reading brightness\n");
	} else /* No GPLV method */
		value = hotk->brightness;
	return value;
}

static int
proc_read_brn(char *page, char **start, off_t off, int count, int *eof,
	      void *data)
{
	struct asus_hotk *hotk = (struct asus_hotk *) data;
	return sprintf(page, "%d\n", read_brightness(hotk));
}

static int
proc_write_brn(struct file *file, const char *buffer,
	       unsigned long count, void *data)
{
	int value;
	struct asus_hotk *hotk = (struct asus_hotk *) data;

	/* scan expression.  Multiple expressions may be delimited with ; */
	if (sscanf(buffer, "%d", &value) == 1) {
		value = (0 < value) ? ((15 < value) ? 15 : value) : 0;
			/* 0 <= value <= 15 */
		set_brightness(value, hotk);
	} else {
		printk(KERN_WARNING "Asus ACPI: Error reading user input\n");
	}

	return count;
}

static void set_display(int value, struct asus_hotk *hotk)
{
	/* no sanity check needed for now */
	if (!write_acpi_int(hotk->handle, hotk->methods->display_set, 
			    value, NULL))
		printk(KERN_WARNING "Asus ACPI: Error setting display\n");
	return;
}

/*
 * Now, *this* one could be more user-friendly, but so far, no-one has 
 * complained. The significance of bits is the same as in proc_write_disp()
 */

static int
proc_read_disp(char *page, char **start, off_t off, int count, int *eof,
	      void *data)
{
	int value = 0;
	struct asus_hotk *hotk = (struct asus_hotk *) data;
	
	if (!read_acpi_int(hotk->handle, hotk->methods->display_get, &value))
		printk(KERN_WARNING "Asus ACPI: Error reading display status\n");
	return sprintf(page, "%d\n", value);
}

/*
 * Experimental support for display switching. As of now: 0x01 should activate 
 * the LCD output, 0x02 should do for CRT, and 0x04 for TV-Out. Any combination 
 * (bitwise) of these will suffice. I never actually tested 3 displays hooked up 
 * simultaneously, so be warned.
 */

static int
proc_write_disp(struct file *file, const char *buffer,
	       unsigned long count, void *data)
{
	int value;
	struct asus_hotk *hotk = (struct asus_hotk *) data;

	/* scan expression.  Multiple expressions may be delimited with ; */
	if (sscanf(buffer, "%d", &value) == 1)
		set_display(value, hotk);
	else {
		printk(KERN_WARNING "Asus ACPI: Error reading user input\n");
	}

	return count;
}

static int __init asus_hotk_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *proc;
	struct asus_hotk *hotk = acpi_driver_data(device);
	mode_t mode;
	
	/*
	 * If parameter uid or gid is not changed, keep the default setting for
	 * our proc entries (-rw-rw-rw-) else, it means we care about security,
	 * and then set to -rw-rw----
	 */

	if ((asus_uid == 0) && (asus_gid == 0)){
		mode = S_IFREG | S_IRUGO | S_IWUGO;
	} else {
		mode = S_IFREG | S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP;
	}

	acpi_device_dir(device) = asus_proc_dir;
	if (!acpi_device_dir(device))
		return(-ENODEV);

	proc = create_proc_entry(PROC_INFOS, mode, acpi_device_dir(device));
	if (proc) {
		proc->read_proc = proc_read_info;
		proc->data = acpi_driver_data(device);
		proc->owner = THIS_MODULE;
		proc->uid = asus_uid;
		proc->gid = asus_gid;;
	} else {
		printk(KERN_WARNING "  Unable to create " PROC_INFOS
		       " fs entry\n");
	}

	if (hotk->methods->mt_wled) {
		proc = create_proc_entry(PROC_WLED, mode, acpi_device_dir(device));
		if (proc) {
			proc->write_proc = proc_write_wled;
			proc->read_proc = proc_read_wled;
			proc->data = acpi_driver_data(device);
			proc->owner = THIS_MODULE;
			proc->uid = asus_uid;
			proc->gid = asus_gid;;
		} else {
			printk(KERN_WARNING "  Unable to create " PROC_WLED
			       " fs entry\n");
		}
	}

	if (hotk->methods->mt_mled) {
		proc = create_proc_entry(PROC_MLED, mode, acpi_device_dir(device));
		if (proc) {
			proc->write_proc = proc_write_mled;
			proc->read_proc = proc_read_mled;
			proc->data = acpi_driver_data(device);
			proc->owner = THIS_MODULE;
			proc->uid = asus_uid;
			proc->gid = asus_gid;;
		} else {
			printk(KERN_WARNING "  Unable to create " PROC_MLED
			       " fs entry\n");
		}
	}

	/* 
	 * We need both read node and write method as LCD switch is also accessible
	 * from keyboard 
	 */
	if (hotk->methods->mt_lcd_switch && hotk->methods->lcd_status) {
		proc = create_proc_entry(PROC_LCD, mode, acpi_device_dir(device));
		if (proc) {
			proc->write_proc = proc_write_lcd;
			proc->read_proc = proc_read_lcd;
			proc->data = acpi_driver_data(device);
			proc->owner = THIS_MODULE;
			proc->uid = asus_uid;
			proc->gid = asus_gid;;
		} else {
			printk(KERN_WARNING "  Unable to create " PROC_LCD
			       " fs entry\n");
		}
	}
	
	if ((hotk->methods->brightness_up && hotk->methods->brightness_down) ||
	    (hotk->methods->brightness_get && hotk->methods->brightness_get)) {
		proc = create_proc_entry(PROC_BRN, mode, acpi_device_dir(device));
		if (proc) {
			proc->write_proc = proc_write_brn;
			proc->read_proc = proc_read_brn;
			proc->data = acpi_driver_data(device);
			proc->owner = THIS_MODULE;
			proc->uid = asus_uid;
			proc->gid = asus_gid;;
		} else {
			printk(KERN_WARNING "  Unable to create " PROC_BRN
			       " fs entry\n");
		}
	}

	if (hotk->methods->display_set) {
		proc = create_proc_entry(PROC_DISP, mode, acpi_device_dir(device));
		if (proc) {
			proc->write_proc = proc_write_disp;
			proc->read_proc = proc_read_disp;
			proc->data = acpi_driver_data(device);
			proc->owner = THIS_MODULE;
			proc->uid = asus_uid;
			proc->gid = asus_gid;;
		} else {
			printk(KERN_WARNING "  Unable to create " PROC_DISP
			       " fs entry\n");
		}
	}

	return 0;
}


static void asus_hotk_notify(acpi_handle handle, u32 event, void *data)
{
	/* TODO Find a better way to handle events count. Here, in data, we receive
	 * the hotk, so we can do anything!
	 */
	struct asus_hotk *hotk = (struct asus_hotk *) data;

	if (!hotk)
		return;

	if ((event & ~((u32) BR_UP)) < 16) {
		hotk->brightness = (event & ~((u32) BR_UP));
	} else if ((event & ~((u32) BR_DOWN)) < 16 ) {
		hotk->brightness = (event & ~((u32) BR_DOWN));
	}

	acpi_bus_generate_event(hotk->device, event,
				hotk->event_count[event % 128]++);

	return;
}

/*
 * This function is used to initialize the hotk with right values. In this
 * method, we can make all the detection we want, and modify the hotk struct
 */
static int __init asus_hotk_get_info(struct asus_hotk *hotk)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer dsdt = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *model = NULL;
	int bsts_result;
	acpi_status status;

	/*
	 * Get DSDT headers early enough to allow for differentiating between 
	 * models, but late enough to allow acpi_bus_register_driver() to fail 
	 * before doing anything ACPI-specific. Should we encounter a machine,
	 * which needs special handling (i.e. its hotkey device has a different
	 * HID), this bit will be moved. A global variable asus_info contains
	 * the DSDT header.
	 */
	status = acpi_get_table(ACPI_TABLE_DSDT, 1, &dsdt);
	if (ACPI_FAILURE(status))
		printk(KERN_WARNING "  Couldn't get the DSDT table header\n");
	else
		asus_info = (struct acpi_table_header *) dsdt.pointer;

	/* We have to write 0 on init this far for all ASUS models */
	if (!write_acpi_int(hotk->handle, "INIT", 0, &buffer)) {
		printk(KERN_ERR "  Hotkey initialization failed\n");
		return -ENODEV;
	}

	/* For testing purposes */
	if (!read_acpi_int(hotk->handle, "BSTS", &bsts_result))
		printk(KERN_WARNING "  Error calling BSTS\n");
	else if (bsts_result)
		printk(KERN_NOTICE "  BSTS called, 0x%02x returned\n", bsts_result);

	/*
	 * Here, we also use asus_info to make decision. For example, on INIT
	 * method, S1X and L1X models both reports to be L84F, but they don't
	 * have the same methods (L1X has WLED, S1X don't)
	 */
	model = (union acpi_object *) buffer.pointer;
	if (model->type == ACPI_TYPE_STRING) {
		printk(KERN_NOTICE "  %s model detected, ", model->string.pointer);
	}

	hotk->model = END_MODEL;
	if (strncmp(model->string.pointer, "L3D", 3) == 0)
		hotk->model = L3D;
		/*
		 * L2B has same settings that L3X, except for GL32, but as
		 * there is no node to get the LCD status, and as GL32 is never
		 * used anywhere else, I assume it's safe, even if lcd get is
		 * broken for this model (TODO fix it ?)
		 */
	else if (strncmp(model->string.pointer, "L3", 2) == 0 ||
		 strncmp(model->string.pointer, "L2B", 3) == 0)
		hotk->model = L3X;
	else if (strncmp(model->string.pointer, "M2", 2) == 0)
		hotk->model = M2X;
	else if (strncmp(model->string.pointer, "M3N", 3) == 0 ||
		 strncmp(model->string.pointer, "S1N", 3) == 0)
		hotk->model = M3N; /* S1300N is similar enough */
	else if (strncmp(model->string.pointer, "L2", 2) == 0)
		hotk->model = L2X;
	else if (strncmp(model->string.pointer, "L8", 2) == 0) {
		/* S1300A reports L84F, but L1400B too */
		if (asus_info) {
			if (strncmp(asus_info->oem_table_id, "L1", 2) == 0)
				hotk->model = L1X;
		} else
			hotk->model = S1X;
	}
	else if (strncmp(model->string.pointer, "D1", 2) == 0)
		hotk->model = D1X;
	else if (strncmp(model->string.pointer, "A1", 2) == 0)
		hotk->model = A1X;
	else if (strncmp(model->string.pointer, "A2", 2) == 0)
		hotk->model = A2X;
	else if (strncmp(model->string.pointer, "J1", 2) == 0)
		hotk->model = S2X;
	else if (strncmp(model->string.pointer, "L5", 2) == 0)
		hotk->model = L5X;

	if (hotk->model == END_MODEL) {
		/* By default use the same values, as I don't know others */
		printk("unsupported, trying default values, supply the "
		       "developers with your DSDT\n");
		hotk->model = L2X;
	} else {
		printk("supported\n");
	}

	hotk->methods = &model_conf[hotk->model];

	acpi_os_free(model);

	return AE_OK;
}



static int __init asus_hotk_check(struct asus_hotk *hotk)
{
	int result = 0;

	if (!hotk)
		return(-EINVAL);

	result = acpi_bus_get_status(hotk->device);
	if (result)
		return(result);

	if (hotk->device->status.present) {
		result = asus_hotk_get_info(hotk);
	} else {
		printk(KERN_ERR "  Hotkey device not present, aborting\n");
		return(-EINVAL);
	}

	return(result);
}



static int __init asus_hotk_add(struct acpi_device *device)
{
	struct asus_hotk *hotk = NULL;
	acpi_status status = AE_OK;
	int result;

	if (!device)
		return(-EINVAL);

	printk(KERN_NOTICE "Asus Laptop ACPI Extras version %s\n",
	       ASUS_ACPI_VERSION);

	hotk =
	    (struct asus_hotk *) kmalloc(sizeof(struct asus_hotk), GFP_KERNEL);
	if (!hotk)
		return(-ENOMEM);
	memset(hotk, 0, sizeof(struct asus_hotk));

	hotk->handle = device->handle;
	sprintf(acpi_device_name(device), "%s", ACPI_HOTK_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_HOTK_CLASS);
	acpi_driver_data(device) = hotk;
	hotk->device = device;


	result = asus_hotk_check(hotk);
	if (result)
		goto end;

	result = asus_hotk_add_fs(device);
	if (result)
		goto end;

	/*
	 * We install the handler, it will receive the hotk in parameter, so, we
	 * could add other data to the hotk struct
	 */
	status = acpi_install_notify_handler(hotk->handle, ACPI_SYSTEM_NOTIFY,
					     asus_hotk_notify, hotk);
	if (ACPI_FAILURE(status))
		printk(KERN_ERR "  Error installing notify handler\n");

	/* For laptops without GPLV: init the hotk->brightness value */
	if ((!hotk->methods->brightness_get) && (!hotk->methods->brightness_status) &&
	    (hotk->methods->brightness_up && hotk->methods->brightness_down)) {
		status = acpi_evaluate_object(NULL, hotk->methods->brightness_down,
					      NULL, NULL);
		if (ACPI_FAILURE(status))
			printk(KERN_WARNING "  Error changing brightness\n");
		else {
			status = acpi_evaluate_object(NULL, hotk->methods->brightness_up,
						      NULL, NULL);
			if (ACPI_FAILURE(status))
				printk(KERN_WARNING "  Strange, error changing" 
				       " brightness\n");
		}
	}

      end:
	if (result) {
		kfree(hotk);
	}

	return(result);
}




static int asus_hotk_remove(struct acpi_device *device, int type)
{
	acpi_status status = 0;
	struct asus_hotk *hotk = NULL;

	if (!device || !acpi_driver_data(device))
		return(-EINVAL);

	hotk = (struct asus_hotk *) acpi_driver_data(device);

	status = acpi_remove_notify_handler(hotk->handle, ACPI_SYSTEM_NOTIFY,
					    asus_hotk_notify);
	if (ACPI_FAILURE(status))
		printk(KERN_ERR "Asus ACPI: Error removing notify handler\n");

	kfree(hotk);

	return(0);
}




static int __init asus_acpi_init(void)
{
	int result;

	asus_proc_dir = proc_mkdir(PROC_ASUS, acpi_root_dir);
	if (!asus_proc_dir) {
		printk(KERN_ERR "Asus ACPI: Unable to create /proc entry");
		return(-ENODEV);
	}
	asus_proc_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&asus_hotk_driver);
	if (result < 0) {
		remove_proc_entry(PROC_ASUS, acpi_root_dir);
		return(-ENODEV);
	}

	return(0);
}



static void __exit asus_acpi_exit(void)
{
	acpi_bus_unregister_driver(&asus_hotk_driver);
	remove_proc_entry(PROC_ASUS, acpi_root_dir);

	acpi_os_free(asus_info);

	return;
}

module_init(asus_acpi_init);
module_exit(asus_acpi_exit);
