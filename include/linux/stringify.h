#ifndef __LINUX_STRINGIFY_H
#define __LINUX_STRINGIFY_H

/* Indirect stringification.  Doing two levels allows the parameter to be a
 * macro itself.  For example, compile with -DFOO=bar, __stringify(FOO)
 * converts to "bar".
 *
 * The "..." is gcc's cpp vararg macro syntax.  It is required because __ALIGN,
 * in linkage.h, contains a comma, which when expanded, causes it to look 
 * like two arguments, which breaks the standard non-vararg stringizer.
 */

#define __stringify_1(x)	#x
#define __stringify(x)		__stringify_1(x)

#endif	/* !__LINUX_STRINGIFY_H */
