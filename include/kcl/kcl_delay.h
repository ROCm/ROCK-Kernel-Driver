#ifndef AMDKCL_DELAY_H
#define AMDKCL_DELAY_H

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/printk.h>

#ifndef HAVE_FSLEEP
static inline void _kcl_fsleep(unsigned long usecs)
{
       if (usecs <= 10)
               udelay(usecs);
       else if (usecs <= 20000)
               usleep_range(usecs, 2 * usecs);
       else
               msleep(DIV_ROUND_UP(usecs, 1000));
}

#define fsleep _kcl_fsleep

#endif

#ifndef HAVE_USLEEP_RANGE_STATE
static inline void _kcl_usleep_range_state(unsigned long min, unsigned long max,
			unsigned int state)
{
        if (state != TASK_UNINTERRUPTIBLE)
                pr_warn_once("legacy kernel without usleep_range_state()\n");

        usleep_range(min, max);
}

#define usleep_range_state _kcl_usleep_range_state
#endif

#endif
