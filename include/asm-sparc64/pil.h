/* $Id: pil.h,v 1.1 2002/01/23 11:27:36 davem Exp $ */
#ifndef _SPARC64_PIL_H
#define _SPARC64_PIL_H

/* To avoid some locking problems, we hard allocate certain PILs
 * for SMP cross call messages.  cli() does not block the cross
 * call delivery, so when SMP locking is an issue we reschedule
 * the event into a PIL interrupt which is blocked by cli().
 *
 * XXX In fact the whole set of PILs used for hardware interrupts
 * XXX may be allocated in this manner.  All of the devices can
 * XXX happily sit at the same PIL.  We would then need only two
 * XXX PILs, one for devices and one for the CPU local timer tick.
 */
#define PIL_MIGRATE	1

#ifndef __ASSEMBLY__
#define PIL_RESERVED(PIL)	((PIL) == PIL_MIGRATE)
#endif

#endif /* !(_SPARC64_PIL_H) */
