/*
 * File:	mca.c
 * Purpose:	SN specific MCA code.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2003 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <asm/sn/sgi.h>
#include <asm/mca.h>
#include <asm/sal.h>
#include <asm/sn/sn_sal.h>



/*
 * Interval for calling SAL to poll for errors that do NOT cause error
 * interrupts. SAL will raise a CPEI if any errors are present that
 * need to be logged.
 */
#define CPEI_INTERVAL	(5*HZ)


struct timer_list sn_cpei_timer;
void sn_init_cpei_timer(void);


/*
 * print_hook
 *
 * This function is the callback routine that SAL calls to log error
 * info for platform errors. 
 */
static int
print_hook(const char *fmt, ...)
{
	static int	newline=1;
	char		buf[400], *p;
	va_list		args;
	int		len=0;


	va_start(args, fmt);
	if (newline) {
		strcpy(buf, "+ ");
		len += 2;
	}
	len += vsnprintf(buf+len, sizeof(buf)-len, fmt, args);

	/* Prefix each line with "+ " to be consistent with mca.c. */
	p = buf;
	while ((p=strchr(p, '\n')) && *++p != '\0') {
		memmove(p+2, p, 1+strlen(p));
		strncpy(p, "+ ", 2);
		len += 2;
	}
	newline = (p != 0);

	va_end(args);
	printk("%s", buf);
	return len;
}



/*
 * ia64_sn2_platform_plat_specific_err_print
 *
 * Called by the MCA handler to log platform-specific errors.
 */
void
ia64_sn2_platform_plat_specific_err_print(int header_len, int sect_len, u8 *p_data, prfunc_t prfunc)
{
	ia64_sn_plat_specific_err_print(print_hook, p_data - sect_len);
}



static void
sn_cpei_handler(int irq, void *devid, struct pt_regs *regs)
{
	/*
	 * this function's sole purpose is to call SAL when we receive
	 * a CE interrupt from SHUB or when the timer routine decides
	 * we need to call SAL to check for CEs.
	 */

	/* CALL SAL_LOG_CE */

	ia64_sn_plat_cpei_handler();
}


static void
sn_cpei_timer_handler(unsigned long dummy)
{
	sn_cpei_handler(-1, NULL, NULL);
	mod_timer(&sn_cpei_timer, jiffies + CPEI_INTERVAL);
}

void
sn_init_cpei_timer(void)
{
	init_timer(&sn_cpei_timer);
	sn_cpei_timer.expires = jiffies + CPEI_INTERVAL;
        sn_cpei_timer.function = sn_cpei_timer_handler;
	add_timer(&sn_cpei_timer);
}
