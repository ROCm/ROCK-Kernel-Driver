#include <linux/config.h>
#include <linux/module.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/pm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/tty.h>
 
#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/hardirq.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/pgtable.h>

extern void dump_thread(struct pt_regs *, struct user *);
extern unsigned long get_cmos_time(void);
extern void __ashrdi3(void);

/* platform dependent support */

EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(get_cmos_time);

EXPORT_SYMBOL(strtok);
EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(simple_strtol);
EXPORT_SYMBOL(strstr);

EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(__ashrdi3);

/* other stuff */

EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__generic_copy_from_user);
EXPORT_SYMBOL(__generic_copy_to_user);
EXPORT_SYMBOL(strnlen_user);
EXPORT_SYMBOL(__copy_user_zeroing);
EXPORT_SYMBOL(__copy_user);

#undef memcpy
#undef memset
extern void * memset(void *,int,__kernel_size_t);
extern void * memcpy(void *,const void *,__kernel_size_t);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memset);


