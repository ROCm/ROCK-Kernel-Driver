/*
 * debug.c - ACPI debug interface to userspace.
 */

#include <linux/proc_fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME		("debug")

#define ACPI_SYSTEM_FILE_DEBUG_LAYER	"debug_layer"
#define ACPI_SYSTEM_FILE_DEBUG_LEVEL	"debug_level"

static int
acpi_system_read_debug (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	char			*p = page;
	int 			size = 0;

	if (off != 0)
		goto end;

	switch ((unsigned long) data) {
	case 0:
		p += sprintf(p, "0x%08x\n", acpi_dbg_layer);
		break;
	case 1:
		p += sprintf(p, "0x%08x\n", acpi_dbg_level);
		break;
	default:
		p += sprintf(p, "Invalid debug option\n");
		break;
	}
	
end:
	size = (p - page);
	if (size <= off+count) *eof = 1;
	*start = page + off;
	size -= off;
	if (size>count) size = count;
	if (size<0) size = 0;

	return size;
}


static int
acpi_system_write_debug (
	struct file             *file,
        const char              *buffer,
	unsigned long           count,
        void                    *data)
{
	char			debug_string[12] = {'\0'};

	ACPI_FUNCTION_TRACE("acpi_system_write_debug");

	if (count > sizeof(debug_string) - 1)
		return_VALUE(-EINVAL);

	if (copy_from_user(debug_string, buffer, count))
		return_VALUE(-EFAULT);

	debug_string[count] = '\0';

	switch ((unsigned long) data) {
	case 0:
		acpi_dbg_layer = simple_strtoul(debug_string, NULL, 0);
		break;
	case 1:
		acpi_dbg_level = simple_strtoul(debug_string, NULL, 0);
		break;
	default:
		return_VALUE(-EINVAL);
	}

	return_VALUE(count);
}

static int __init acpi_debug_init(void)
{
	struct proc_dir_entry	*entry;
	int error = 0;
	char * name;

	ACPI_FUNCTION_TRACE("acpi_debug_init");

	if (acpi_disabled)
		return_VALUE(0);

	/* 'debug_layer' [R/W] */
	name = ACPI_SYSTEM_FILE_DEBUG_LAYER;
	entry = create_proc_read_entry(name, S_IFREG|S_IRUGO|S_IWUSR, acpi_root_dir,
				       acpi_system_read_debug,(void *)0);
	if (entry)
		entry->write_proc = acpi_system_write_debug;
	else
		goto Error;

	/* 'debug_level' [R/W] */
	name = ACPI_SYSTEM_FILE_DEBUG_LEVEL;
	entry = create_proc_read_entry(name, S_IFREG|S_IRUGO|S_IWUSR, acpi_root_dir,
				       acpi_system_read_debug, (void *)1);
	if (entry) 
		entry->write_proc = acpi_system_write_debug;
	else
		goto Error;

 Done:
	return_VALUE(error);

 Error:
	ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			 "Unable to create '%s' proc fs entry\n", name));

	remove_proc_entry(ACPI_SYSTEM_FILE_DEBUG_LEVEL, acpi_root_dir);
	remove_proc_entry(ACPI_SYSTEM_FILE_DEBUG_LAYER, acpi_root_dir);
	error = -EFAULT;
	goto Done;
}

subsys_initcall(acpi_debug_init);
