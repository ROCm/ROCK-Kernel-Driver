/*
 *      MIPL Mobile IPv6 Debugging macros and functions
 *
 *      $Id: s.debug.h 1.19 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _DEBUG_H
#define _DEBUG_H

#include <linux/autoconf.h>

/* priorities for different debug conditions */

#define DBG_CRITICAL   0 /* unrecoverable error                     */
#define DBG_ERROR      1 /* error (recoverable)                     */
#define DBG_WARNING    2 /* unusual situation but not a real error  */
#define DBG_INFO       3 /* generally useful information            */
#define DBG_EXTRA      4 /* extra information                       */
#define DBG_FUNC_ENTRY 6 /* use to indicate function entry and exit */
#define DBG_DATADUMP   7 /* packet dumps, etc. lots of flood        */

/**
 * NIPV6ADDR - macro for IPv6 addresses
 * @addr: Network byte order IPv6 address
 *
 * Macro for printing IPv6 addresses.  Used in conjunction with
 * printk() or derivatives (such as DEBUG macro).
 **/
#define NIPV6ADDR(addr) \
        ntohs(((u16 *)addr)[0]), \
        ntohs(((u16 *)addr)[1]), \
        ntohs(((u16 *)addr)[2]), \
        ntohs(((u16 *)addr)[3]), \
        ntohs(((u16 *)addr)[4]), \
        ntohs(((u16 *)addr)[5]), \
        ntohs(((u16 *)addr)[6]), \
        ntohs(((u16 *)addr)[7])

#ifdef CONFIG_IPV6_MOBILITY_DEBUG
extern int mipv6_debug;

/**
 * debug_print - print debug message
 * @debug_level: message priority
 * @fname: calling function's name
 * @fmt: printf-style formatting string
 *
 * Prints a debug message to system log if @debug_level is less or
 * equal to @mipv6_debug.  Should always be called using DEBUG()
 * macro, not directly.
 **/
static void debug_print(int debug_level, const char *fname, const char* fmt, ...)
{
	char s[1024];
	va_list args;
 
	if (mipv6_debug < debug_level)
		return;
 
	va_start(args, fmt);
	vsprintf(s, fmt, args);
	printk("mip6[%s]: %s\n", fname, s);
	va_end(args);
}

/**
 * debug_print_buffer - print arbitrary buffer to system log
 * @debug_level: message priority
 * @data: pointer to buffer
 * @len: number of bytes to print
 *
 * Prints @len bytes from buffer @data to system log.  @debug_level
 * tells on which debug level message gets printed.  For
 * debug_print_buffer() priority %DBG_DATADUMP should be used.
 **/
#define debug_print_buffer(debug_level,data,len) { \
	if (mipv6_debug >= debug_level) { \
	int i; \
	for (i=0; i<len; i++) { \
		if (i%16 == 0) printk("\n%04x: ", i); \
		printk("%02x ", ((unsigned char *)data)[i]); \
	} \
	printk("\n\n"); \
	} \
}

#define DEBUG(x,y,z...) debug_print(x,__FUNCTION__,y,##z)
#define DEBUG_FUNC() \
DEBUG(DBG_FUNC_ENTRY, "%s(%d)/%s: ", __FILE__,__LINE__,__FUNCTION__)

#else
#define DEBUG(x,y,z...)
#define DEBUG_FUNC()
#define debug_print_buffer(x,y,z)
#endif

#undef ASSERT
#define ASSERT(expression) { \
        if (!(expression)) { \
                (void)printk(KERN_ERR \
                 "Assertion \"%s\" failed: file \"%s\", function \"%s\", line %d\n", \
                 #expression, __FILE__, __FUNCTION__, __LINE__); \
		BUG(); \
        } \
}

#endif /* _DEBUG_H */
