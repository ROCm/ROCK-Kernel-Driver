#ifndef _LINUX_DPROBES_IN_H
#define _LINUX_DPROBES_IN_H

/*
 * IBM Dynamic Probes
 * Copyright (c) International Business Machines Corp., 2000
 */

/*
 * This header file defines the opcodes for the RPN instructions.
 *
 * Opcodes 0x00 to 0xAF are for common, arch-independent instructions.
 * Opcodes 0xB0 to 0xDF are for arch-dependent instructions.
 * Opcodes 0xF1 to 0xFF are for two byte instructions.
 */

#define DP_NOP			0x00					

/*
 * RPN execution group.
 */
#define DP_JMP			0x01
#define DP_JLT			0x02
#define DP_JLE			0x03
#define DP_JGT			0x04
#define DP_JGE			0x05
#define DP_JZ			0x06
#define DP_JNZ			0x07
#define DP_LOOP			0x08
#define DP_CALL			0x09
#define DP_RET			0x0a
#define DP_ABORT		0x0b
#define DP_REM			0x0c
#define DP_EXIT			0x0d
#define DP_EXIT_N		0x0e
#define DP_SUSPEND		0x0f

/*
 * Logging Group.
 */
#define DP_RESUME		0x10
#define DP_SETMIN_I		0x11
#define DP_SETMIN		0x12
#define DP_SETMAJ_I		0x13
#define DP_SETMAJ		0x14
#define DP_LOG_STR		0x15
#define DP_LOG_MRF		0x17
#define DP_LOG_I		0x1b

/*
 * Global Variable Group.
 */
#define DP_ALLOC_GV		0x1c
#define DP_FREE_GV		0x1d
#define DP_FREE_GVI		0x1e
#define DP_PUSH_GVI		0x1f
#define DP_PUSH_GV		0x20
#define DP_POP_GVI		0x21
#define DP_POP_GV		0x22
#define DP_MOVE_GVI		0x23
#define DP_MOVE_GV		0x24
#define DP_INC_GVI		0x25
#define DP_INC_GV		0x26
#define DP_DEC_GVI		0x27
#define DP_DEC_GV		0x28

/*
 * Local Variable Group.
 */
#define DP_PUSH_LVI		0x29
#define DP_PUSH_LV		0x2a
#define DP_POP_LVI		0x2b
#define DP_POP_LV		0x2c
#define DP_MOVE_LVI		0x2d
#define DP_MOVE_LV		0x2e
#define DP_INC_LVI		0x2f
#define DP_INC_LV		0x30
#define DP_DEC_LVI		0x31
#define DP_DEC_LV		0x32

/*
 * Arithmetic Group.
 */
#define DP_ADD			0x33
#define DP_SUB			0x34
#define DP_MUL			0x35

/*
 * Logic Group.
 */
#define DP_NEG			0x36
#define DP_AND			0x37
#define DP_OR			0x38
#define DP_XOR			0x39
#define DP_ROL_I		0x3a
#define DP_ROL			0x3b
#define DP_ROR_I		0x3c
#define DP_ROR			0x3d
#define DP_SHL_I		0x3e
#define DP_SHL			0x3f
#define DP_SHR_I		0x40
#define DP_SHR			0x41

/*
 * RPN Stack Group.
 */
#define DP_XCHG			0x42
#define DP_DUP_I		0x43
#define DP_DUP			0x44
#define DP_ROS			0x45

/*
 * Register Group.
 */
#define DP_PUSH_R		0x46
#define DP_POP_R		0x47
#define DP_PUSH_U		0x48
#define DP_POP_U		0x49

/* 
 * Data Group.
 */
#define DP_PUSH			0x4c
#define DP_PUSH_MEM_U8		0x4d
#define DP_PUSH_MEM_U16		0x4e
#define DP_PUSH_MEM_U32		0x4f
#define DP_PUSH_MEM_U64		0x50
#define DP_POP_MEM_U8		0x51
#define DP_POP_MEM_U16		0x52
#define DP_POP_MEM_U32		0x53
#define DP_POP_MEM_U64		0x54

/*
 * System Variable Group.
 */
#define DP_PUSH_TASK		0x5d
#define DP_PUSH_PID		0x5e
#define DP_PUSH_PROCID		0x5f

/*
 * Address Verification.
 */
#define DP_VFY_R		0x60
#define DP_VFY_RW		0x61

/*
 * Some more arithmetic/logic instructions.
 */
#define DP_DIV			0x62
#define DP_IDIV			0x63
#define DP_PBL			0x64
#define DP_PBR			0x65
#define DP_PBL_I		0x66
#define DP_PBR_I		0x67
#define DP_PZL			0x68
#define DP_PZR			0x69
#define DP_PZL_I		0x6a
#define DP_PZR_I		0x6b

/*
 * Exception handling/Stacktrace instructions.
 */
#define DP_SX			0x6c
#define DP_UX			0x6d
#define DP_RX			0x6e
#define DP_PUSH_X		0x6f
#define DP_PUSH_LP		0x70
#define DP_PUSH_PLP		0x71
#define DP_POP_LP		0x72
#define DP_LOG_ST		0x73
#define DP_PURGE_ST		0x74
#define DP_TRACE_LV		0x75
#define DP_TRACE_GV		0x76
#define DP_TRACE_PV		0x77

#define DP_PUSH_SBP		0x78
#define DP_POP_SBP		0x79
#define DP_PUSH_TSP		0x7a
#define DP_POP_TSP		0x7b
#define DP_PUSH_SBP_I		0x7c
#define DP_POP_SBP_I		0x7d
#define DP_PUSH_TSP_I		0x7e
#define DP_POP_TSP_I		0x7f
#define DP_COPY_SBP_I		0x80
#define DP_COPY_TSP_I		0x81
#define DP_PUSH_STP		0x82
#define DP_POP_STP		0x83

#define DP_SAVE_SBP		0x84
#define DP_RESTORE_SBP		0x85
#define DP_SAVE_TSP		0x86
#define DP_RESTORE_TSP		0x87
#define DP_COPY_SBP		0x88
#define DP_COPY_TSP		0x89

/* stack based log instructions */
#define DP_LOG			0x8a
#define DP_LOG_LV		0x8b
#define DP_LOG_GV		0x8c

#define DP_CALLK		0x8d

/* byte heap instructions */
#define DP_MALLOC		0x8e
#define DP_FREE			0x8f
#define DP_PUSH_WID		0x90
#define DP_PUSHH_U8_I		0x91
#define DP_PUSHH_U16_I		0x92
#define DP_PUSHH_U32_I		0x93
#define DP_PUSHH_U64_I		0x94
#define DP_POPH_U8_I		0x95
#define DP_POPH_U16_I		0x96
#define DP_POPH_U32_I		0x97
#define DP_POPH_U64_I		0x98

#include <asm/dprobes_in.h>

#endif
