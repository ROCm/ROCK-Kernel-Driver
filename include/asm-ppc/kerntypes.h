/*
 * asm-ppc/kerntypes.h
 *
 * Arch-dependent header file that includes headers for all arch-specific 
 * types of interest.
 * The kernel type information is used by the lcrash utility when
 * analyzing system crash dumps or the live system. Using the type
 * information for the running system, rather than kernel header files,
 * makes for a more flexible and robust analysis tool.
 *
 * This source code is released under the GNU GPL.
 */

/* PowerPC-specific header files */
#ifndef _PPC_KERNTYPES_H
#define _PPC_KERNTYPES_H

/* Use the default */
#include <asm-generic/kerntypes.h>

#endif /* _PPC_KERNTYPES_H */
