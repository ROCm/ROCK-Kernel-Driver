/* $Id$
 *
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
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/eeprom.h>
#include <asm/sn/router.h>
#include <asm/sn/module.h>
#include <asm/sn/ksys/l1.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn_sal.h>
#include <linux/ctype.h>

#define ELSC_TIMEOUT	1000000		/* ELSC response timeout (usec) */
#define LOCK_TIMEOUT	5000000		/* Hub lock timeout (usec) */

#define hub_cpu_get()	0

#define LBYTE(caddr)	(*(char *) caddr)

extern char *bcopy(const char * src, char * dest, int count);

#define LDEBUG		0

/*
 * ELSC data is in NVRAM page 7 at the following offsets.
 */

#define NVRAM_MAGIC_AD	0x700		/* magic number used for init */
#define NVRAM_PASS_WD	0x701		/* password (4 bytes in length) */
#define NVRAM_DBG1	0x705		/* virtual XOR debug switches */
#define NVRAM_DBG2	0x706		/* physical XOR debug switches */
#define NVRAM_CFG	0x707		/* ELSC Configuration info */
#define NVRAM_MODULE	0x708		/* system module number */
#define NVRAM_BIST_FLG	0x709		/* BIST flags (2 bits per nodeboard) */
#define NVRAM_PARTITION 0x70a		/* module's partition id */
#define	NVRAM_DOMAIN	0x70b		/* module's domain id */
#define	NVRAM_CLUSTER	0x70c		/* module's cluster id */
#define	NVRAM_CELL	0x70d		/* module's cellid */

#define NVRAM_MAGIC_NO	0x37		/* value of magic number */
#define NVRAM_SIZE	16		/* 16 bytes in nvram */


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

	*rack = (result & L1_ADDR_RACK_MASK) >> L1_ADDR_RACK_SHFT;
	*bay = (result & L1_ADDR_BAY_MASK) >> L1_ADDR_BAY_SHFT;
	*brick_type = (result & L1_ADDR_TYPE_MASK) >> L1_ADDR_TYPE_SHFT;
	*brick_type = toupper(*brick_type);

	return 0;
}


int iomoduleid_get(nasid_t nasid)
{

	int result = 0;

	if ( ia64_sn_sysctl_iobrick_module_get(nasid, &result) )
		return( ELSC_ERROR_CMD_SEND );

	return result;

}

int iobrick_module_get(nasid_t nasid)
{
    uint rnum, rack, bay, brick_type, t;
    int ret;

    /* construct module ID from rack and slot info */

    if ((ret = iobrick_rack_bay_type_get(nasid, &rnum, &bay, &brick_type)) < 0)
        return ret;

    if (bay > MODULE_BPOS_MASK >> MODULE_BPOS_SHFT)
        return ELSC_ERROR_MODULE;

    /* Build a moduleid_t-compatible rack number */

    rack = 0;           
    t = rnum / 100;             /* rack class (CPU/IO) */
    if (t > RACK_CLASS_MASK(rack) >> RACK_CLASS_SHFT(rack))
        return ELSC_ERROR_MODULE;
    RACK_ADD_CLASS(rack, t);
    rnum %= 100;

    t = rnum / 10;              /* rack group */
    if (t > RACK_GROUP_MASK(rack) >> RACK_GROUP_SHFT(rack))
        return ELSC_ERROR_MODULE;
    RACK_ADD_GROUP(rack, t);

    t = rnum % 10;              /* rack number (one-based) */
    if (t-1 > RACK_NUM_MASK(rack) >> RACK_NUM_SHFT(rack))
        return ELSC_ERROR_MODULE;
    RACK_ADD_NUM(rack, t);

    switch( brick_type ) {
      case 'I': 
	brick_type = MODULE_IBRICK; break;
      case 'P':
	brick_type = MODULE_PBRICK; break;
      case 'X':
	brick_type = MODULE_XBRICK; break;
    }

    ret = RBT_TO_MODULE(rack, bay, brick_type);

    return ret;
}
#ifdef CONFIG_PCI
/*
 * iobrick_module_get_nasid() returns a module_id which has the brick
 * type encoded in bits 15-12, but this is not the true brick type...
 * The module_id returned by iobrick_module_get_nasid() is modified
 * to make a PEBRICKs & PXBRICKs look like a PBRICK.  So this routine
 * iobrick_type_get_nasid() returns the true unmodified brick type.
 */
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
        if( brick_types[t] == type )
            return t;
    }

    return -1;    /* unknown brick */
}
#endif
int iobrick_module_get_nasid(nasid_t nasid)
{
    int io_moduleid;

#ifdef PIC_LATER
    uint rack, bay;

    if (PEBRICK_NODE(nasid)) {
        if (peer_iobrick_rack_bay_get(nasid, &rack, &bay)) {
            printf("Could not read rack and bay location "
                   "of PEBrick at nasid %d\n", nasid);
        }

        io_moduleid = peer_iobrick_module_get(sc, rack, bay);
    }
#endif	/* PIC_LATER */
    io_moduleid = iobrick_module_get(nasid);
    return io_moduleid;
}
