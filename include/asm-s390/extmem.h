/*
 *  include/asm-s390x/extmem.h
 *
 *  definitions for external memory segment support
 *  Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 */

#ifndef _ASM_S390X_DCSS_H
#define _ASM_S390X_DCSS_H
#ifndef __ASSEMBLY__
#define SEGMENT_SHARED_RW       0
#define SEGMENT_SHARED_RO       1
#define SEGMENT_EXCLUSIVE_RW    2
#define SEGMENT_EXCLUSIVE_RO    3
extern int segment_load (char *name,int segtype,unsigned long *addr,unsigned long *length);
extern void segment_unload(char *name);
extern void segment_replace(char *name);
#endif
#endif
