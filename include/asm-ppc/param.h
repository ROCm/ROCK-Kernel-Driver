#ifndef _ASM_PPC_PARAM_H
#define _ASM_PPC_PARAM_H

#ifndef HZ
#define HZ 100
#endif

#ifdef __KERNEL__
#define HZ		100		/* internal timer frequency */
#define USER_HZ		100		/* for user interfaces in "ticks" */
#define CLOCKS_PER_SEC	(USER_HZ)	/* frequency at which times() counts */
#endif /* __KERNEL__ */

#define EXEC_PAGESIZE	4096

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif
