#ifndef __I386_UNALIGNED_H
#define __I386_UNALIGNED_H

/*
 * The i386 can do unaligned accesses itself. 
 *
 * The strange macros are there to make sure these can't
 * be misused in a way that makes them not work on other
 * architectures where unaligned accesses aren't as simple.
 */

#define get_unaligned(ptr) (*(ptr))

#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

#endif
