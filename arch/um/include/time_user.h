/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __TIME_USER_H__
#define __TIME_USER_H__

extern void timer(void);
extern void get_profile_timer(void);
extern void disable_profile_timer(void);
extern void switch_timers(int to_real);
extern void user_time_init(void);
extern void set_timers(int set_signal);
extern void idle_sleep(int secs);

#endif
