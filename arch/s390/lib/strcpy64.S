/*
 *  arch/s390/kernel/strcpy.S
 *    S390 strcpy routine
 *
 *  S390 version
 *    Copyright (C) 2004 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

/*
 * R2 = address of destination
 * R3 = address of source string
 */
        .globl   strcpy
strcpy:
	sgr	%r0,%r0
0:	mvst	%r2,%r3
	jo	0b
	br	%r14

