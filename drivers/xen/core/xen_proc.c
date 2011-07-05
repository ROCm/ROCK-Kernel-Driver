#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <xen/xen_proc.h>

static struct proc_dir_entry *xen_base;

struct proc_dir_entry *
#ifndef MODULE
__init
#endif
create_xen_proc_entry(const char *name, mode_t mode)
{
	if ( xen_base == NULL )
		if ( (xen_base = proc_mkdir("xen", NULL)) == NULL )
			panic("Couldn't create /proc/xen");
	return create_proc_entry(name, mode, xen_base);
}

#ifdef MODULE
EXPORT_SYMBOL_GPL(create_xen_proc_entry); 
#elif defined(CONFIG_XEN_PRIVILEGED_GUEST)

void remove_xen_proc_entry(const char *name)
{
	remove_proc_entry(name, xen_base);
}

#endif
