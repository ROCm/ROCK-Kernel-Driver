/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_CMN_ERR_H
#define _ASM_SN_CMN_ERR_H

/*
** Common error handling severity levels.  Converted to be
** represented by the associated 4.3BSD syslog priorities.
*/

#define CE_DEBUG	KERN_DEBUG	/* debug	*/
#define CE_CONT		KERN_INFO	/* continuation	*/
#define CE_NOTE		KERN_NOTICE	/* notice	*/
#define CE_WARN		KERN_WARNING	/* warning	*/
#define CE_ALERT	KERN_ALERT	/* alert	*/
#define CE_PANIC	KERN_EMERG	/* panic	*/

#define	CE_LEVELMASK	LOG_PRIMASK	/* mask for severity level	*/
#define	CE_CPUID	0x8		/* prepend CPU id to output	*/
#define CE_PHYSID	0x10		/* prepend CPU phys location    */
#define CE_SYNC		0x20		/* wait for uart to drain before returning */

/* Flags for Availmon Monitoring
 * When a developer or's these bits into the cmn_err flags above,
 * and they have availmon installed, certain "actions" will take
 * place depending upon how they have the availmon software configured.
 */
#define CE_TOOKACTIONS   0x0100            /* Actions taken by some error   */
#define CE_RUNNINGPOOR   0x0200            /* System running degraded       */
#define CE_MAINTENANCE   0x0400            /* System needs maintenance      */
#define CE_CONFIGERROR   0x0800            /* System configured incorrectly */

/* Bitmasks for separating subtasks from priority levels */
#define CE_PRIOLEVELMASK 0x00ff  /* bitmask for severity levels of cmn_err */
#define CE_SUBTASKMASK   0xff00  /* bitmask for availmon actions of cmn_err */
#define CE_AVAILMONALL   (CE_TOOKACTIONS|CE_RUNNINGPOOR| \
                                 CE_MAINTENANCE|CE_CONFIGERROR)

#ifdef __KERNEL__

#define CE_PBPANIC	KERN_CRIT	/* Special define used to manipulate
					 * putbufndx in kernel */

/* Console output flushing flag and routine */

extern int constrlen;		/* Length of current console string, if zero,
				   there are no characters to flush */
#define	CONBUF_LOCKED	0	/* conbuf is already locked */
#define	CONBUF_UNLOCKED	1	/* need to reacquire lock */
#define CONBUF_DRAIN	2	/* ensure output before returning */

/*
 * bit field descriptions for printf %r and %R formats
 *
 * printf("%r %R", val, reg_descp);
 * struct reg_desc *reg_descp;
 *
 * the %r and %R formats allow formatted print of bit fields.  individual
 * bit fields are described by a struct reg_desc, multiple bit fields within
 * a single word can be described by multiple reg_desc structures.
 * %r outputs a string of the format "<bit field descriptions>"
 * %R outputs a string of the format "0x%x<bit field descriptions>"
 *
 * The fields in a reg_desc are:
 *	__psunsigned_t rd_mask;	An appropriate mask to isolate the bit field
 *				within a word, and'ed with val
 *
 *	int rd_shift;		A shift amount to be done to the isolated
 *				bit field.  done before printing the isolate
 *				bit field with rd_format and before searching
 *				for symbolic value names in rd_values
 *
 *	char *rd_name;		If non-null, a bit field name to label any
 *				out from rd_format or searching rd_values.
 *				if neither rd_format or rd_values is non-null
 *				rd_name is printed only if the isolated
 *				bit field is non-null.
 *
 *	char *rd_format;	If non-null, the shifted bit field value
 *				is printed using this format.
 *
 *	struct reg_values *rd_values;	If non-null, a pointer to a table
 *				matching numeric values with symbolic names.
 *				rd_values are searched and the symbolic
 *				value is printed if a match is found, if no
 *				match is found "???" is printed.
 *				
 */


/*
 * register values
 * map between numeric values and symbolic values
 */
struct reg_values {
	__psunsigned_t rv_value;
	char *rv_name;
};

/*
 * register descriptors are used for formatted prints of register values
 * rd_mask and rd_shift must be defined, other entries may be null
 */
struct reg_desc {
	k_machreg_t rd_mask;	/* mask to extract field */
	int rd_shift;		/* shift for extracted value, - >>, + << */
	char *rd_name;		/* field name */
	char *rd_format;	/* format to print field */
	struct reg_values *rd_values;	/* symbolic names of values */
};

#endif	/* __KERNEL__ */
#endif	/* _ASM_SN_CMN_ERR_H */
