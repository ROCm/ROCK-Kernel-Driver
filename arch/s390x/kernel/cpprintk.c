#include "cpcmd.h"
#include <linux/mm.h>
#include <linux/tty_driver.h>
#include <linux/smp_lock.h>
#include <linux/console.h>
#include <linux/init.h>

#include <asm/uaccess.h>

static char buf[1024];

asmlinkage int s390printk(const char *fmt, ...)
{
	va_list args;
	int i;
	unsigned long flags;
	spin_lock_irqsave(&console_lock, flags);
	va_start(args, fmt);
	i = vsprintf(&buf[0],"MSG * ",args);
	i = vsprintf(&buf[i], fmt, args); 
	va_end(args);
	cpcmd(buf,0,0);
	spin_unlock_irqrestore(&console_lock, flags);
	return i;
}
