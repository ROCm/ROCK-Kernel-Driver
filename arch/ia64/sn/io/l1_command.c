/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */ 

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/eeprom.h>
#include <asm/sn/ksys/i2c.h>
#include <asm/sn/cmn_err.h>
#include <asm/sn/router.h>
#include <asm/sn/module.h>
#include <asm/sn/ksys/l1.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/clksupport.h>

#define ELSC_TIMEOUT	1000000		/* ELSC response timeout (usec) */
#define LOCK_TIMEOUT	5000000		/* Hub lock timeout (usec) */

#define LOCAL_HUB	LOCAL_HUB_ADDR
#define LD(x)		(*(volatile uint64_t *)(x))
#define SD(x, v)	(LD(x) = (uint64_t) (v))

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

/*
 * Declare a static ELSC NVRAM buffer to hold all data read from
 * and written to NVRAM.  This nvram "cache" will be used only during the
 * IP27prom execution.
 */
static char elsc_nvram_buffer[NVRAM_SIZE];

#define SC_COMMAND sc_command


/*
 * elsc_init
 *
 *   Initialize ELSC structure
 */

void elsc_init(elsc_t *e, nasid_t nasid)
{
    sc_init((l1sc_t *)e, nasid, BRL1_LOCALUART);
}


/*
 * elsc_errmsg
 *
 *   Given a negative error code,
 *   returns a corresponding static error string.
 */

char *elsc_errmsg(int code)
{
    switch (code) {
    case ELSC_ERROR_CMD_SEND:
	return "Command send error";
    case ELSC_ERROR_CMD_CHECKSUM:
	return "Command packet checksum error";
    case ELSC_ERROR_CMD_UNKNOWN:
	return "Unknown command";
    case ELSC_ERROR_CMD_ARGS:
	return "Invalid command argument(s)";
    case ELSC_ERROR_CMD_PERM:
	return "Permission denied";
    case ELSC_ERROR_RESP_TIMEOUT:
	return "System controller response timeout";
    case ELSC_ERROR_RESP_CHECKSUM:
	return "Response packet checksum error";
    case ELSC_ERROR_RESP_FORMAT:
	return "Response format error";
    case ELSC_ERROR_RESP_DIR:
	return "Response direction error";
    case ELSC_ERROR_MSG_LOST:
	return "Message lost because queue is full";
    case ELSC_ERROR_LOCK_TIMEOUT:
	return "Timed out getting ELSC lock";
    case ELSC_ERROR_DATA_SEND:
	return "Error sending data";
    case ELSC_ERROR_NIC:
	return "NIC protocol error";
    case ELSC_ERROR_NVMAGIC:
	return "Bad magic number in NVRAM";
    case ELSC_ERROR_MODULE:
	return "Module location protocol error";
    default:
	return "Unknown error";
    }
}

/*
 * elsc_nvram_init
 *
 *   Initializes reads and writes to NVRAM.  This will perform a single
 *   read to NVRAM, getting all data at once.  When the PROM tries to
 *   read NVRAM, it returns the data from the buffer being read.  If the
 *   PROM tries to write out to NVRAM, the write is done, and the internal
 *   buffer is updated.
 */

void elsc_nvram_init(nasid_t nasid, uchar_t *elsc_nvram_data)
{
    /* This might require implementation of multiple-packet request/responses
     * if it's to provide the same behavior that was available in SN0.
     */
    nasid = nasid;
    elsc_nvram_data = elsc_nvram_data;
}

/*
 * elsc_nvram_copy
 *
 *   Copies the content of a buffer into the static buffer in this library.
 */

void elsc_nvram_copy(uchar_t *elsc_nvram_data)
{
    memcpy(elsc_nvram_buffer, elsc_nvram_data, NVRAM_SIZE);
}

/*
 * elsc_nvram_write
 *
 *   Copies bytes from 'buf' into NVRAM, starting at NVRAM address
 *   'addr' which must be between 0 and 2047.
 *
 *   If 'len' is non-negative, the routine copies 'len' bytes.
 *
 *   If 'len' is negative, the routine treats the data as a string and
 *   copies bytes up to and including a NUL-terminating zero, but not
 *   to exceed '-len' bytes.
 */

