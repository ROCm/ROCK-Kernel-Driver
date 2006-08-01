/*
 *  Panasonic HotKey and lcd brightness control Extra driver
 *  (C) 2004 Hiroshi Miura <miura@da-cha.org>
 *  (C) 2004 NTT DATA Intellilink Co. http://www.intellilink.co.jp/
 *  (C) 2005 Timo Hoenig <thoenig@nouse.net>
 *  (C) 2006 Stefan Seyfried <seife@suse.de>
 *
 *  derived from toshiba_acpi.c, Copyright (C) 2002-2004 John Belmonte
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publicshed by the Free Software Foundation.
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
 */

#define ACPI_PCC_VERSION	"0.8.3"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/seq_file.h>
#include <linux/input.h>

#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

MODULE_AUTHOR("Hiroshi Miura");
MODULE_DESCRIPTION("ACPI driver for Panasonic Lets Note laptops");
MODULE_LICENSE("GPL");

/* Defines */
#define ACPI_HOTKEY_COMPONENT	0x10000000
#define _COMPONENT		ACPI_HOTKEY_COMPONENT
#define HKEY_NOTIFY		0x80
#define PROC_PCC		"panasonic"

#define PCC_LOG    "pcc_acpi: "
#define PCC_ERR    KERN_ERR    PCC_LOG
#define PCC_INFO   KERN_INFO   PCC_LOG

/* This is transitional definition */
#ifndef KEY_BATT
# define KEY_BATT 227
#endif

#define PROC_STR_MAX_LEN  8

/* LCD_TYPEs: 0 = Normal, 1 = Semi-transparent
 * ENV_STATEs: Normal temp=0x01, High temp=0x81, N/A=0x00
 *
 */
enum SINF_BITS { SINF_NUM_BATTERIES = 0,
                 SINF_LCD_TYPE, SINF_AC_MAX_BRIGHT,
		 SINF_AC_MIN_BRIGHT, SINF_AC_CUR_BRIGHT, SINF_DC_MAX_BRIGHT,
		 SINF_DC_MIN_BRIGHT, SINF_DC_CUR_BRIGHT, SINF_MUTE,
		 SINF_RESERVED,      SINF_ENV_STATE,
		 SINF_STICKY_KEY = 0x80,
};


static int __devinit acpi_pcc_hotkey_add (struct acpi_device *device);
static int __devexit acpi_pcc_hotkey_remove (struct acpi_device *device,
						int type);
static int acpi_pcc_hotkey_resume(struct acpi_device *device, int state);

static struct acpi_driver acpi_pcc_driver = {
	.name =		"Panasonic PCC extra driver",
	.class =	"pcc",
	.ids =		"MAT0012,MAT0013,MAT0018,MAT0019",
	.ops =		{
				.add =    acpi_pcc_hotkey_add,
				.remove = __devexit_p(acpi_pcc_hotkey_remove),
				.resume = acpi_pcc_hotkey_resume,
			},
};

struct acpi_hotkey {
	acpi_handle		handle;
	struct acpi_device	*device;
	struct proc_dir_entry   *proc_dir_entry;
	unsigned long		num_sifr;
	unsigned long		status;
	struct input_dev	*input_dev;
	int			sticky_mode;
};

struct pcc_keyinput {
	struct acpi_hotkey *hotkey;
	int key_mode;
};

/* method access functions */
static int acpi_pcc_write_sset(struct acpi_hotkey *hotkey, int func, int val)
{
	acpi_status status;
	union acpi_object in_objs[] = {
		{ .integer.type  = ACPI_TYPE_INTEGER,
		  .integer.value = func, },
		{ .integer.type  = ACPI_TYPE_INTEGER,
		  .integer.value = val, },
	};
	struct acpi_object_list params = {
		.count   = ARRAY_SIZE(in_objs),
		.pointer = in_objs,
	};

	status = acpi_evaluate_object(hotkey->handle, "SSET", &params, NULL);

	if (status != AE_OK) {
		return -1;
	}

	return 0;
}

