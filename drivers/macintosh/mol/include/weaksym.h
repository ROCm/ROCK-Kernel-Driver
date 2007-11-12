/* 
 *   Creation Date: <2001/08/02 23:53:57 samuel>
 *   Time-stamp: <2001/08/03 00:30:23 samuel>
 *   
 *	<weak.h>
 *	
 *	Support of weak symbols (extract, stolen from glibc)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_WEAKSYM
#define _H_WEAKSYM


/* Define ALIASNAME as a strong alias for NAME.  */
#define strong_alias(name, aliasname) _strong_alias(name, aliasname)
#define _strong_alias(name, aliasname) \
  extern __typeof (name) aliasname __attribute__ ((alias (#name)));

/* This comes between the return type and function name in
   a function definition to make that definition weak.  */
#define weak_function __attribute__ ((weak))
#define weak_const_function __attribute__ ((weak, __const__))

/* Define ALIASNAME as a weak alias for NAME. */
#define weak_alias(name, aliasname) _weak_alias (name, aliasname)
#define _weak_alias(name, aliasname) \
  extern __typeof (name) aliasname __attribute__ ((weak, alias (#name)));

/* Declare SYMBOL as weak undefined symbol (resolved to 0 if not defined).  */
#define weak_extern(symbol) _weak_extern (symbol)
#  define _weak_extern(symbol)    asm (".weak " #symbol);


#endif   /* _H_WEAKSYM */
