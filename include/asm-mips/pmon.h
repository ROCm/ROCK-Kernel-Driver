/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle
 */
#ifndef _ASM_PMON_H
#define _ASM_PMON_H

struct callvectors {
	int	(*open) (char*, int, int);		/*	 0 */
	int	(*close) (int);				/*	 4 */
	int	(*read) (int, void*, int);		/*	 8 */
	int	(*write) (int, void*, int);		/*	12 */
	off_t	(*lseek) (int, off_t, int);		/*	16 */
	int	(*printf) (const char*, ...);		/*	20 */
	void	(*cacheflush) (void);			/*	24 */
	char*	(*gets) (char*);			/*	28 */
	int	(*cpustart) (int, void *, int, int);	/*	32 */
};

extern struct callvectors *debug_vectors;

#endif /* _ASM_PMON_H */