static inline int acpi_pcc_get_sqty(struct acpi_device *device)
{
	acpi_status status;
	unsigned long s;

	status = acpi_evaluate_integer(device->handle, "SQTY", NULL, &s);

	if (ACPI_SUCCESS(status)) {
		return(s);
	}
	else {
		printk(PCC_ERR "acpi_pcc_get_sqty() evaluation error "
			       "HKEY.SQTY\n");
		return(-EINVAL);
	}
}

static int acpi_pcc_retrieve_biosdata(struct acpi_hotkey *hotkey, u32* sinf)
{
	acpi_status status;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *hkey = NULL;
	int i;

	status = acpi_evaluate_object(hotkey->handle, "SINF", 0 , &buffer);
	if (ACPI_FAILURE(status)) {
		printk(PCC_ERR "acpi_pcc_retrieve_biosdata() evaluation error "
			       "HKEY.SINF\n");
		return 0;
	}

	hkey = buffer.pointer;
	if (!hkey || (hkey->type != ACPI_TYPE_PACKAGE)) {
		printk(PCC_ERR "acpi_pcc_retrieve_biosdata() invalid "
			       "HKEY.SINF\n");
		goto end;
	}

	if (hotkey->num_sifr < hkey->package.count) {
		printk(PCC_ERR "acpi_pcc_retrieve_biosdata() SQTY reports bad "
			       "SINF length\n");
		status = AE_ERROR;
		goto end;
	}

	for (i = 0; i < hkey->package.count; i++) {
		union acpi_object *element = &(hkey->package.elements[i]);
		if (likely(element->type == ACPI_TYPE_INTEGER)) {
			sinf[i] = element->integer.value;
		}
		else {
			printk(PCC_ERR "acpi_pcc_retrieve_biosdata() invalid "
				       "HKEY.SINF data");
		}
	}
	sinf[hkey->package.count] = -1;

end:
	kfree(buffer.pointer);

	if (status != AE_OK) {
		return 0;
	}

	return 1;
}

static int acpi_pcc_read_sinf_field(struct seq_file *seq, int field)
{
	struct acpi_hotkey *hotkey = (struct acpi_hotkey *) seq->private;
	u32* sinf = kmalloc(sizeof(u32) * (hotkey->num_sifr + 1), GFP_KERNEL);

	if (!sinf) {
		printk(PCC_ERR "acpi_pcc_read_sinf_field() could not allocate "
			       "%li bytes\n", sizeof(u32) * hotkey->num_sifr);
		return 0;
	}

	if (acpi_pcc_retrieve_biosdata(hotkey, sinf)) {
		seq_printf(seq, "%u\n",	sinf[field]);
	}
	else {
		seq_printf(seq, "error");
		printk(PCC_ERR "acpi_pcc_read_sinf_field() could not retrieve "
			       "BIOS data\n");
	}

	kfree(sinf);
	return 0;
}

/* user interface functions
 *   - read methods
 *   - SINF read methods
 *
 */

#define PCC_SINF_READ_F(_name_, FUNC) \
static int _name_ (struct seq_file *seq, void *offset) \
{ \
	return acpi_pcc_read_sinf_field(seq, (FUNC)); \
}

PCC_SINF_READ_F(acpi_pcc_numbatteries_show,	 SINF_NUM_BATTERIES);
PCC_SINF_READ_F(acpi_pcc_lcdtype_show,		 SINF_LCD_TYPE);
PCC_SINF_READ_F(acpi_pcc_ac_brightness_max_show, SINF_AC_MAX_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_ac_brightness_min_show, SINF_AC_MIN_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_ac_brightness_show,	 SINF_AC_CUR_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_dc_brightness_max_show, SINF_DC_MAX_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_dc_brightness_min_show, SINF_DC_MIN_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_dc_brightness_show,	 SINF_DC_CUR_BRIGHT);
PCC_SINF_READ_F(acpi_pcc_mute_show,		 SINF_MUTE);

