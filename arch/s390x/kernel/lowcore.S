/*
 *  arch/s390/kernel/lowcore.S
 *    S390 lowcore definition.
 *
 *  S390 64 bit Version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hpenner@de.ibm.com)
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 */
#include <asm/lowcore.h>
	
         .align 8192
         .globl init_S390_lowcore
init_S390_lowcore:      
         .fill 0x1a0-0x000,1,0	
         .quad  _RESTART_PSW_MASK
         .quad  restart_int_handler 
         .quad  _EXT_PSW_MASK
         .quad  ext_int_handler 
         .quad  _SVC_PSW_MASK
         .quad  system_call
         .quad  _PGM_PSW_MASK
         .quad  pgm_check_handler 
         .quad  _MCCK_PSW_MASK
         .quad  mcck_int_handler 
EXT_PSW: .quad  _IO_PSW_MASK
         .quad  io_int_handler
	 .fill  0x2000-0x200,1,0
