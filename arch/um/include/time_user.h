/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __TIME_USER_H__
#define __TIME_USER_H__

extern void timer(void);
extern void switch_timers(int to_real);
extern void set_interval(int timer_type);
extern void idle_sleep(int secs);
extern void enable_timer(void);
extern void time_lock(void);
extern void time_unlock(void);

#endif