static int acpi_pcc_sticky_key_show(struct seq_file *seq, void *offset)
{
	struct acpi_hotkey *hotkey = seq->private;

	if (!hotkey || !hotkey->device) {
		return 0;
	}

	seq_printf(seq, "%d\n", hotkey->sticky_mode);

	return 0;
}

static int acpi_pcc_keyinput_show(struct seq_file *seq, void *offset)
{
	struct acpi_hotkey *hotkey = (struct acpi_hotkey *) seq->private;
	struct input_dev *hotk_input_dev = hotkey->input_dev;
	struct pcc_keyinput *keyinput = hotk_input_dev->private;

	seq_printf(seq, "%d\n", keyinput->key_mode);

	return 0;
}

static int acpi_pcc_version_show(struct seq_file *seq, void *offset)
{
	struct acpi_hotkey *hotkey = (struct acpi_hotkey *) seq->private;

	if (!hotkey || !hotkey->device) {
		return 0;
	}

	seq_printf(seq, "%s version %s\n", "Panasonic PCC extra driver",
			ACPI_PCC_VERSION);
	seq_printf(seq, "%li functions\n", hotkey->num_sifr);

	return 0;
}

/* write methods */
static ssize_t acpi_pcc_write_single_flag (struct file *file,
                                           const char __user *buffer,
                                           size_t count,
                                           int sinf_func)
{
	struct seq_file	*seq = file->private_data;
	struct acpi_hotkey *hotkey = seq->private;
	char write_string[PROC_STR_MAX_LEN];
	u32 val;

	if (!hotkey || (count > sizeof(write_string) - 1)) {
		return -EINVAL;
        }

	if (copy_from_user(write_string, buffer, count)) {
		return -EFAULT;
        }
	write_string[count] = '\0';

	if (sscanf(write_string, "%i", &val) == 1 && (val == 0 || val == 1)) {
		acpi_pcc_write_sset(hotkey, sinf_func, val);
	}

	return count;
}

static unsigned long acpi_pcc_write_brightness(struct file *file,
					       const char __user *buffer,
					       size_t count,
					       int min_index, int max_index,
					       int cur_index)
{
	struct seq_file	*seq = (struct seq_file *)file->private_data;
	struct acpi_hotkey *hotkey = (struct acpi_hotkey *)seq->private;
	char write_string[PROC_STR_MAX_LEN];
	u32 bright;
	u32* sinf = kmalloc(sizeof(u32) * (hotkey->num_sifr + 1), GFP_KERNEL);

	if (!hotkey || (count > sizeof(write_string) - 1)) {
		return -EINVAL;
	}

	if (!sinf) {
		printk(PCC_ERR "acpi_pcc_write_brightness() could not "
			       "allocate %li bytes\n",
				sizeof(u32) * hotkey->num_sifr);
		return -EFAULT;
	}

	if (copy_from_user(write_string, buffer, count)) {
		return -EFAULT;
	}

	write_string[count] = '\0';

	if (!acpi_pcc_retrieve_biosdata(hotkey, sinf)) {
		printk(PCC_ERR "acpi_pcc_write_brightness() could not "
			       "retrieve BIOS data\n");
		goto end;
	}

	if ((sscanf(write_string, "%i", &bright) == 1) &&
			(bright >= sinf[min_index]) &&
			(bright <= sinf[max_index])) {
		acpi_pcc_write_sset(hotkey, cur_index, bright);
	}

end:
	kfree(sinf);
	return count;
}

static ssize_t acpi_pcc_write_ac_brightness(struct file *file,
					    const char __user *buffer,
					    size_t count, loff_t *ppos)
{
	return acpi_pcc_write_brightness(file, buffer, count,
					 SINF_AC_MIN_BRIGHT,
					 SINF_AC_MAX_BRIGHT,
					 SINF_AC_CUR_BRIGHT);
}

