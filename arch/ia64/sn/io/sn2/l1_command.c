/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1992-1997,2000-2003 Silicon Graphics, Inc. All rights reserved.
 */ 

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/router.h>
#include <asm/sn/module.h>
#include <asm/sn/ksys/l1.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn_sal.h>
#include <linux/ctype.h>

/* elsc_display_line writes up to 12 characters to either the top or bottom
 * line of the L1 display.  line points to a buffer containing the message
 * to be displayed.  The zero-based line number is specified by lnum (so
 * lnum == 0 specifies the top line and lnum == 1 specifies the bottom).
 * Lines longer than 12 characters, or line numbers not less than
 * L1_DISPLAY_LINES, cause elsc_display_line to return an error.
 */
int elsc_display_line(nasid_t nasid, char *line, int lnum)
{
    return 0;
}


/*
 * iobrick routines
 */

/* iobrick_rack_bay_type_get fills in the three int * arguments with the
 * rack number, bay number and brick type of the L1 being addressed.  Note
 * that if the L1 operation fails and this function returns an error value, 
 * garbage may be written to brick_type.
 */


int iobrick_rack_bay_type_get( nasid_t nasid, uint *rack, 
			       uint *bay, uint *brick_type )
{
	int result = 0;

	if ( ia64_sn_sysctl_iobrick_module_get(nasid, &result) )
		return( ELSC_ERROR_CMD_SEND );

	*rack = (result & MODULE_RACK_MASK) >> MODULE_RACK_SHFT;
	*bay = (result & MODULE_BPOS_MASK) >> MODULE_BPOS_SHFT;
	*brick_type = (result & MODULE_BTYPE_MASK) >> MODULE_BTYPE_SHFT;
	return 0;
}


int iomoduleid_get(nasid_t nasid)
{
	int result = 0;

	if ( ia64_sn_sysctl_iobrick_module_get(nasid, &result) )
		return( ELSC_ERROR_CMD_SEND );

	return result;
}

int
iobrick_type_get_nasid(nasid_t nasid)
{
    uint rack, bay, type;
    int t, ret;
    extern char brick_types[];

    if ((ret = iobrick_rack_bay_type_get(nasid, &rack, &bay, &type)) < 0) {
        return ret;
    }

    /* convert brick_type to lower case */
    if ((type >= 'A') && (type <= 'Z'))
        type = type - 'A' + 'a';

    /* convert to a module.h brick type */
    for( t = 0; t < MAX_BRICK_TYPES; t++ ) {
        if( brick_types[t] == type ) {
            return t;
	}
    }

    return -1;    /* unknown brick */
}

/*
 * given a L1 bricktype, return a bricktype string.  This string is the
 * string that will be used in the hwpath for I/O bricks
 */
char *
iobrick_L1bricktype_to_name(int type)
{
    switch (type)
    {
    default:
        return("Unknown");

    case L1_BRICKTYPE_PX:
        return(EDGE_LBL_PXBRICK);

    case L1_BRICKTYPE_OPUS:
        return(EDGE_LBL_OPUSBRICK);

    case L1_BRICKTYPE_IX:
        return(EDGE_LBL_IXBRICK);

    case L1_BRICKTYPE_C:
        return("Cbrick");

    case L1_BRICKTYPE_R:
        return("Rbrick");

    case L1_BRICKTYPE_CHI_CG:
        return(EDGE_LBL_CGBRICK);
    }
}

