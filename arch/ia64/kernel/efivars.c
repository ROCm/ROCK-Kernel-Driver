/*
 * EFI Variables - efivars.c
 *
 * Copyright (C) 2001 Dell Computer Corporation <Matt_Domsch@dell.com>
 *
 * This code takes all variables accessible from EFI runtime and
 *  exports them via /proc
 *
 * Reads to /proc/efi/vars/varname return an efi_variable_t structure.
 * Writes to /proc/efi/vars/varname must be an efi_variable_t structure.
 * Writes with DataSize = 0 or Attributes = 0 deletes the variable.
 * Writes with a new value in VariableName+VendorGuid creates
 * a new variable.
 *
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
 * Changelog:
 *
 *  20 April 2001 - Matt Domsch <Matt_Domsch@dell.com>
 *   Moved vars from /proc/efi to /proc/efi/vars, and made
 *   efi.c own the /proc/efi directory.
 *   v0.03 release to linux-ia64@linuxia64.org
 *
 *  26 March 2001 - Matt Domsch <Matt_Domsch@dell.com>
 *   At the request of Stephane, moved ownership of /proc/efi
 *   to efi.c, and now efivars lives under /proc/efi/vars.
 *
 *  12 March 2001 - Matt Domsch <Matt_Domsch@dell.com>
 *   Feedback received from Stephane Eranian incorporated.
 *   efivar_write() checks copy_from_user() return value.
 *   efivar_read/write() returns proper errno.
 *   v0.02 release to linux-ia64@linuxia64.org
 *
 *  26 February 2001 - Matt Domsch <Matt_Domsch@dell.com>
 *   v0.01 release to linux-ia64@linuxia64.org
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>		/* for capable() */
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/efi.h>
#include <asm/uaccess.h>
#ifdef CONFIG_SMP
#include <linux/smp.h>
#endif

MODULE_AUTHOR("Matt Domsch <Matt_Domsch@Dell.com>");
MODULE_DESCRIPTION("/proc interface to EFI Variables");
MODULE_LICENSE("GPL");

#define EFIVARS_VERSION "0.03 2001-Apr-20"

static int
efivar_read(char *page, char **start, off_t off,
	    int count, int *eof, void *data);
static int
efivar_write(struct file *file, const char *buffer,
	     unsigned long count, void *data);


/*
 * The maximum size of VariableName + Data = 1024
 * Therefore, it's reasonable to save that much
 * space in each part of the structure,
 * and we use a page for reading/writing.
 */

typedef struct _efi_variable_t {
	efi_char16_t  VariableName[1024/sizeof(efi_char16_t)];
	efi_guid_t    VendorGuid;
	unsigned long DataSize;
	__u8          Data[1024];
	efi_status_t  Status;
	__u32         Attributes;
} __attribute__((packed)) efi_variable_t;


typedef struct _efivar_entry_t {
	efi_variable_t          var;
	struct proc_dir_entry   *entry;
	struct list_head        list;
} efivar_entry_t;

static spinlock_t efivars_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(efivar_list);
static struct proc_dir_entry *efi_vars_dir = NULL;

#define efivar_entry(n) list_entry(n, efivar_entry_t, list)

/* Return the number of unicode characters in data */
static unsigned long
utf8_strlen(efi_char16_t *data, unsigned long maxlength)
{
	unsigned long length = 0;
	while (*data++ != 0 && length < maxlength)
		length++;
	return length;
}

/* Return the number of bytes is the length of this string */
/* Note: this is NOT the same as the number of unicode characters */
static inline unsigned long
utf8_strsize(efi_char16_t *data, unsigned long maxlength)
{
	return utf8_strlen(data, maxlength/sizeof(efi_char16_t)) *
		sizeof(efi_char16_t);
}


static int
proc_calc_metrics(char *page, char **start, off_t off,
		  int count, int *eof, int len)
{
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}


