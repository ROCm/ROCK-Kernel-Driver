#ifndef _ASM_CACHE_H
#define _ASM_CACHE_H

/* Etrax 100LX have 32-byte cache-lines. When we add support for future chips
 * here should be a check for CPU type.
 */

#define L1_CACHE_BYTES 32

#define L1_CACHE_SHIFT_MAX 5	/* largest L1 which this arch supports */

#endif /* _ASM_CACHE_H */
