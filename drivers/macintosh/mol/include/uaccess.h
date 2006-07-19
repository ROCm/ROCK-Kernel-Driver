/* 
 *   Creation Date: <2004/02/01 20:02:11 samuel>
 *   Time-stamp: <2004/02/01 20:02:11 samuel>
 *   
 *	<uaccess.h>
 *	
 *	
 *   
 *   Copyright (C) 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *   
 */

#ifndef _H_UACCESS
#define _H_UACCESS


static inline unsigned int copy_to_user_mol( void *to, const void *from, ulong len ) {
	return copy_to_user( to, from, len );
}
static inline unsigned int copy_from_user_mol( void *to, const void *from, ulong len ) {
	return copy_from_user( to, from, len );
}

static inline unsigned int copy_int_to_user( int *to, int val ) {
	return copy_to_user_mol( to, &val, sizeof(int) );
}
static inline unsigned int copy_int_from_user( int *retint, int *userptr ) {
	return copy_from_user_mol( retint, userptr, sizeof(int) );
}


#endif   /* _H_UACCESS */
