/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
	.text
	.globl	_start
_start:
	lis	9,_start@h
	lis	8,_etext@ha
	addi	8,8,_etext@l
1:	dcbf	0,9
	icbi	0,9
	addi	9,9,0x20
	cmplwi	0,9,8
	blt	1b
	b	start
