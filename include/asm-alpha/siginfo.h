#ifndef _ALPHA_SIGINFO_H
#define _ALPHA_SIGINFO_H

#define SI_PAD_SIZE	((SI_MAX_SIZE/sizeof(int)) - 4)

#define SIGEV_PAD_SIZE	((SIGEV_MAX_SIZE/sizeof(int)) - 4)

#define HAVE_ARCH_COPY_SIGINFO

#include <asm-generic/siginfo.h>

#ifdef __KERNEL__
#include <linux/string.h>

extern inline void copy_siginfo(siginfo_t *to, siginfo_t *from)
{
	if (from->si_code < 0)
		memcpy(to, from, sizeof(siginfo_t));
	else
		/* _sigchld is currently the largest know union member */
		memcpy(to, from, 4*sizeof(int) + sizeof(from->_sifields._sigchld));
}

#endif /* __KERNEL__ */

#endif