static void
uuid_unparse(efi_guid_t *guid, char *out)
{
	sprintf(out, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		guid->data1, guid->data2, guid->data3,
		guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
		guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}





/*
 * efivar_create_proc_entry()
 * Requires:
 *    variable_name_size = number of bytes required to hold
 *                         variable_name (not counting the NULL
 *                         character at the end.
 * Returns 1 on failure, 0 on success
 */
static int
efivar_create_proc_entry(unsigned long variable_name_size,
			 efi_char16_t *variable_name,
			 efi_guid_t *vendor_guid)
{

	int i, short_name_size = variable_name_size /
		sizeof(efi_char16_t) + 38;
	char *short_name = kmalloc(short_name_size+1,
				   GFP_KERNEL);
	efivar_entry_t *new_efivar = kmalloc(sizeof(efivar_entry_t),
					     GFP_KERNEL);
	if (!short_name || !new_efivar)  {
		if (short_name)        kfree(short_name);
		if (new_efivar)        kfree(new_efivar);
		return 1;
	}
	memset(short_name, 0, short_name_size+1);
	memset(new_efivar, 0, sizeof(efivar_entry_t));

	memcpy(new_efivar->var.VariableName, variable_name,
	       variable_name_size);
	memcpy(&(new_efivar->var.VendorGuid), vendor_guid, sizeof(efi_guid_t));

	/* Convert Unicode to normal chars (assume top bits are 0),
	   ala UTF-8 */
	for (i=0; i<variable_name_size / sizeof(efi_char16_t); i++) {
		short_name[i] = variable_name[i] & 0xFF;
	}

	/* This is ugly, but necessary to separate one vendor's
	   private variables from another's.         */

	*(short_name + strlen(short_name)) = '-';
	uuid_unparse(vendor_guid, short_name + strlen(short_name));


	/* Create the entry in proc */
	new_efivar->entry = create_proc_entry(short_name, 0600, efi_vars_dir);
	kfree(short_name); short_name = NULL;
	if (!new_efivar->entry) return 1;


	new_efivar->entry->data = new_efivar;
	new_efivar->entry->read_proc = efivar_read;
	new_efivar->entry->write_proc = efivar_write;

	list_add(&new_efivar->list, &efivar_list);


	return 0;
}



/***********************************************************
 * efivar_read()
 * Requires:
 * Modifies: page
 * Returns: number of bytes written, or -EINVAL on failure
 ***********************************************************/

static int
efivar_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sizeof(efi_variable_t);
	efivar_entry_t *efi_var = data;
	efi_variable_t *var_data = (efi_variable_t *)page;

	if (!page || !data) return -EINVAL;

	spin_lock(&efivars_lock);
	MOD_INC_USE_COUNT;

	memcpy(var_data, &efi_var->var, len);

	var_data->DataSize = 1024;
	var_data->Status = efi.get_variable(var_data->VariableName,
					    &var_data->VendorGuid,
					    &var_data->Attributes,
					    &var_data->DataSize,
					    var_data->Data);

	MOD_DEC_USE_COUNT;
	spin_unlock(&efivars_lock);

	return proc_calc_metrics(page, start, off, count, eof, len);
}

/***********************************************************
 * efivar_write()
 * Requires: data is an efi_setvariable_t data type,
 *           properly filled in, possibly by a call
 *           first to efivar_read().
 *           Caller must have CAP_SYS_ADMIN
 * Modifies: NVRAM
 * Returns: var_data->DataSize on success, errno on failure
 *
 ***********************************************************/