int elsc_nvram_write(elsc_t *e, int addr, char *buf, int len)
{
    /* Here again, we might need to work out the details of a
     * multiple-packet protocol.
     */

    /* For now, pretend it worked. */
    e = e;
    addr = addr;
    buf = buf;
    return (len < 0 ? -len : len);
}

/*
 * elsc_nvram_read
 *
 *   Copies bytes from NVRAM into 'buf', starting at NVRAM address
 *   'addr' which must be between 0 and 2047.
 *
 *   If 'len' is non-negative, the routine copies 'len' bytes.
 *
 *   If 'len' is negative, the routine treats the data as a string and
 *   copies bytes up to and including a NUL-terminating zero, but not
 *   to exceed '-len' bytes.  NOTE:  This method is no longer supported.
 *   It was never used in the first place.
 */

int elsc_nvram_read(elsc_t *e, int addr, char *buf, int len)
{
    /* multiple packets? */
    e = e;
    addr = addr;
    buf = buf;
    len = len;
    return -1;
}

/*
 * Command Set
 */

int elsc_version(elsc_t *e, char *result)
{
    char	msg[BRL1_QSIZE];
    int		len;    /* length of message being sent */
    int		subch;  /* system controller subchannel used */
    int		major,  /* major rev number */
	        minor,  /* minor rev number */
                bugfix; /* bugfix rev number */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    subch = sc_open( (l1sc_t *)e, L1_ADDR_LOCAL );

    if( (len = sc_construct_msg( (l1sc_t *)e, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_FW_REV, 0 )) < 0 )
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( SC_COMMAND( (l1sc_t *)e, subch, msg, msg, &len ) < 0 )
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( (l1sc_t *)e, subch );

    /* check response */
    if( sc_interpret_resp( msg, 6, L1_ARG_INT, &major,
			   L1_ARG_INT, &minor, L1_ARG_INT, &bugfix )
	< 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    sprintf( result, "%d.%d.%d", major, minor, bugfix );

    return 0;
}

int elsc_debug_set(elsc_t *e, u_char byte1, u_char byte2)
{
    /* shush compiler */
    e = e;
    byte1 = byte1;
    byte2 = byte2;

    /* fill in a buffer with the opcode & params; call sc_command */

    return 0;
}

int elsc_debug_get(elsc_t *e, u_char *byte1, u_char *byte2)
{
    char	msg[BRL1_QSIZE];
    int		subch;  /* system controller subchannel used */
    int		dbg_sw; /* holds debug switch settings */
    int		len;	/* number of msg buffer bytes used */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    if( (subch = sc_open( (l1sc_t *)e, L1_ADDR_LOCAL )) < 0 ) {
	return( ELSC_ERROR_CMD_SEND );
    }

    if( (len = sc_construct_msg( (l1sc_t *)e, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_RDBG, 0 ) ) < 0 )
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( sc_command( (l1sc_t *)e, subch, msg, msg, &len ) < 0 )
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( (l1sc_t *)e, subch );

    /* check response */
    if( sc_interpret_resp( msg, 2, L1_ARG_INT, &dbg_sw ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    /* copy out debug switch settings (last two bytes of the
     * integer response)
     */
    *byte1 = ((dbg_sw >> 8) & 0xFF);
    *byte2 = (dbg_sw & 0xFF);

    return 0;
}

/*
 * elsc_rack_bay_get fills in the two int * arguments with the
 * rack number and bay number of the L1 being addressed
 */
int elsc_rack_bay_get(elsc_t *e, uint *rack, uint *bay)
{
    char msg[BRL1_QSIZE];	/* L1 request/response info */
    int subch;			/* system controller subchannel used */
    int len;			/* length of message */
    uint32_t	buf32;		/* used to copy 32-bit rack/bay out of msg */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    if( (subch = sc_open( (l1sc_t *)e, L1_ADDR_LOCAL )) < 0 ) {
	return( ELSC_ERROR_CMD_SEND );
    }

    if( (len = sc_construct_msg( (l1sc_t *)e, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_RRACK, 0 )) < 0 ) 
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }


    /* send the request to the L1 */
    if( sc_command( (l1sc_t *)e, subch, msg, msg, &len ) ) {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close(e, subch);

    /* check response */
    if( sc_interpret_resp( msg, 2, L1_ARG_INT, &buf32 ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    /* extract rack/bay info
     *
     * note that the 32-bit value returned by the L1 actually
     * only uses the low-order sixteen bits for rack and bay
     * information.  A "normal" L1 address puts rack and bay
     * information in bit positions 12 through 28.  So if
     * we initially shift the value returned 12 bits to the left,
     * we can use the L1 addressing #define's to extract the
     * values we need (see ksys/l1.h for a complete list of the
     * various fields of an L1 address).
     */
    buf32 <<= L1_ADDR_BAY_SHFT;

    *rack = (buf32 & L1_ADDR_RACK_MASK) >> L1_ADDR_RACK_SHFT;
    *bay = (buf32 & L1_ADDR_BAY_MASK) >> L1_ADDR_BAY_SHFT;

    return 0;
}


/* elsc_rack_bay_type_get fills in the three int * arguments with the
 * rack number, bay number and brick type of the L1 being addressed.  Note
 * that if the L1 operation fails and this function returns an error value, 
 * garbage may be written to brick_type.
 */
int elsc_rack_bay_type_get( l1sc_t *sc, uint *rack, 
			       uint *bay, uint *brick_type )
{
    char msg[BRL1_QSIZE];       /* L1 request/response info */
    int subch;                  /* system controller subchannel used */
    int len;                    /* length of message */
    uint32_t buf32;	        /* used to copy 32-bit rack & bay out of msg */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    if( (subch = sc_open( sc, L1_ADDR_LOCAL )) < 0 ) {
	return ELSC_ERROR_CMD_SEND;
    }

    if( (len = sc_construct_msg( sc, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_RRBT, 0 )) < 0 )
    {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( SC_COMMAND( sc, subch, msg, msg, &len ) ) {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( sc, subch );

    /* check response */
    if( sc_interpret_resp( msg, 4, L1_ARG_INT, &buf32, 
			           L1_ARG_INT, brick_type ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    /* extract rack/bay info
     *
     * note that the 32-bit value returned by the L1 actually
     * only uses the low-order sixteen bits for rack and bay
     * information.  A "normal" L1 address puts rack and bay
     * information in bit positions 12 through 28.  So if
     * we initially shift the value returned 12 bits to the left,
     * we can use the L1 addressing #define's to extract the
     * values we need (see ksys/l1.h for a complete list of the
     * various fields of an L1 address).
     */
    buf32 <<= L1_ADDR_BAY_SHFT;

    *rack = (buf32 & L1_ADDR_RACK_MASK) >> L1_ADDR_RACK_SHFT;
    *bay = (buf32 & L1_ADDR_BAY_MASK) >> L1_ADDR_BAY_SHFT;

    /* convert brick_type to lower case */
    *brick_type = *brick_type - 'A' + 'a';

    return 0;
}


int elsc_module_get(elsc_t *e)
{
    extern char brick_types[];
    uint rnum, rack, bay, bricktype, t;
    int ret;

    /* construct module ID from rack and slot info */

    if ((ret = elsc_rack_bay_type_get(e, &rnum, &bay, &bricktype)) < 0)
	return ret;

    /* report unset location info. with a special, otherwise invalid modid */
    if (rnum == 0 && bay == 0)
	return MODULE_NOT_SET;

    if (bay > MODULE_BPOS_MASK >> MODULE_BPOS_SHFT)
	return ELSC_ERROR_MODULE;

    /* Build a moduleid_t-compatible rack number */

    rack = 0;		
    t = rnum / 100;		/* rack class (CPU/IO) */
    if (t > RACK_CLASS_MASK(rack) >> RACK_CLASS_SHFT(rack))
	return ELSC_ERROR_MODULE;
    RACK_ADD_CLASS(rack, t);
    rnum %= 100;

    t = rnum / 10;		/* rack group */
    if (t > RACK_GROUP_MASK(rack) >> RACK_GROUP_SHFT(rack))
	return ELSC_ERROR_MODULE;
    RACK_ADD_GROUP(rack, t);

    t = rnum % 10;		/* rack number (one-based) */
    if (t-1 > RACK_NUM_MASK(rack) >> RACK_NUM_SHFT(rack))
	return ELSC_ERROR_MODULE;
    RACK_ADD_NUM(rack, t);

    for( t = 0; t < MAX_BRICK_TYPES; t++ ) {
	if( brick_types[t] == bricktype )
	    return RBT_TO_MODULE(rack, bay, t);
    }
    
    return ELSC_ERROR_MODULE;
}

int elsc_partition_set(elsc_t *e, int partition)
{
    char msg[BRL1_QSIZE];       /* L1 request/response info */
    int subch;                  /* system controller subchannel used */
    int len;                    /* length of message */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    if( (subch = sc_open( e, L1_ADDR_LOCAL )) < 0 ) {
	return ELSC_ERROR_CMD_SEND;
    }

    if( (len = sc_construct_msg( e, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_PARTITION_SET, 2,
				 L1_ARG_INT, partition )) < 0 )
    {
	
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( sc_command( e, subch, msg, msg, &len ) ) {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( e, subch );

    /* check response */
    if( sc_interpret_resp( msg, 0 ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }
    
    return( 0 );
}

int elsc_partition_get(elsc_t *e)
{
    char msg[BRL1_QSIZE];       /* L1 request/response info */
    int subch;                  /* system controller subchannel used */
    int len;                    /* length of message */
    uint32_t partition_id;    /* used to copy partition id out of msg */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    if( (subch = sc_open( e, L1_ADDR_LOCAL )) < 0 ) {
	return ELSC_ERROR_CMD_SEND;
    }

    if( (len = sc_construct_msg( e, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_PARTITION_GET, 0 )) < 0 )

    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( sc_command( e, subch, msg, msg, &len ) ) {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( e, subch );

    /* check response */
    if( sc_interpret_resp( msg, 2, L1_ARG_INT, &partition_id ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }
    
    return( partition_id );
}


/*
 * elsc_cons_subch selects the "active" console subchannel for this node
 * (i.e., the one that will currently receive input)
 */
int elsc_cons_subch(elsc_t *e, uint ch)
{
    char msg[BRL1_QSIZE];       /* L1 request/response info */
    int subch;                  /* system controller subchannel used */
    int len;                    /* length of message */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    subch = sc_open( e, L1_ADDR_LOCAL );
    
    if( (len = sc_construct_msg( e, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_CONS_SUBCH, 2,
				 L1_ARG_INT, ch)) < 0 )
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( SC_COMMAND( e, subch, msg, msg, &len ) ) {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( e, subch );

    /* check response */
    if( sc_interpret_resp( msg, 0 ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    return 0;
}


/*
 * elsc_cons_node should only be executed by one node.  It declares to
 * the system controller that the node from which it is called will be
 * the owner of the system console.
 */
int elsc_cons_node(elsc_t *e)
{
    char msg[BRL1_QSIZE];       /* L1 request/response info */
    int subch;                  /* system controller subchannel used */
    int len;                    /* length of message */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    subch = sc_open( e, L1_ADDR_LOCAL );
    
    if( (len = sc_construct_msg( e, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_CONS_NODE, 0 )) < 0 )
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( SC_COMMAND( e, subch, msg, msg, &len ) ) {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( e, subch );

    /* check response */
    if( sc_interpret_resp( msg, 0 ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    return 0;
}
    

/* elsc_display_line writes up to 12 characters to either the top or bottom
 * line of the L1 display.  line points to a buffer containing the message
 * to be displayed.  The zero-based line number is specified by lnum (so
 * lnum == 0 specifies the top line and lnum == 1 specifies the bottom).
 * Lines longer than 12 characters, or line numbers not less than
 * L1_DISPLAY_LINES, cause elsc_display_line to return an error.
 */
int elsc_display_line(elsc_t *e, char *line, int lnum)
{
    char	msg[BRL1_QSIZE];
    int		subch;  /* system controller subchannel used */
    int		len;	/* number of msg buffer bytes used */

    /* argument sanity checking */
    if( !(lnum < L1_DISPLAY_LINES) )
	return( ELSC_ERROR_CMD_ARGS );
    if( !(strlen( line ) <= L1_DISPLAY_LINE_LENGTH) )
	return( ELSC_ERROR_CMD_ARGS );

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    subch = sc_open( (l1sc_t *)e, L1_ADDR_LOCAL );

    if( (len = sc_construct_msg( (l1sc_t *)e, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 (L1_REQ_DISP1+lnum), 2,
				 L1_ARG_ASCII, line )) < 0 )
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( SC_COMMAND( (l1sc_t *)e, subch, msg, msg, &len ) < 0 )
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( (l1sc_t *)e, subch );

    /* check response */
    if( sc_interpret_resp( msg, 0 ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    return 0;
}


/* elsc_display_mesg silently drops message characters beyond the 12th.
 */
int elsc_display_mesg(elsc_t *e, char *chr)
{

    char line[L1_DISPLAY_LINE_LENGTH+1];
    int numlines, i;
    int result;

    numlines = (strlen( chr ) + L1_DISPLAY_LINE_LENGTH - 1) /
	L1_DISPLAY_LINE_LENGTH;

    if( numlines > L1_DISPLAY_LINES )
	numlines = L1_DISPLAY_LINES;

    for( i = 0; i < numlines; i++ )
    {
	strncpy( line, chr, L1_DISPLAY_LINE_LENGTH );
	line[L1_DISPLAY_LINE_LENGTH] = '\0';

	/* generally we want to leave the first line of the L1 display
	 * alone (so the L1 can manipulate it).  If you need to be able
	 * to display to both lines (for debugging purposes), define
	 * L1_DISP_2LINES in irix/kern/ksys/l1.h, or add -DL1_DISP_2LINES
	 * to your 'defs file.
	 */
#if defined(L1_DISP_2LINES)
	if( (result = elsc_display_line( e, line, i )) < 0 )
#else
	if( (result = elsc_display_line( e, line, i+1 )) < 0 )
#endif

	    return result;

	chr += L1_DISPLAY_LINE_LENGTH;
    }
    
    return 0;
}


int elsc_password_set(elsc_t *e, char *password)
{
    /* shush compiler */
    e = e;
    password = password;

    /* fill in buffer with the opcode & params; call elsc_command */

    return 0;
}

int elsc_password_get(elsc_t *e, char *password)
{
    /* shush compiler */
    e = e;
    password = password;

    /* fill in buffer with the opcode & params; call elsc_command */

    return 0;
}


/*
 * sc_portspeed_get
 *
 * retrieve the current portspeed setting for the bedrock II
 */
int sc_portspeed_get(l1sc_t *sc)
{
    char	msg[BRL1_QSIZE];
    int         len;    /* length of message being sent */
    int         subch;  /* system controller subchannel used */
    int		portspeed_a, portspeed_b;
			/* ioport clock rates */

    bzero( msg, BRL1_QSIZE );
    subch = sc_open( sc, L1_ADDR_LOCAL );

    if( (len = sc_construct_msg( sc, subch, msg, BRL1_QSIZE,
                                 L1_ADDR_TASK_GENERAL,
				 L1_REQ_PORTSPEED,
				 0 )) < 0 )
    {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }
    
    /* send the request to the L1 */
    if( sc_command( sc, subch, msg, msg, &len ) < 0 )
    {
        sc_close( sc, subch );
        return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( sc, subch );

    /* check response */
    if( sc_interpret_resp( msg, 4, 
			   L1_ARG_INT, &portspeed_a,
			   L1_ARG_INT, &portspeed_b ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    /* for the c-brick, we ignore the portspeed_b value */
    return (portspeed_a ? 600 : 400);
}

/*
 * elsc_power_query
 *
 *   To be used after system reset, this command returns 1 if the reset
 *   was the result of a power-on, 0 otherwise.
 *
 *   The power query status is cleared to 0 after it is read.
 */

int elsc_power_query(elsc_t *e)
{
    e = e; /* shush the compiler */

    /* fill in buffer with the opcode & params; call elsc_command */

    return 1;
}

int elsc_rpwr_query(elsc_t *e, int is_master)
{
    /* shush the compiler */
    e = e;
    is_master = is_master;

    /* fill in buffer with the opcode & params; call elsc_command */

    return 0;
} 

/*
 * elsc_power_down
 *
 *   Sets up system to shut down in "sec" seconds (or modifies the
 *   shutdown time if one is already in effect).  Use 0 to power
 *   down immediately.
 */

int elsc_power_down(elsc_t *e, int sec)
{
    /* shush compiler */
    e = e;
    sec = sec;

    /* fill in buffer with the opcode & params; call elsc_command */

    return 0;
}


int elsc_system_reset(elsc_t *e)
{
    char	msg[BRL1_QSIZE];
    int		subch;  /* system controller subchannel used */
    int		len;	/* number of msg buffer bytes used */
    int		result;

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    if( (subch = sc_open( e, L1_ADDR_LOCAL )) < 0 ) {
	return ELSC_ERROR_CMD_SEND;
    }

    if( (len = sc_construct_msg( e, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_RESET, 0 )) < 0 )
    {
	sc_close( e, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( (result = sc_command( e, subch, msg, msg, &len )) ) {
	sc_close( e, subch );
	if( result == SC_NMSG ) {
	    /* timeout is OK.  We've sent the reset.  Now it's just
	     * a matter of time...
	     */
	    return( 0 );
	}
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( e, subch );

    /* check response */
    if( sc_interpret_resp( msg, 0 ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    return 0;
}


int elsc_power_cycle(elsc_t *e)
{
    /* shush compiler */
    e = e;

    /* fill in buffer with the opcode & params; call sc_command */

    return 0;
}


/*
 * L1 Support for reading 
 * cbrick uid.
 */

int elsc_nic_get(elsc_t *e, uint64_t *nic, int verbose)
{
    /* this parameter included only for SN0 compatibility */
    verbose = verbose;

    /* We don't go straight to the bedrock/L1 protocol on this one, but let
     * the eeprom layer prepare the eeprom data as we would like it to
     * appear to the caller
     */
    return cbrick_uid_get( e->nasid, nic );
}

int _elsc_hbt(elsc_t *e, int ival, int rdly)
{
    e = e;
    ival = ival;
    rdly = rdly;

    /* fill in buffer with the opcode & params; call elsc_command */

    return 0;
}


/* send a command string to an L1 */
int sc_command_interp( l1sc_t *sc, l1addr_t compt, l1addr_t rack, l1addr_t bay,
		       char *cmd )
{
    char        msg[BRL1_QSIZE];
    int         len;    /* length of message being sent */
    int         subch;  /* system controller subchannel used */
    l1addr_t	target; /* target system controller for command */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    subch = sc_open( sc, L1_ADDR_LOCAL );

    L1_BUILD_ADDR( &target, compt, rack, bay, L1_ADDR_TASK_CMD );
    if( (len = sc_construct_msg( sc, subch, msg, BRL1_QSIZE,
				 target, L1_REQ_EXEC_CMD, 2,
				 L1_ARG_ASCII, cmd )) < 0 )
    {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }
		   
    /* send the request to the L1 */
    if( sc_command( sc, subch, msg, msg, &len ) < 0 )
    {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( sc, subch );
    
    /* check response */
    if( sc_interpret_resp( msg, 0 ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    return 0;
}


/*
 * Routines for reading the R-brick's L1
 */

int router_module_get( nasid_t nasid, net_vec_t path )
{
    uint rnum, rack, bay, t;
    int ret;
    l1sc_t sc;

    /* prepare l1sc_t struct */
    sc_init( &sc, nasid, path );

    /* construct module ID from rack and slot info */

    if ((ret = elsc_rack_bay_get(&sc, &rnum, &bay)) < 0)
	return ret;

    /* report unset location info. with a special, otherwise invalid modid */
    if (rnum == 0 && bay == 0)
	return MODULE_NOT_SET;

    if (bay > MODULE_BPOS_MASK >> MODULE_BPOS_SHFT)
	return ELSC_ERROR_MODULE;

    /* Build a moduleid_t-compatible rack number */

    rack = 0;		
    t = rnum / 100;		/* rack class (CPU/IO) */
    if (t > RACK_CLASS_MASK(rack) >> RACK_CLASS_SHFT(rack))
	return ELSC_ERROR_MODULE;
    RACK_ADD_CLASS(rack, t);
    rnum %= 100;

    t = rnum / 10;		/* rack group */
    if (t > RACK_GROUP_MASK(rack) >> RACK_GROUP_SHFT(rack))
	return ELSC_ERROR_MODULE;
    RACK_ADD_GROUP(rack, t);

    t = rnum % 10;		/* rack number (one-based) */
    if (t-1 > RACK_NUM_MASK(rack) >> RACK_NUM_SHFT(rack))
	return ELSC_ERROR_MODULE;
    RACK_ADD_NUM(rack, t);

    ret = RBT_TO_MODULE(rack, bay, MODULE_RBRICK);
    return ret;
}
    

/*
 * iobrick routines
 */

/* iobrick_rack_bay_type_get fills in the three int * arguments with the
 * rack number, bay number and brick type of the L1 being addressed.  Note
 * that if the L1 operation fails and this function returns an error value, 
 * garbage may be written to brick_type.
 */
int iobrick_rack_bay_type_get( l1sc_t *sc, uint *rack, 
			       uint *bay, uint *brick_type )
{
    char msg[BRL1_QSIZE];       /* L1 request/response info */
    int subch;                  /* system controller subchannel used */
    int len;                    /* length of message */
    uint32_t buf32;	        /* used to copy 32-bit rack & bay out of msg */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    if( (subch = sc_open( sc, L1_ADDR_LOCALIO )) < 0 ) {
	return( ELSC_ERROR_CMD_SEND );
    }

    if( (len = sc_construct_msg( sc, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_RRBT, 0 )) < 0 )
    {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( sc_command( sc, subch, msg, msg, &len ) ) {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( sc, subch );

    /* check response */
    if( sc_interpret_resp( msg, 4, L1_ARG_INT, &buf32, 
			           L1_ARG_INT, brick_type ) < 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    /* extract rack/bay info
     *
     * note that the 32-bit value returned by the L1 actually
     * only uses the low-order sixteen bits for rack and bay
     * information.  A "normal" L1 address puts rack and bay
     * information in bit positions 12 through 28.  So if
     * we initially shift the value returned 12 bits to the left,
     * we can use the L1 addressing #define's to extract the
     * values we need (see ksys/l1.h for a complete list of the
     * various fields of an L1 address).
     */
    buf32 <<= L1_ADDR_BAY_SHFT;

    *rack = (buf32 & L1_ADDR_RACK_MASK) >> L1_ADDR_RACK_SHFT;
    *bay = (buf32 & L1_ADDR_BAY_MASK) >> L1_ADDR_BAY_SHFT;

    return 0;
}


int iobrick_module_get(l1sc_t *sc)
{
    uint rnum, rack, bay, brick_type, t;
    int ret;

    /* construct module ID from rack and slot info */

    if ((ret = iobrick_rack_bay_type_get(sc, &rnum, &bay, &brick_type)) < 0)
        return ret;

    /* report unset location info. with a special, otherwise invalid modid */
    if (rnum == 0 && bay == 0)
        return MODULE_NOT_SET;

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

/* iobrick_get_sys_snum asks the attached iobrick for the system
 * serial number.  This function will only be relevant to the master
 * cbrick (the one attached to the bootmaster ibrick); other nodes
 * may call the function, but the value returned to the master node
 * will be the one used as the system serial number by the kernel.
 */

int
iobrick_get_sys_snum( l1sc_t *sc, char *snum_str )
{
    char msg[BRL1_QSIZE];       /* L1 request/response info */
    int subch;                  /* system controller subchannel used */
    int len;                    /* length of message */
    
    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    if( (subch = sc_open( sc, L1_ADDR_LOCALIO )) < 0 ) {
	return( ELSC_ERROR_CMD_SEND );
    }

    if( (len = sc_construct_msg( sc, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_SYS_SERIAL, 0 )) < 0 )
    {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( sc_command( sc, subch, msg, msg, &len ) ) {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( sc, subch );

    /* check response */
    return( sc_interpret_resp( msg, 2, L1_ARG_ASCII, snum_str ) );
}


/*
 * The following functions apply (or cut off) power to the specified
 * pci bus or slot.
 */

int
iobrick_pci_slot_pwr( l1sc_t *sc, int bus, int slot, int up )
{
    char cmd[BRL1_QSIZE];
    unsigned rack, bay, brick_type;
    if( iobrick_rack_bay_type_get( sc, &rack, &bay, &brick_type ) < 0 )
	return( ELSC_ERROR_CMD_SEND );
    sprintf( cmd, "pci %d %d %s", bus, slot,
	     (up ? "u" : "d") );
    return( sc_command_interp
	    ( sc, L1_ADDR_TYPE_L1, rack, bay, cmd ) );
}

int
iobrick_pci_bus_pwr( l1sc_t *sc, int bus, int up )
{
    char cmd[BRL1_QSIZE];
    unsigned rack, bay, brick_type;
    if( iobrick_rack_bay_type_get( sc, &rack, &bay, &brick_type ) < 0 )
	return( ELSC_ERROR_CMD_SEND );
    sprintf( cmd, "pci %d %s", bus, (up ? "u" : "d") );
    return( sc_command_interp
	    ( sc, L1_ADDR_TYPE_L1, rack, bay, cmd ) );
}


/* get the L1 firmware version for an iobrick */
int
iobrick_sc_version( l1sc_t *sc, char *result )
{
    char	msg[BRL1_QSIZE];
    int		len;    /* length of message being sent */
    int		subch;  /* system controller subchannel used */
    int		major,  /* major rev number */
	        minor,  /* minor rev number */
                bugfix; /* bugfix rev number */

    /* fill in msg with the opcode & params */
    bzero( msg, BRL1_QSIZE );
    subch = sc_open( sc, L1_ADDR_LOCALIO );

    if( (len = sc_construct_msg( sc, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_FW_REV, 0 )) < 0 )
    {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_ARGS );
    }

    /* send the request to the L1 */
    if( SC_COMMAND(sc, subch, msg, msg, &len ) < 0 )
    {
	sc_close( sc, subch );
	return( ELSC_ERROR_CMD_SEND );
    }

    /* free up subchannel */
    sc_close( sc, subch );

    /* check response */
    if( sc_interpret_resp( msg, 6, L1_ARG_INT, &major,
			   L1_ARG_INT, &minor, L1_ARG_INT, &bugfix )
	< 0 )
    {
	return( ELSC_ERROR_RESP_FORMAT );
    }

    sprintf( result, "%d.%d.%d", major, minor, bugfix );

    return 0;
}



/* elscuart routines 
 *
 * Most of the elscuart functionality is implemented in l1.c.  The following
 * is directly "recycled" from elsc.c.
 */


/*
 * _elscuart_puts
 */

int _elscuart_puts(elsc_t *e, char *s)
{
    int			c;

    if (s == 0)
	s = "<NULL>";

    while ((c = LBYTE(s)) != 0) {
	if (_elscuart_putc(e, c) < 0)
	    return -1;
	s++;
    }

    return 0;
}


/*
 * elscuart wrapper routines
 *
 *   The following routines are similar to their counterparts in l1.c,
 *   except instead of taking an elsc_t pointer directly, they call
 *   a global routine "get_elsc" to obtain the pointer.
 *   This is useful when the elsc is employed for stdio.
 */

int elscuart_probe(void)
{
    return _elscuart_probe(get_elsc());
}

void elscuart_init(void *init_data)
{
    _elscuart_init(get_elsc());
    /* dummy variable included for driver compatability */
    init_data = init_data;
}

int elscuart_poll(void)
{
    return _elscuart_poll(get_elsc());
}

int elscuart_readc(void)
{
    return _elscuart_readc(get_elsc());
}

int elscuart_getc(void)
{
    return _elscuart_getc(get_elsc());
}

int elscuart_puts(char *s)
{
    return _elscuart_puts(get_elsc(), s);
}

int elscuart_putc(int c)
{
    return _elscuart_putc(get_elsc(), c);
}

int elscuart_flush(void)
{
    return _elscuart_flush(get_elsc());
}
