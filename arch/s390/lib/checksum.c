/*
 *  arch/s390/lib/checksum.c
 *    S390 fast network checksum routines
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ulrich Hild        (first version),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 * This file contains network checksum routines
 */
 
#include <linux/string.h>
#include <linux/types.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/checksum.h>

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */
unsigned int
csum_partial (const unsigned char *buff, int len, unsigned int sum)
{
	  /*
	   * Experiments with ethernet and slip connections show that buff
	   * is aligned on either a 2-byte or 4-byte boundary.
	   */
        __asm__ __volatile__ (
                "    lr   2,%1\n"    /* address in gpr 2 */
                "    lr   3,%2\n"    /* length in gpr 3 */
                "0:  cksm %0,2\n"    /* do checksum on longs */
                "    jo   0b\n"
                : "+&d" (sum)
                : "d" (buff), "d" (len)
                : "cc", "2", "3" );
        return sum;
}

/*
 *	Fold a partial checksum without adding pseudo headers
 */
unsigned short csum_fold(unsigned int sum)
{
        __asm__ __volatile__ (
                "    sr   3,3\n"   /* %0 = H*65536 + L */
                "    lr   2,%0\n"  /* %0 = H L, R2/R3 = H L / 0 0 */
                "    srdl 2,16\n"  /* %0 = H L, R2/R3 = 0 H / L 0 */
                "    alr  2,3\n"   /* %0 = H L, R2/R3 = L H / L 0 */
                "    alr  %0,2\n"  /* %0 = H+L+C L+H */
                "    srl  %0,16\n" /* %0 = H+L+C */
                : "+d" (sum) : : "cc", "2", "3");
        return ((unsigned short) ~sum);
}

