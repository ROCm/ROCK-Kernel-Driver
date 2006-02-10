/*
 * asm-x86_64/kerntypes.h
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

/* x86_64-specific header files */
#ifndef _X86_64_KERNTYPES_H
#define _X86_64_KERNTYPES_H

/* Use the default */
#include <asm-generic/kerntypes.h>

#endif /* _X86_64_KERNTYPES_H */