static ssize_t acpi_pcc_write_dc_brightness(struct file *file,
					    const char __user *buffer,
					    size_t count, loff_t *ppos)
{
	return acpi_pcc_write_brightness(file, buffer, count,
					 SINF_DC_MIN_BRIGHT,
					 SINF_DC_MAX_BRIGHT,
					 SINF_DC_CUR_BRIGHT);
}

static ssize_t acpi_pcc_write_mute (struct file *file,
				    const char __user *buffer,
				    size_t count, loff_t *ppos)
{
	return acpi_pcc_write_single_flag(file, buffer, count, SINF_MUTE);
}

static ssize_t acpi_pcc_write_sticky_key (struct file *file,
					  const char __user *buffer,
					  size_t count, loff_t *ppos)
{
	return acpi_pcc_write_single_flag(file, buffer, count,
					  SINF_STICKY_KEY);
}

static ssize_t acpi_pcc_write_keyinput(struct file *file,
				       const char __user *buffer,
				       size_t count, loff_t *ppos)
{
	struct seq_file	*seq = (struct seq_file *)file->private_data;
	struct acpi_hotkey *hotkey = (struct acpi_hotkey *)seq->private;
	struct pcc_keyinput *keyinput;
	char write_string[PROC_STR_MAX_LEN];
	int key_mode;

	if (!hotkey || (count > sizeof(write_string) - 1)) {
		return -EINVAL;
	}

	if (copy_from_user(write_string, buffer, count)) {
		return -EFAULT;
	}

	write_string[count] = '\0';

	if ((sscanf(write_string, "%i", &key_mode) == 1) &&
	    (key_mode == 0 || key_mode == 1)) {
		keyinput = (struct pcc_keyinput *) hotkey->input_dev->private;
		keyinput->key_mode = key_mode;
	}

	return count;
}

/* hotkey driver */
static void acpi_pcc_generete_keyinput(struct acpi_hotkey *hotkey)
{
	struct input_dev *hotk_input_dev = hotkey->input_dev;
	struct pcc_keyinput *keyinput = hotk_input_dev->private;
	int hinf = hotkey->status;
	int key_code, hkey_num;
	const int key_map[] = {
		/*  0 */ -1,
		/*  1 */ KEY_BRIGHTNESSDOWN,
		/*  2 */ KEY_BRIGHTNESSUP,
		/*  3 */ -1, /* vga/lcd switch event does not occur on
			      * hotkey driver.
			      */
		/*  4 */ KEY_MUTE,
		/*  5 */ KEY_VOLUMEDOWN,
		/*  6 */ KEY_VOLUMEUP,
		/*  7 */ KEY_SLEEP,
		/*  8 */ -1, /* Change CPU boost: do nothing */
		/*  9 */ KEY_BATT,
		/* 10 */ KEY_SUSPEND,
	};

	if (keyinput->key_mode == 0) {
		return;
	}

	hkey_num = hinf & 0xf;

	if ((0 > hkey_num) || (hkey_num > ARRAY_SIZE(key_map))) {
		printk(PCC_ERR "acpi_pcc_generete_keyinput() hotkey "
			       "number (%d) out of range\n", hkey_num);
		return;
	}

	key_code = key_map[hkey_num];

	if (key_code != -1) {
		int pushed = (hinf & 0x80) ? TRUE : FALSE;

		input_report_key(hotk_input_dev, key_code, pushed);
		input_sync(hotk_input_dev);
	}

	return;
}

static int acpi_pcc_hotkey_get_key(struct acpi_hotkey *hotkey)
{
	acpi_status status;
	unsigned long result;

	status = acpi_evaluate_integer(hotkey->handle, "HINF", NULL, &result);
	if (likely(ACPI_SUCCESS(status))) {
		hotkey->status = result;
	}
	else {
		printk(PCC_ERR "acpi_pcc_hotkey_get_key() error getting "
			       "hotkey status\n");
	}

	if (status != AE_OK) {
		return -1;
	}

	return 0;
}

