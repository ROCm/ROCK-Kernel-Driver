/*
 * Copyright (C) 2001,2002,2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/config.h>
#include <linux/init.h>

#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/sibyte/board.h>

	.text
	.set	noat
	.set	mips4

        __INIT
        
	/* Cache Error handler for SB1 */
	LEAF(except_vec2_sb1)
	mfc0	k1, $26
	# check if error was recoverable
	bltz	k1, leave_cerr
#ifdef CONFIG_SB1_PASS_1_WORKAROUNDS
	# look for signature of spurious CErr
	lui	k0, 0x4000
	bne	k0, k1, 1f
	.word	0x401Bd801			# mfc0	k1, $27, 1
	lui	k0, 0xffe0
	and	k1, k0, k1
	lui	k0, 0x0200
	beq	k0, k1, leave_cerr
1:
#endif
	j	handle_vec2_sb1

leave_cerr:
	# clear/unlock the registers
	mtc0	zero, $26
	mtc0	zero, $27
	.word	0x4080d801			# mtc0	zero, $27, 1
	.word	0x4080d803			# mtc0	zero, $27, 3
	eret
	END(except_vec2_sb1)

	__FINIT

	LEAF(handle_vec2_sb1)
	mfc0	k0,CP0_CONFIG
	li	k1,~CONF_CM_CMASK
	and	k0,k0,k1
	ori	k0,k0,CONF_CM_UNCACHED
	mtc0	k0,CP0_CONFIG

	SSNOP
	SSNOP
	SSNOP
	SSNOP
	bnezl	$0, 1f
1:
	mfc0	k0, CP0_STATUS
	sll	k0, k0, 3			# check CU0 (kernel?)
	bltz	k0, 2f
	get_saved_sp
2:	j	sb1_cache_error

	END(handle_vec2_sb1)
