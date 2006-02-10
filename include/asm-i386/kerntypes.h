/*
 * asm-i386/kerntypes.h
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

/* ix86-specific header files */
#ifndef _I386_KERNTYPES_H
#define _I386_KERNTYPES_H

/* Use the default */
#include <asm-generic/kerntypes.h>

#endif /* _I386_KERNTYPES_H */
