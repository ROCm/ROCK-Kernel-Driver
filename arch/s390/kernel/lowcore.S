/*
 *  arch/s390/kernel/lowcore.S
 *    S390 lowcore definition.
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#include <asm/lowcore.h>
         .align 4096
         .globl init_S390_lowcore
init_S390_lowcore:      
         .long  _RESTART_PSW_MASK
         .long  restart_int_handler + _ADDR_31
         .long  0,0
         .long  0,0
EXT_OLD: .long  0,0
SVC_OLD: .long  0,0
PGM_OLD: .long  0,0
MCCK_OLD:.long  0,0
IO_OLD:  .long  0,0
         .long  0,0,0,0,0,0
#
# new psws need all to be physical
# because we start with dat off
#
EXT_PSW: .long  _EXT_PSW_MASK
         .long  ext_int_handler + _ADDR_31
#
SVC_PSW: .long  _SVC_PSW_MASK
         .long  system_call + _ADDR_31
#
PGM_PSW: .long  _PGM_PSW_MASK
         .long  pgm_check_handler + _ADDR_31
#
MCCK_PSW:.long  _MCCK_PSW_MASK
         .long  mcck_int_handler + _ADDR_31
#
IO_PSW:  .long  _IO_PSW_MASK
         .long  io_int_handler + _ADDR_31
#
#
#
EXTERNAL_PARAMETER:     .long  0
CPU_ADDRESS:            .word 0
EXT_INTERRUPT_CODE:     .word 0
SVC_ILC:                .word 0
SVC_CODE:               .word 0
PGM_ILC:                .word 0
PGM_CODE:               .word 0
TRANS_EXC_ADDR:         .long  0                         # 090
                        .fill 0xC00-0x094,1,0
SAVE_AREA:              .fill 0x40,1,0                   # C00
KERNEL_STACK:           .long 0                          # C40
KERNEL_LEVEL:           .long 0                          # C44
CPUID:                  .long 0,0                        # C48
                        .fill 0x1000-0xC50,1,0

