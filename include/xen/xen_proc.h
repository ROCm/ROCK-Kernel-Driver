
#ifndef __ASM_XEN_PROC_H__
#define __ASM_XEN_PROC_H__

#include <linux/proc_fs.h>

struct proc_dir_entry *create_xen_proc_entry(const char *, mode_t,
					     const struct file_operations *,
					     void *data);
void remove_xen_proc_entry(const char *);

#endif /* __ASM_XEN_PROC_H__ */
