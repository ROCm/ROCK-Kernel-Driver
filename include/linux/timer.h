#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/config.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/cache.h>

struct tvec_t_base_s;

/*
 * Timers may be dynamically created and destroyed, and should be initialized
 * by a call to init_timer() upon creation.
 *
 * The "data" field enables use of a common timeout function for several
 * timeouts. You can use this field to distinguish between the different
 * invocations.
 */
typedef struct timer_list {
	struct list_head list;
	unsigned long expires;
	unsigned long data;
	void (*function)(unsigned long);
	struct tvec_t_base_s *base;
} timer_t;

extern void add_timer(timer_t * timer);
extern int del_timer(timer_t * timer);
  
#ifdef CONFIG_SMP
extern int del_timer_sync(timer_t * timer);
extern void sync_timers(void);
#define timer_enter(base, t) do { base->running_timer = t; mb(); } while (0)
#define timer_exit(base) do { base->running_timer = NULL; } while (0)
#define timer_is_running(base,t) (base->running_timer == t)
#define timer_synchronize(base,t) while (timer_is_running(base,t)) barrier()
#else
#define del_timer_sync(t)	del_timer(t)
#define sync_timers()		do { } while (0)
#define timer_enter(base,t)          do { } while (0)
#define timer_exit(base)            do { } while (0)
#endif
  
/*
 * mod_timer is a more efficient way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 * mod_timer(a,b) is equivalent to del_timer(a); a->expires = b; add_timer(a).
 * If the timer is known to be not pending (ie, in the handler), mod_timer
 * is less efficient than a->expires = b; add_timer(a).
 */
int mod_timer(timer_t *timer, unsigned long expires);

extern void it_real_fn(unsigned long);

extern void init_timers(void);
extern void run_local_timers(void);

static inline void init_timer(timer_t * timer)
{
	timer->list.next = timer->list.prev = NULL;
	timer->base = NULL;
}

static inline int timer_pending(const timer_t * timer)
{
	return timer->list.next != NULL;
}

#endif