void acpi_pcc_hotkey_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_hotkey *hotkey = (struct acpi_hotkey *) data;

	switch(event) {
	case HKEY_NOTIFY:
		if (acpi_pcc_hotkey_get_key(hotkey)) {
			/* generate event
			 *   e.g. '"pcc HKEY 00000080 00000084"' when Fn+F4 is
			 *   pressed
			 *
			 */

			acpi_bus_generate_event(hotkey->device, event,
					hotkey->status);
		}

		acpi_pcc_generete_keyinput(hotkey);
		break;

	default:
		/* nothing to do */
		break;

	}

	return;
}

/* proc interface  */
#define SEQ_OPEN_FS(_open_func_name_, _show_func_name_) \
    static int _open_func_name_(struct inode *inode, struct file *file) \
{ \
            return single_open(file, _show_func_name_, PDE(inode)->data); \
}

SEQ_OPEN_FS(acpi_pcc_dc_brightness_open_fs,
	    acpi_pcc_dc_brightness_show);
SEQ_OPEN_FS(acpi_pcc_numbatteries_open_fs,
	    acpi_pcc_numbatteries_show);
SEQ_OPEN_FS(acpi_pcc_lcdtype_open_fs,
	    acpi_pcc_lcdtype_show);
SEQ_OPEN_FS(acpi_pcc_ac_brightness_max_open_fs,
    	    acpi_pcc_ac_brightness_max_show);
SEQ_OPEN_FS(acpi_pcc_ac_brightness_min_open_fs,
	    acpi_pcc_ac_brightness_min_show);
SEQ_OPEN_FS(acpi_pcc_ac_brightness_open_fs,
	    acpi_pcc_ac_brightness_show);
SEQ_OPEN_FS(acpi_pcc_dc_brightness_max_open_fs,
	    acpi_pcc_dc_brightness_max_show);
SEQ_OPEN_FS(acpi_pcc_dc_brightness_min_open_fs,
	    acpi_pcc_dc_brightness_min_show);
SEQ_OPEN_FS(acpi_pcc_mute_open_fs,
	    acpi_pcc_mute_show);
SEQ_OPEN_FS(acpi_pcc_version_open_fs,
	    acpi_pcc_version_show);
SEQ_OPEN_FS(acpi_pcc_keyinput_open_fs,
	    acpi_pcc_keyinput_show);
SEQ_OPEN_FS(acpi_pcc_sticky_key_open_fs,
            acpi_pcc_sticky_key_show);

#define SEQ_FILEOPS_R(_open_func_name_) \
{ \
            .open    = _open_func_name_, \
            .read    = seq_read,         \
            .llseek  = seq_lseek,        \
            .release = single_release,   \
}

#define SEQ_FILEOPS_RW(_open_func_name_, _write_func_name_) \
{ \
            .open    = _open_func_name_ , \
            .read    = seq_read,          \
            .write   = _write_func_name_, \
            .llseek  = seq_lseek,         \
            .release = single_release,    \
}

typedef struct file_operations fops_t;

/* number of batteries */
static fops_t acpi_pcc_numbatteries_fops = \
	SEQ_FILEOPS_R (acpi_pcc_numbatteries_open_fs);

/* type of lcd */
static fops_t acpi_pcc_lcdtype_fops = \
	SEQ_FILEOPS_R (acpi_pcc_lcdtype_open_fs);

/* mute */
static fops_t acpi_pcc_mute_fops = \
	SEQ_FILEOPS_RW(acpi_pcc_mute_open_fs,
		       acpi_pcc_write_mute);

/* brightness */
static fops_t acpi_pcc_ac_brightness_fops = \
	SEQ_FILEOPS_RW(acpi_pcc_ac_brightness_open_fs,
		       acpi_pcc_write_ac_brightness);
static fops_t acpi_pcc_ac_brightness_max_fops = \
	SEQ_FILEOPS_R(acpi_pcc_ac_brightness_max_open_fs);
static fops_t acpi_pcc_ac_brightness_min_fops = \
	SEQ_FILEOPS_R(acpi_pcc_ac_brightness_min_open_fs);
