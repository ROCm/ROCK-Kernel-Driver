/*
 *  include/asm-s390/misc390.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */

#define	allocaligned2(type,name,number,align)				\
   __u8	name##buff[(sizeof(type)*(number+1))-1]; 	                \
   type	*name=(type *)(((__u32)(&name##buff[align-1]))&(-align))

#define	allocaligned(type,name,number)  allocaligned2(type,name,number,__alignof__(type))

