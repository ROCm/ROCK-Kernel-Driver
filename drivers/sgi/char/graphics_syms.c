/*
 * graphics_syms.c: interfaces for SGI Indy newport graphics
 *
 * Copyright (C) 1999 Alex deVries <puffin@redhat.com>
 *
 * We should not even be trying to compile this if we are not doing
 * a module.
 */

#define __NO_VERSION__
#include <linux/module.h>

/* extern int rrm_command (unsigned int cmd, void *arg);
extern int rrm_close (struct inode *inode, struct file *file);
EXPORT_SYMBOL(rrm_command);
EXPORT_SYMBOL(rrm_close);


*/
extern void shmiq_init (void);
extern void usema_init(void);

EXPORT_SYMBOL(shmiq_init);
EXPORT_SYMBOL(usema_init);

extern void disable_gconsole(void);
extern void enable_gconsole(void);
extern void remove_mapping (struct task_struct *task, unsigned long start,
      unsigned long end);

EXPORT_SYMBOL(disable_gconsole);
EXPORT_SYMBOL(enable_gconsole);
EXPORT_SYMBOL(remove_mapping);

EXPORT_SYMBOL(npregs);