static fops_t acpi_pcc_dc_brightness_fops = \
	SEQ_FILEOPS_RW(acpi_pcc_dc_brightness_open_fs,
		       acpi_pcc_write_dc_brightness);
static fops_t acpi_pcc_dc_brightness_max_fops = \
	SEQ_FILEOPS_R(acpi_pcc_dc_brightness_max_open_fs);
static fops_t acpi_pcc_dc_brightness_min_fops = \
	SEQ_FILEOPS_R(acpi_pcc_dc_brightness_min_open_fs);

/* sticky key */
static fops_t acpi_pcc_sticky_key_fops = \
	SEQ_FILEOPS_RW(acpi_pcc_sticky_key_open_fs,
		       acpi_pcc_write_sticky_key);

/* keyinput */
static fops_t acpi_pcc_keyinput_fops = \
	SEQ_FILEOPS_RW(acpi_pcc_keyinput_open_fs,
	   	       acpi_pcc_write_keyinput);

/* version */
static fops_t acpi_pcc_version_fops = \
	SEQ_FILEOPS_R (acpi_pcc_version_open_fs);

typedef struct _ProcItem
{
	const char* name;
	struct file_operations *fops;
	mode_t flag;
} ProcItem;

/* Note: These functions map *exactly* to the SINF/SSET functions */
ProcItem pcc_proc_items_sifr[] =
{
	{ "num_batteries",      &acpi_pcc_numbatteries_fops,     S_IRUGO },
	{ "lcd_type",           &acpi_pcc_lcdtype_fops,          S_IRUGO },
	{ "ac_brightness_max" , &acpi_pcc_ac_brightness_max_fops,S_IRUGO },
	{ "ac_brightness_min" , &acpi_pcc_ac_brightness_min_fops,S_IRUGO },
	{ "ac_brightness" ,     &acpi_pcc_ac_brightness_fops,    S_IFREG |
								 S_IRUGO |
								 S_IWUSR },
	{ "dc_brightness_max" , &acpi_pcc_dc_brightness_max_fops,S_IRUGO },
	{ "dc_brightness_min" , &acpi_pcc_dc_brightness_min_fops,S_IRUGO },
	{ "dc_brightness" ,     &acpi_pcc_dc_brightness_fops,    S_IFREG |
								 S_IRUGO |
								 S_IWUSR },
	{ "mute",               &acpi_pcc_mute_fops,             S_IFREG |
								 S_IRUGO |
								 S_IWUSR },
	{ NULL, NULL, 0 },
};

ProcItem pcc_proc_items[] =
{
	{ "sticky_key",		&acpi_pcc_sticky_key_fops,	 S_IFREG |
								 S_IRUGO |
								 S_IWUSR },
	{ "keyinput",           &acpi_pcc_keyinput_fops,         S_IFREG |
								 S_IRUGO |
							 	 S_IWUSR },
	{ "version",            &acpi_pcc_version_fops,          S_IRUGO },
	{ NULL, NULL, 0 },
};

static int __init acpi_pcc_add_device(struct acpi_device *device,
                                      ProcItem *proc_items,
                                      int num)
{
	struct acpi_hotkey *hotkey = \
		(struct acpi_hotkey*)acpi_driver_data(device);
	struct proc_dir_entry* proc;
	ProcItem* item;
	int i;


	for (item = proc_items, i = 0; item->name && i < num; ++item, ++i) {
		proc = create_proc_entry(item->name, item->flag,
					 hotkey->proc_dir_entry);
		if (likely(proc)) {
			proc->proc_fops = item->fops;
			proc->data = hotkey;
			proc->owner = THIS_MODULE;
		}
		else {
			while (i-- > 0) {
				item--;
				remove_proc_entry(item->name,
						  hotkey->proc_dir_entry);
			}
			return -ENODEV;
		}
	}

	return 0;
}

