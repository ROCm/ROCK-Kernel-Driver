/* -*- linux-c -*-
 *
 *	$Id: sysrq.h,v 1.3 1997/07/17 11:54:33 mj Exp $
 *
 *	Linux Magic System Request Key Hacks
 *
 *	(c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/config.h>

struct pt_regs;
struct kbd_struct;
struct tty_struct;

/* Generic SysRq interface -- you may call it from any device driver, supplying
 * ASCII code of the key, pointer to registers and kbd/tty structs (if they
 * are available -- else NULL's).
 */

void handle_sysrq(int, struct pt_regs *, struct kbd_struct *, struct tty_struct *);

/* Deferred actions */

extern int emergency_sync_scheduled;

#define EMERG_SYNC 1
#define EMERG_REMOUNT 2

void do_emergency_sync(void);

#ifdef CONFIG_MAGIC_SYSRQ
#define CHECK_EMERGENCY_SYNC			\
	if (emergency_sync_scheduled)		\
		do_emergency_sync();
#else
#define CHECK_EMERGENCY_SYNC
#endif
