#ifndef _ASMARM_SHMPARAM_H
#define _ASMARM_SHMPARAM_H

/*
 * This should be the size of the virtually indexed cache/ways,
 * or page size, whichever is greater since the cache aliases
 * every size/ways bytes.
 */
#if __LINUX_ARM_ARCH__ > 5
#define	SHMLBA	(4 * PAGE_SIZE)
#else
#define	SHMLBA PAGE_SIZE		 /* attach addr a multiple of this */
#endif

#endif /* _ASMARM_SHMPARAM_H */