static int __init acpi_pcc_proc_init(struct acpi_device *device)
{
	acpi_status status;
	struct acpi_hotkey *hotkey = \
		(struct acpi_hotkey*)acpi_driver_data(device);
	struct proc_dir_entry* acpi_pcc_dir;

	acpi_pcc_dir = proc_mkdir(PROC_PCC, acpi_root_dir);

	if (unlikely(!acpi_pcc_dir)) {
		printk(PCC_ERR "acpi_pcc_proc_init() could not create proc "
			       "entry\n");
		return -ENODEV;
	}

	acpi_pcc_dir->owner = THIS_MODULE;
	hotkey->proc_dir_entry = acpi_pcc_dir;

	status = acpi_pcc_add_device(device, pcc_proc_items_sifr,
				     hotkey->num_sifr);
	status |= acpi_pcc_add_device(device, pcc_proc_items,
			              sizeof(pcc_proc_items)/sizeof(ProcItem));

	if (unlikely(status)) {
		remove_proc_entry(PROC_PCC, acpi_root_dir);
		hotkey->proc_dir_entry = NULL;
		return -ENODEV;
	}

	return status;
}

static void __exit acpi_pcc_remove_device(struct acpi_device *device,
                                          ProcItem *proc_items,
                                          int num)
{
	struct acpi_hotkey *hotkey =
		(struct acpi_hotkey*)acpi_driver_data(device);
	ProcItem* item;
	int i;

	for (item = proc_items, i = 0; item->name != NULL &&
				        i < num; ++item, ++i) {
		remove_proc_entry(item->name, hotkey->proc_dir_entry);
	}

	return;
}

/* input init */
static int hotk_input_open(struct input_dev *dev)
{
	return 0;
}

static void hotk_input_close(struct input_dev *dev)
{
	return;
}

static int acpi_pcc_init_input(struct acpi_hotkey *hotkey)
{
	struct input_dev *hotk_input_dev;
	struct pcc_keyinput *pcc_keyinput;

	hotk_input_dev = input_allocate_device();

	if (!hotk_input_dev) {
		printk(PCC_ERR "acpi_pcc_init_input() could not allocate "
			       "memory\n");
		return -ENOMEM;
	}

	pcc_keyinput = kmalloc(sizeof(struct pcc_keyinput),GFP_KERNEL);

	if (!pcc_keyinput) {
		printk(PCC_ERR "acpi_pcc_init_input() could not allocate "
			       "memory\n");
		return -ENOMEM;
	}

	hotk_input_dev->open = hotk_input_open;
	hotk_input_dev->close = hotk_input_close;

	hotk_input_dev->evbit[0] = BIT(EV_KEY);

	set_bit(KEY_BRIGHTNESSDOWN, hotk_input_dev->keybit);
	set_bit(KEY_BRIGHTNESSUP, hotk_input_dev->keybit);
	set_bit(KEY_MUTE, hotk_input_dev->keybit);
	set_bit(KEY_VOLUMEDOWN, hotk_input_dev->keybit);
	set_bit(KEY_VOLUMEUP, hotk_input_dev->keybit);
	set_bit(KEY_SLEEP, hotk_input_dev->keybit);
	set_bit(KEY_BATT, hotk_input_dev->keybit);
	set_bit(KEY_SUSPEND, hotk_input_dev->keybit);

	hotk_input_dev->name = "Panasonic PCC extra driver";
	hotk_input_dev->phys = "panasonic/hkey0";
	hotk_input_dev->id.bustype = 0x1a; /* XXX FIXME: BUS_I8042? */
	hotk_input_dev->id.vendor = 0x0001;
	hotk_input_dev->id.product = 0x0001;
	hotk_input_dev->id.version = 0x0100;

	pcc_keyinput->key_mode = 1; /* default on */
	pcc_keyinput->hotkey = hotkey;

	hotk_input_dev->private = pcc_keyinput;

	hotkey->input_dev = hotk_input_dev;

	input_register_device(hotk_input_dev);

	return 0;
}

