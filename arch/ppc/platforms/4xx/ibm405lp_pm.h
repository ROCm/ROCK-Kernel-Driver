#ifndef IBM405LP_PM_H
#define IBM405LP_PM_H

#include <asm/types.h>

#ifndef __ASSEMBLY__

/* sysctl numbers */

/* top-level number for the 405LP sleep sysctl()s
 *
 * This is picked at random out of thin air, hoping that it won't
 * clash with someone.  That's really ugly, but appears to be
 * "standard" practice (!?).  Oh well, with any luck we can throw
 * these away and replace them with sysfs parameters, in the
 * not-too-distant future...
 */
#define CTL_PM_405LP 0xbc17

/* sleep sysctls */
enum
{
	PM_405LP_SLEEP_CMD=1,
	PM_405LP_SLEEP_MODE=2,
	PM_405LP_SLEEP_ALARM=3,
	PM_405LP_SLEEP_DEBUG_CDIV=4,
	PM_405LP_SLEEP_DEBUG_WATCHDOG=5
};

/* Used to tell firmware where to return control to Linux on
 * wake. Currently only the first two words are used by firmware; the
 * rest are Linux convenience.
 */
struct ibm405lp_wakeup_info {
	/* physical address of wakeup function */
	void (*wakeup_func_phys)(unsigned long apm0_cfg,
				 unsigned long apm0_sr);
	u32 magic;

	/* private to Linux: */
	unsigned long wakeup_stack_phys; /* physical stack pointer */
};

#endif /* __ASSEMBLY__ */

#define IBM405LP_WAKEUP_MAGIC	(0x31415926)

/* These values are ORed into RTC0_CEN before APM power-down modes as a
 * signal to the firmware as to which type of wakeup is required. */

#define IBM405LP_POWERDOWN_REBOOT    0x00 /* Reboot the system */
#define IBM405LP_POWERDOWN_SUSPEND   0x40 /* Suspend-to-RAM */
#define IBM405LP_POWERDOWN_HIBERNATE 0x80 /* Hibernate to device 0 */

#endif /* IBM405LP_PM_H */