static int
efivar_write(struct file *file, const char *buffer,
	     unsigned long count, void *data)
{
	unsigned long strsize1, strsize2;
	int found=0;
	struct list_head *pos;
	unsigned long size = sizeof(efi_variable_t);
	efi_status_t status;
	efivar_entry_t *efivar = data, *search_efivar = NULL;
	efi_variable_t *var_data;
	if (!data || count != size) {
		printk(KERN_WARNING "efivars: improper struct of size 0x%lx passed.\n", count);
		return -EINVAL;
	}
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	MOD_INC_USE_COUNT;

	var_data = kmalloc(size, GFP_KERNEL);
	if (!var_data) {
		MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
	if (copy_from_user(var_data, buffer, size)) {
		MOD_DEC_USE_COUNT;
                kfree(var_data);
		return -EFAULT;
	}

	spin_lock(&efivars_lock);

	/* Since the data ptr we've currently got is probably for
	   a different variable find the right variable.
	   This allows any properly formatted data structure to
	   be written to any of the files in /proc/efi/vars and it will work.
	*/
	list_for_each(pos, &efivar_list) {
		search_efivar = efivar_entry(pos);
		strsize1 = utf8_strsize(search_efivar->var.VariableName, 1024);
		strsize2 = utf8_strsize(var_data->VariableName, 1024);
		if ( strsize1 == strsize2 &&
		     !memcmp(&(search_efivar->var.VariableName),
			     var_data->VariableName, strsize1) &&
		     !efi_guidcmp(search_efivar->var.VendorGuid,
				  var_data->VendorGuid)) {
			found = 1;
			break;
		}
	}
	if (found) efivar = search_efivar;

	status = efi.set_variable(var_data->VariableName,
				  &var_data->VendorGuid,
				  var_data->Attributes,
				  var_data->DataSize,
				  var_data->Data);

	if (status != EFI_SUCCESS) {
		printk(KERN_WARNING "set_variable() failed: status=%lx\n", status);
		kfree(var_data);
		MOD_DEC_USE_COUNT;
		spin_unlock(&efivars_lock);
		return -EIO;
	}


	if (!var_data->DataSize || !var_data->Attributes) {
		/* We just deleted the NVRAM variable */
		remove_proc_entry(efivar->entry->name, efi_vars_dir);
		list_del(&efivar->list);
		kfree(efivar);
	}

	/* If this is a new variable, set up the proc entry for it. */
	if (!found) {
		efivar_create_proc_entry(utf8_strsize(var_data->VariableName,
						      1024),
					 var_data->VariableName,
					 &var_data->VendorGuid);
	}

	kfree(var_data);
	MOD_DEC_USE_COUNT;
	spin_unlock(&efivars_lock);
	return size;
}



static int __init
efivars_init(void)
{

	efi_status_t status;
	efi_guid_t vendor_guid;
	efi_char16_t *variable_name = kmalloc(1024, GFP_KERNEL);
	unsigned long variable_name_size = 1024;

	spin_lock(&efivars_lock);

	printk(KERN_INFO "EFI Variables Facility v%s\n", EFIVARS_VERSION);

        /* Since efi.c happens before procfs is available,
           we create the directory here if it doesn't
           already exist.  There's probably a better way
           to do this.
        */
        if (!efi_dir)
                efi_dir = proc_mkdir("efi", NULL);

	efi_vars_dir = proc_mkdir("vars", efi_dir);



	/* Per EFI spec, the maximum storage allocated for both
	   the variable name and variable data is 1024 bytes.
	*/

	memset(variable_name, 0, 1024);

	do {
		variable_name_size=1024;

		status = efi.get_next_variable(&variable_name_size,
					       variable_name,
					       &vendor_guid);


		switch (status) {
		case EFI_SUCCESS:
			efivar_create_proc_entry(variable_name_size,
						 variable_name,
						 &vendor_guid);
			break;
		case EFI_NOT_FOUND:
			break;
		default:
			printk(KERN_WARNING "get_next_variable: status=%lx\n", status);
			status = EFI_NOT_FOUND;
			break;
		}

	} while (status != EFI_NOT_FOUND);

	kfree(variable_name);
	spin_unlock(&efivars_lock);
	return 0;
}

static void __exit
efivars_exit(void)
{
	struct list_head *pos;
	efivar_entry_t *efivar;

	spin_lock(&efivars_lock);

	list_for_each(pos, &efivar_list) {
		efivar = efivar_entry(pos);
		remove_proc_entry(efivar->entry->name, efi_vars_dir);
		list_del(&efivar->list);
		kfree(efivar);
	}
	remove_proc_entry(efi_vars_dir->name, efi_dir);
	spin_unlock(&efivars_lock);

}

module_init(efivars_init);
module_exit(efivars_exit);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