/* module init */
static int acpi_pcc_hotkey_add (struct acpi_device *device)
{
	acpi_status status;
	struct acpi_hotkey *hotkey = NULL;
	int num_sifr, result;

	if (!device) {
		return -EINVAL;
	}

	num_sifr = acpi_pcc_get_sqty(device);

	if (num_sifr > 255) {
		printk(PCC_ERR "acpi_pcc_hotkey_add() num_sifr too large "
			       "(%i)\n", num_sifr);
		return -ENODEV;
	}

	hotkey = kmalloc(sizeof(struct acpi_hotkey), GFP_KERNEL);

	if (!hotkey) {
		printk(PCC_ERR "acpi_pcc_hotkey_add() could not allocate "
			       "memory\n");
		return -ENOMEM;
	}

	memset(hotkey, 0, sizeof(struct acpi_hotkey));

	hotkey->device = device;
	hotkey->handle = device->handle;
	hotkey->num_sifr = num_sifr;
	acpi_driver_data(device) = hotkey;
	strcpy(acpi_device_name(device), "Panasonic PCC");
	strcpy(acpi_device_class(device), "pcc");

	status = acpi_install_notify_handler (
			hotkey->handle,
			ACPI_DEVICE_NOTIFY,
			acpi_pcc_hotkey_notify,
			hotkey);

	if (ACPI_FAILURE(status)) {
		printk(PCC_ERR "acpi_pcc_hotkey_add() error installing notify "
			       "handler\n");
		kfree(hotkey);
		return -ENODEV;
	}

	result = acpi_pcc_init_input(hotkey);

	if (result) {
		printk(PCC_ERR "acpi_pcc_hotkey_add() error installing input "
			       "handler\n");
		kfree(hotkey);
		return result;
	}

	return acpi_pcc_proc_init(device);
}

static int acpi_pcc_hotkey_remove(struct acpi_device *device, int type)
{
	acpi_status status;
	struct acpi_hotkey *hotkey = acpi_driver_data(device);

	if (!device || !hotkey) {
		return -EINVAL;
	}

	if (hotkey->proc_dir_entry) {
		acpi_pcc_remove_device(device, pcc_proc_items_sifr,
				       hotkey->num_sifr);
		acpi_pcc_remove_device(device, pcc_proc_items,
				       sizeof(pcc_proc_items)/sizeof(ProcItem));
		remove_proc_entry(PROC_PCC, acpi_root_dir);
	}

	status = acpi_remove_notify_handler(hotkey->handle,
		    ACPI_DEVICE_NOTIFY, acpi_pcc_hotkey_notify);

	if (ACPI_FAILURE(status)) {
		printk(PCC_ERR "acpi_pcc_hotkey_remove() error removing "
			       "notify handler\n");
	}

	input_unregister_device(hotkey->input_dev);

	kfree(hotkey);

	if(status != AE_OK) {
		return -1;
	}

	return 0;
}

static int acpi_pcc_hotkey_resume(struct acpi_device *device, int state)
{
	acpi_status status;
	struct acpi_hotkey *hotkey = acpi_driver_data(device);

	if (device == NULL || hotkey == NULL) {
		return -EINVAL;
	}

	printk(PCC_INFO "acpi_pcc_hotkey_resume() sticky mode restore: %d\n",
			hotkey->sticky_mode);

	status = acpi_pcc_write_sset(hotkey, SINF_STICKY_KEY,
				     hotkey->sticky_mode);

	if (status != AE_OK) {
		return -EINVAL;
	}

	return 0;
}

static int __init acpi_pcc_init(void)
{
	int result = 0;

	if (acpi_disabled) {
		return -ENODEV;
	}

	result = acpi_bus_register_driver(&acpi_pcc_driver);
	if (result < 0) {
		printk(PCC_ERR "error registering hotkey driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit acpi_pcc_exit(void)
{
	acpi_bus_unregister_driver(&acpi_pcc_driver);

	return;
}

module_init(acpi_pcc_init);
module_exit(acpi_pcc_exit);
