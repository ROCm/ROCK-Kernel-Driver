/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */


/*
 * WARNING:     There is more than one copy of this file in different isms.
 *              All copies must be kept exactly in sync.
 *              Do not modify this file without also updating the following:
 *
 *              irix/kern/io/eeprom.c
 *              stand/arcs/lib/libsk/ml/eeprom.c
 *		stand/arcs/lib/libkl/io/eeprom.c
 *
 *      (from time to time they might not be in sync but that's due to bringup
 *       activity - this comment is to remind us that they eventually have to
 *       get back together)
 *
 * eeprom.c
 *
 * access to board-mounted EEPROMs via the L1 system controllers
 *
 */

/**************************************************************************
 *                                                                        *
 *  Copyright (C) 1999 Silicon Graphics, Inc.                             *
 *                                                                        *
 *  These coded instructions, statements, and computer programs  contain  *
 *  unpublished  proprietary  information of Silicon Graphics, Inc., and  *
 *  are protected by Federal copyright law.  They  may  not be disclosed  *
 *  to  third  parties  or copied or duplicated in any form, in whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *                                                                        *
 **************************************************************************
 */


#include <linux/types.h>
#include <linux/config.h>
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
/* #include <sys/SN/SN1/ip27log.h> */
#include <asm/sn/router.h>
#include <asm/sn/module.h>
#include <asm/sn/ksys/l1.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/clksupport.h>

#if defined(EEPROM_DEBUG)
#define db_printf(x) printk x
#else
#define db_printf(x) printk x
#endif

#define BCOPY(x,y,z)	memcpy(y,x,z)

#define UNDERSCORE	0	/* don't convert underscores to hyphens */
#define HYPHEN		1	/* convert underscores to hyphens */

void		copy_ascii_field( char *to, char *from, int length,
			          int change_underscore );
uint64_t	generate_unique_id( char *sn, int sn_len );
uchar_t		char_to_base36( char c );
int		nicify( char *dst, eeprom_brd_record_t *src );
static void	int64_to_hex_string( char *out, uint64_t val );

// extern int router_lock( net_vec_t, int, int );
// extern int router_unlock( net_vec_t );
#define ROUTER_LOCK(p) 	// router_lock(p, 10000, 3000000)
#define ROUTER_UNLOCK(p) 	// router_unlock(p)

#define IP27LOG_OVNIC           "OverrideNIC"


/* the following function converts an EEPROM record to a close facsimile
 * of the string returned by reading a Dallas Semiconductor NIC (see
 * one of the many incarnations of nic.c for details on that driver)
 */
int nicify( char *dst, eeprom_brd_record_t *src )
{
    int field_len;
    uint64_t unique_id;
    char *cur_dst = dst;
    eeprom_board_ia_t   *board;

    board   = src->board_ia;
    ASSERT( board );  /* there should always be a board info area */

    /* copy part number */
    strcpy( cur_dst, "Part:" );
    cur_dst += strlen( cur_dst );
    ASSERT( (board->part_num_tl & FIELD_FORMAT_MASK)
	    == FIELD_FORMAT_ASCII );
    field_len = board->part_num_tl & FIELD_LENGTH_MASK;
    copy_ascii_field( cur_dst, board->part_num, field_len, HYPHEN );
    cur_dst += field_len;

    /* copy product name */
    strcpy( cur_dst, ";Name:" );
    cur_dst += strlen( cur_dst );
    ASSERT( (board->product_tl & FIELD_FORMAT_MASK) == FIELD_FORMAT_ASCII );
    field_len = board->product_tl & FIELD_LENGTH_MASK;
    copy_ascii_field( cur_dst, board->product, field_len, UNDERSCORE );
    cur_dst += field_len;

    /* copy serial number */
    strcpy( cur_dst, ";Serial:" );
    cur_dst += strlen( cur_dst );
    ASSERT( (board->serial_num_tl & FIELD_FORMAT_MASK)
	    == FIELD_FORMAT_ASCII );
    field_len = board->serial_num_tl & FIELD_LENGTH_MASK;
    copy_ascii_field( cur_dst, board->serial_num, field_len,
		      HYPHEN);

    cur_dst += field_len;

    /* copy revision */
    strcpy( cur_dst, ";Revision:");
    cur_dst += strlen( cur_dst );
    ASSERT( (board->board_rev_tl & FIELD_FORMAT_MASK)
	    == FIELD_FORMAT_ASCII );
    field_len = board->board_rev_tl & FIELD_LENGTH_MASK;
    copy_ascii_field( cur_dst, board->board_rev, field_len, HYPHEN );
    cur_dst += field_len;

    /* EEPROMs don't have equivalents for the Group, Capability and
     * Variety fields, so we pad these with 0's
     */
    strcpy( cur_dst, ";Group:ff;Capability:ffffffff;Variety:ff" );
    cur_dst += strlen( cur_dst );

    /* use the board serial number to "fake" a laser id */
    strcpy( cur_dst, ";Laser:" );
    cur_dst += strlen( cur_dst );
    unique_id = generate_unique_id( board->serial_num,
				    board->serial_num_tl & FIELD_LENGTH_MASK );
    int64_to_hex_string( cur_dst, unique_id );
    strcat( dst, ";" );

    return 1;
}


/* These functions borrow heavily from chars2* in nic.c
 */
void copy_ascii_field( char *to, char *from, int length,
		       int change_underscore )
{
    int i;
    for( i = 0; i < length; i++ ) {

	/* change underscores to hyphens if requested */
	if( from[i] == '_' && change_underscore == HYPHEN )
	    to[i] = '-';

	/* ; and ; are separators, so mustn't appear within
	 * a field */
	else if( from[i] == ':' || from[i] == ';' )
	    to[i] = '?';

	/* I'm not sure why or if ASCII character 0xff would
	 * show up in an EEPROM field, but the NIC parsing
	 * routines wouldn't like it if it did... so we
	 * get rid of it, just in case. */
	else if( (unsigned char)from[i] == (unsigned char)0xff )
	    to[i] = ' ';
	
	/* unprintable characters are replaced with . */
	else if( from[i] < ' ' || from[i] >= 0x7f )
	    to[i] = '.';

	/* otherwise, just copy the character */
	else
	    to[i] = from[i];
    }

    if( i == 0 ) {
	to[i] = ' '; /* return at least a space... */
	i++;
    }
    to[i] = 0;	     /* terminating null */
}

/* Note that int64_to_hex_string currently only has a big-endian
 * implementation.
 */
#ifdef _MIPSEB
static void int64_to_hex_string( char *out, uint64_t val )
{
    int i;
    uchar_t table[] = "0123456789abcdef";
    uchar_t *byte_ptr = (uchar_t *)&val;
    for( i = 0; i < sizeof(uint64_t); i++ ) {
	out[i*2] = table[ ((*byte_ptr) >> 4) & 0x0f ];
	out[i*2+1] = table[ (*byte_ptr) & 0x0f ];
	byte_ptr++;
    }
    out[i*2] = '\0';
}

#else /* little endian */

static void int64_to_hex_string( char *out, uint64_t val )
{


	printk("int64_to_hex_string needs a little-endian implementation.\n");
}
#endif /* _MIPSEB */

/* Convert a standard ASCII serial number to a unique integer
 * id number by treating the serial number string as though
 * it were a base 36 number
 */
uint64_t generate_unique_id( char *sn, int sn_len )
{
    int uid = 0;
    int i;

    #define VALID_BASE36(c)	((c >= '0' && c <='9') \
			    ||   (c >= 'A' && c <='Z') \
			    ||   (c >= 'a' && c <='z'))

    for( i = 0; i < sn_len; i++ ) {
	if( !VALID_BASE36(sn[i]) )
	    continue;
	uid *= 36;
	uid += char_to_base36( sn[i] );
    }

    if( uid == 0 )
	return rtc_time();

    return uid;
}

uchar_t char_to_base36( char c )
{
    uchar_t val;

    if( c >= '0' && c <= '9' )
	val = (c - '0');

    else if( c >= 'A' && c <= 'Z' )
	val = (c - 'A' + 10);

    else if( c >= 'a' && c <= 'z' )
	val = (c - 'a' + 10);

    else val = 0;

    return val;
}


/* given a pointer to the three-byte little-endian EEPROM representation
 * of date-of-manufacture, this function translates to a big-endian
 * integer format
 */
int eeprom_xlate_board_mfr_date( uchar_t *src )
{
    int rval = 0;
    rval += *src; src++;
    rval += ((int)(*src) << 8); src ++;
    rval += ((int)(*src) << 16);
    return rval;
}


int eeprom_str( char *nic_str, nasid_t nasid, int component )
{
    eeprom_brd_record_t eep;
    eeprom_board_ia_t board;
    eeprom_chassis_ia_t chassis;
    int r;

    if( (component & C_DIMM) == C_DIMM ) {
	/* this function isn't applicable to DIMMs */
	return EEP_PARAM;
    }
    else {
	eep.board_ia = &board;
	eep.spd = NULL;
	if( !(component & SUBORD_MASK) )
	    eep.chassis_ia = &chassis;  /* only main boards have a chassis
					 * info area */
	else
	    eep.chassis_ia = NULL;
    }
    
    switch( component & BRICK_MASK ) {
      case C_BRICK:
	r = cbrick_eeprom_read( &eep, nasid, component );
	break;
      case IO_BRICK:
	r = iobrick_eeprom_read( &eep, nasid, component );
	break;
      default:
	return EEP_PARAM;  /* must be an invalid component */
    }
    if( r )
	return r;
    if( !nicify( nic_str, &eep ) )
	return EEP_NICIFY;

    return EEP_OK;
}

int vector_eeprom_str( char *nic_str, nasid_t nasid,
		       int component, net_vec_t path )
{
    eeprom_brd_record_t eep;
    eeprom_board_ia_t board;
    eeprom_chassis_ia_t chassis;
    int r;

    eep.board_ia = &board;
    if( !(component & SUBORD_MASK) )
        eep.chassis_ia = &chassis;  /* only main boards have a chassis
                                     * info area */
    else
        eep.chassis_ia = NULL;

    if( !(component & VECTOR) )
	return EEP_PARAM;

    if( (r = vector_eeprom_read( &eep, nasid, path, component )) )
	return r;

    if( !nicify( nic_str, &eep ) )
        return EEP_NICIFY;

    return EEP_OK;
}


int is_iobrick( int nasid, int widget_num )
{
    uint32_t wid_reg;
    int part_num, mfg_num;

    /* Read the widget's WIDGET_ID register to get
     * its part number and mfg number
     */
    wid_reg = *(volatile int32_t *)
        (NODE_SWIN_BASE( nasid, widget_num ) + WIDGET_ID);

    part_num = (wid_reg & WIDGET_PART_NUM) >> WIDGET_PART_NUM_SHFT;
    mfg_num = (wid_reg & WIDGET_MFG_NUM) >> WIDGET_MFG_NUM_SHFT;

    /* Is this the "xbow part" of an XBridge?  If so, this
     * widget is definitely part of an I/O brick.
     */
    if( part_num == XXBOW_WIDGET_PART_NUM &&
	mfg_num == XXBOW_WIDGET_MFGR_NUM )

	return 1;

    /* Is this a "bridge part" of an XBridge?  If so, once
     * again, we know this widget is part of an I/O brick.
     */
    if( part_num == XBRIDGE_WIDGET_PART_NUM &&
	mfg_num == XBRIDGE_WIDGET_MFGR_NUM )

	return 1;

    return 0;
}


int cbrick_uid_get( nasid_t nasid, uint64_t *uid )
{
#if !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
    return EEP_L1;
#else
    char uid_str[32];
    char msg[BRL1_QSIZE];
    int subch, len;
    l1sc_t sc;
    l1sc_t *scp;
    int local = (nasid == get_nasid());

    if ( IS_RUNNING_ON_SIMULATOR() )
	return EEP_L1;

    /* If the promlog variable pointed to by IP27LOG_OVNIC is set,
     * use that value for the cbrick UID rather than the EEPROM
     * serial number.
     */
#ifdef LOG_GETENV
    if( ip27log_getenv( nasid, IP27LOG_OVNIC, uid_str, NULL, 0 ) >= 0 )
    {
	/* We successfully read IP27LOG_OVNIC, so return it as the UID. */
	db_printf(( "cbrick_uid_get:"
		    "Overriding UID with environment variable %s\n", 
		    IP27LOG_OVNIC ));
	*uid = strtoull( uid_str, NULL, 0 );
	return EEP_OK;
    }
#endif

    /* If this brick is retrieving its own uid, use the local l1sc_t to
     * arbitrate access to the l1; otherwise, set up a new one.
     */
    if( local ) {
	scp = get_l1sc();
    }
    else {
	scp = &sc;
	sc_init( &sc, nasid, BRL1_LOCALUART );
    }

    /* fill in msg with the opcode & params */
    BZERO( msg, BRL1_QSIZE );
    if( (subch = sc_open( scp, L1_ADDR_LOCAL )) < 0 )
	return EEP_L1;

    if( (len = sc_construct_msg( scp, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_SER_NUM, 0 )) < 0 )
    {
	sc_close( scp, subch );
	return( EEP_L1 );
    }

    /* send the request to the L1 */
    if( sc_command( scp, subch, msg, msg, &len ) ) {
	sc_close( scp, subch );
	return( EEP_L1 );
    }

    /* free up subchannel */
    sc_close(scp, subch);

    /* check response */
    if( sc_interpret_resp( msg, 2, L1_ARG_ASCII, uid_str ) < 0 )
    {
	return( EEP_L1 );
    }

    *uid = generate_unique_id( uid_str, strlen( uid_str ) );

    return EEP_OK;
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}


int rbrick_uid_get( nasid_t nasid, net_vec_t path, uint64_t *uid )
{
#if !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
    return EEP_L1;
#else
    char uid_str[32];
    char msg[BRL1_QSIZE];
    int subch, len;
    l1sc_t sc;

    if ( IS_RUNNING_ON_SIMULATOR() )
	return EEP_L1;

#ifdef BRINGUP
#define FAIL								\
    {									\
	*uid = rtc_time();						\
	printk( "rbrick_uid_get failed; using current time as uid\n" );	\
	return EEP_OK;							\
    }
#endif /* BRINGUP */

    ROUTER_LOCK(path);
    sc_init( &sc, nasid, path );

    /* fill in msg with the opcode & params */
    BZERO( msg, BRL1_QSIZE );
    if( (subch = sc_open( &sc, L1_ADDR_LOCAL )) < 0 ) {
	ROUTER_UNLOCK(path);
	FAIL;
    }

    if( (len = sc_construct_msg( &sc, subch, msg, BRL1_QSIZE,
				 L1_ADDR_TASK_GENERAL,
				 L1_REQ_SER_NUM, 0 )) < 0 )
    {
	ROUTER_UNLOCK(path);
	sc_close( &sc, subch );
	FAIL;
    }

    /* send the request to the L1 */
    if( sc_command( &sc, subch, msg, msg, &len ) ) {
	ROUTER_UNLOCK(path);
	sc_close( &sc, subch );
	FAIL;
    }

    /* free up subchannel */
    ROUTER_UNLOCK(path);
    sc_close(&sc, subch);

    /* check response */
    if( sc_interpret_resp( msg, 2, L1_ARG_ASCII, uid_str ) < 0 )
    {
	FAIL;
    }

    *uid = generate_unique_id( uid_str, strlen( uid_str ) );

    return EEP_OK;
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}

int iobrick_uid_get( nasid_t nasid, uint64_t *uid )
{
    eeprom_brd_record_t eep;
    eeprom_board_ia_t board;
    eeprom_chassis_ia_t chassis;
    int r;

    eep.board_ia = &board;
    eep.chassis_ia = &chassis;
    eep.spd = NULL;

    r = iobrick_eeprom_read( &eep, nasid, IO_BRICK );
    if( r != EEP_OK ) {
        *uid = rtc_time();
        return r;
    }

    *uid = generate_unique_id( board.serial_num,
                               board.serial_num_tl & FIELD_LENGTH_MASK );

    return EEP_OK;
}


int ibrick_mac_addr_get( nasid_t nasid, char *eaddr )
{
    eeprom_brd_record_t eep;
    eeprom_board_ia_t board;
    eeprom_chassis_ia_t chassis;
    int r;
    char *tmp;

    eep.board_ia = &board;
    eep.chassis_ia = &chassis;
    eep.spd = NULL;

    r = iobrick_eeprom_read( &eep, nasid, IO_BRICK );
    if( (r != EEP_OK) || (board.mac_addr[0] == '\0') ) {
	db_printf(( "ibrick_mac_addr_get: "
		    "Couldn't read MAC address from EEPROM\n" ));
	return EEP_L1;
    }
    else {
	/* successfully read info area */
	int ix;
	tmp = board.mac_addr;
	for( ix = 0; ix < (board.mac_addr_tl & FIELD_LENGTH_MASK); ix++ )
	{
	    *eaddr++ = *tmp++;
	}
	*eaddr = '\0';
    }

    return EEP_OK;
}


/* 
 * eeprom_vertex_info_set
 *
 * Given a vertex handle, a component designation, a starting nasid
 * and (in the case of a router) a vector path to the component, this
 * function will read the EEPROM and attach the resulting information
 * to the vertex in the same string format as that provided by the
 * Dallas Semiconductor NIC drivers.  If the vertex already has the
 * string, this function just returns the string.
 */

extern char *nic_vertex_info_get( devfs_handle_t );
extern void nic_vmc_check( devfs_handle_t, char * );
#ifdef BRINGUP
/* the following were lifted from nic.c - change later? */
#define MAX_INFO 2048
#define NEWSZ(ptr,sz)   ((ptr) = kern_malloc((sz)))
#define DEL(ptr) (kern_free((ptr)))
#endif /* BRINGUP */

char *eeprom_vertex_info_set( int component, int nasid, devfs_handle_t v,
                              net_vec_t path )
{
        char *info_tmp;
        int info_len;
        char *info;

        /* see if this vertex is already marked */
        info_tmp = nic_vertex_info_get(v);
        if (info_tmp) return info_tmp;

        /* get a temporary place for the data */
        NEWSZ(info_tmp, MAX_INFO);
        if (!info_tmp) return NULL;

        /* read the EEPROM */
	if( component & R_BRICK ) {
	    if( RBRICK_EEPROM_STR( info_tmp, nasid, path ) != EEP_OK )
		return NULL;
	}
	else {
            if( eeprom_str( info_tmp, nasid, component ) != EEP_OK )
	        return NULL;
	}

        /* allocate a smaller final place */
        info_len = strlen(info_tmp)+1;
        NEWSZ(info, info_len);
        if (info) {
                strcpy(info, info_tmp);
                DEL(info_tmp);
        } else {
                info = info_tmp;
        }

        /* add info to the vertex */
        hwgraph_info_add_LBL(v, INFO_LBL_NIC,
                             (arbitrary_info_t) info);

        /* see if someone else got there first */
        info_tmp = nic_vertex_info_get(v);
        if (info != info_tmp) {
            DEL(info);
            return info_tmp;
        }

        /* export the data */
        hwgraph_info_export_LBL(v, INFO_LBL_NIC, info_len);

        /* trigger all matching callbacks */
        nic_vmc_check(v, info);

        return info;
}


/*********************************************************************
 *
 * stubs for use until the Bedrock/L1 link is available
 *
 */

#include <asm/sn/nic.h>

/* #define EEPROM_TEST */

/* fake eeprom reading functions (replace when the BR/L1 communication
 * channel is in working order)
 */


/* generate a charater in [0-9A-Z]; if an "extra" character is
 * specified (such as '_'), include it as one of the possibilities.
 */
char random_eeprom_ch( char extra ) 
{
    char ch;
    int modval = 36;
    if( extra )
	modval++;
    
    ch = rtc_time() % modval;

    if( ch < 10 )
        ch += '0';
    else if( ch >= 10 && ch < 36 )
	ch += ('A' - 10);
    else
	ch = extra;

    return ch;
}

/* create a part number of the form xxx-xxxx-xxx.
 * It may be important later to generate different
 * part numbers depending on the component we're
 * supposed to be "reading" from, so the component
 * paramter is provided.
 */
void fake_a_part_number( char *buf, int component )
{
    int i;
    switch( component ) {

    /* insert component-specific routines here */

    case C_BRICK:
	strcpy( buf, "030-1266-001" );
	break;
    default:
        for( i = 0; i < 12; i++ ) {
	    if( i == 3 || i == 8 )
	        buf[i] = '-';
	    else
	        buf[i] = random_eeprom_ch(0);
        }
    }
}


/* create a six-character serial number */
void fake_a_serial_number( char *buf, uint64_t ser )
{
    int i;
    static const char hexchars[] = "0123456789ABCDEF";

    if (ser) {
	for( i = 5; i >=0; i-- ) {
	    buf[i] = hexchars[ser & 0xf];
	    ser >>= 4;
	}
    }
    else {
	for( i = 0; i < 6; i++ )
	    buf[i] = random_eeprom_ch(0);
    }
}


void fake_a_product_name( uchar_t *format, char* buf, int component )
{
    switch( component & BRICK_MASK ) {

    case C_BRICK:
	if( component & SUBORD_MASK ) {
	    strcpy( buf, "C_BRICK_SUB" );
	    *format = 0xCB;
	}
	else {
	    strcpy( buf, "IP35" );
	    *format = 0xC4;
	}
	break;

    case R_BRICK:
        if( component & SUBORD_MASK ) {
            strcpy( buf, "R_BRICK_SUB" );
            *format = 0xCB;
        }
        else {
            strcpy( buf, "R_BRICK" );
            *format = 0xC7;
        }
        break;

    case IO_BRICK:
        if( component & SUBORD_MASK ) {
            strcpy( buf, "IO_BRICK_SUB" );
            *format = 0xCC;
        }
        else {
            strcpy( buf, "IO_BRICK" );
            *format = 0xC8;
        }
        break;

    default:
	strcpy( buf, "UNK_DEVICE" );
	*format = 0xCA;
    }
}



int fake_an_eeprom_record( eeprom_brd_record_t *buf, int component, 
			   uint64_t ser )
{
    eeprom_board_ia_t *board;
    eeprom_chassis_ia_t *chassis;
    int i, cs;

    board = buf->board_ia;
    chassis = buf->chassis_ia;

    if( !(component & SUBORD_MASK) ) {
	if( !chassis )
	    return EEP_PARAM;
	chassis->format = 0;
	chassis->length = 5;
	chassis->type = 0x17;

	chassis->part_num_tl = 0xCC;
	fake_a_part_number( chassis->part_num, component );
	chassis->serial_num_tl = 0xC6;
	fake_a_serial_number( chassis->serial_num, ser );

	cs = chassis->format + chassis->length + chassis->type
	    + chassis->part_num_tl + chassis->serial_num_tl;
	for( i = 0; i < (chassis->part_num_tl & FIELD_LENGTH_MASK); i++ )
	    cs += chassis->part_num[i];
	for( i = 0; i < (chassis->serial_num_tl & FIELD_LENGTH_MASK); i++ )
	    cs += chassis->serial_num[i];
	chassis->checksum = 256 - (cs % 256);
    }

    if( !board )
	return EEP_PARAM;
    board->format = 0;
    board->length = 10;
    board->language = 0;
    board->mfg_date = 1789200; /* noon, 5/26/99 */
    board->manuf_tl = 0xC3;
    strcpy( board->manuf, "SGI" );

    fake_a_product_name( &(board->product_tl), board->product, component );

    board->serial_num_tl = 0xC6;
    fake_a_serial_number( board->serial_num, ser );

    board->part_num_tl = 0xCC;
    fake_a_part_number( board->part_num, component );

    board->board_rev_tl = 0xC2;
    board->board_rev[0] = '0';
    board->board_rev[1] = '1';

    board->eeprom_size_tl = 0x01;
    board->eeprom_size = 1;

    board->temp_waiver_tl = 0xC2;
    board->temp_waiver[0] = '0';
    board->temp_waiver[1] = '1';

    cs = board->format + board->length + board->language
	+ (board->mfg_date & 0xFF)
	+ (board->mfg_date & 0xFF00)
	+ (board->mfg_date & 0xFF0000)
	+ board->manuf_tl + board->product_tl + board->serial_num_tl
	+ board->part_num_tl + board->board_rev_tl
	+ board->board_rev[0] + board->board_rev[1]
	+ board->eeprom_size_tl + board->eeprom_size + board->temp_waiver_tl
	+ board->temp_waiver[0] + board->temp_waiver[1];
    for( i = 0; i < (board->manuf_tl & FIELD_LENGTH_MASK); i++ )
	cs += board->manuf[i];
    for( i = 0; i < (board->product_tl & FIELD_LENGTH_MASK); i++ )
	cs += board->product[i];
    for( i = 0; i < (board->serial_num_tl & FIELD_LENGTH_MASK); i++ )
	cs += board->serial_num[i];
    for( i = 0; i < (board->part_num_tl & FIELD_LENGTH_MASK); i++ )
	cs += board->part_num[i];
    
    board->checksum = 256 - (cs % 256);

    return EEP_OK;
}

#define EEPROM_CHUNKSIZE	64

#if defined(EEPROM_DEBUG)
#define RETURN_ERROR							\
{									\
    printk( "read_ia error return, component 0x%x, line %d"		\
	    ", address 0x%x, ia code 0x%x\n",				\
	    l1_compt, __LINE__, sc->subch[subch].target, ia_code );	\
    return EEP_L1;							\
}

#else
#define RETURN_ERROR	return(EEP_L1)
#endif

int read_ia( l1sc_t *sc, int subch, int l1_compt, 
	     int ia_code, char *eep_record )
{
#if !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
    return EEP_L1;
#else
    char msg[BRL1_QSIZE]; 	   /* message buffer */
    int len;              	   /* number of bytes used in message buffer */
    int ia_len = EEPROM_CHUNKSIZE; /* remaining bytes in info area */
    int offset = 0;                /* current offset into info area */

    if ( IS_RUNNING_ON_SIMULATOR() )
	return EEP_L1;

    BZERO( msg, BRL1_QSIZE );

    /* retrieve EEPROM data in 64-byte chunks
     */

    while( ia_len )
    {
	/* fill in msg with opcode & params */
	if( (len = sc_construct_msg( sc, subch, msg, BRL1_QSIZE,
				     L1_ADDR_TASK_GENERAL,
				     L1_REQ_EEPROM, 8,
				     L1_ARG_INT, l1_compt,
				     L1_ARG_INT, ia_code,
				     L1_ARG_INT, offset,
				     L1_ARG_INT, ia_len )) < 0 )
	{
	    RETURN_ERROR;
	}

	/* send the request to the L1 */

	if( sc_command( sc, subch, msg, msg, &len ) ) {
	    RETURN_ERROR;
	}

	/* check response */
	if( sc_interpret_resp( msg, 5, 
			       L1_ARG_INT, &ia_len,
			       L1_ARG_UNKNOWN, &len, eep_record ) < 0 )
	{
	    RETURN_ERROR;
	}

	if( ia_len > EEPROM_CHUNKSIZE )
	    ia_len = EEPROM_CHUNKSIZE;

	eep_record += EEPROM_CHUNKSIZE;
	offset += EEPROM_CHUNKSIZE;
    }

    return EEP_OK;
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}


int read_spd( l1sc_t *sc, int subch, int l1_compt,
	      eeprom_spd_u *spd )
{
#if !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
    return EEP_L1;
#else
    char msg[BRL1_QSIZE]; 	    /* message buffer */
    int len;              	    /* number of bytes used in message buffer */
    int spd_len = EEPROM_CHUNKSIZE; /* remaining bytes in spd record */
    int offset = 0;		    /* current offset into spd record */
    char *spd_p = spd->bytes;	    /* "thumb" for writing to spd */

    if ( IS_RUNNING_ON_SIMULATOR() )
	return EEP_L1;

    BZERO( msg, BRL1_QSIZE );

    /* retrieve EEPROM data in 64-byte chunks
     */

    while( spd_len )
    {
	/* fill in msg with opcode & params */
	if( (len = sc_construct_msg( sc, subch, msg, BRL1_QSIZE,
				     L1_ADDR_TASK_GENERAL,
				     L1_REQ_EEPROM, 8,
				     L1_ARG_INT, l1_compt,
				     L1_ARG_INT, L1_EEP_SPD,
				     L1_ARG_INT, offset,
				     L1_ARG_INT, spd_len )) < 0 )
	{
	    return( EEP_L1 );
	}

	/* send the request to the L1 */
	if( sc_command( sc, subch, msg, msg, &len ) ) {
	    return( EEP_L1 );
	}

	/* check response */
	if( sc_interpret_resp( msg, 5, 
			       L1_ARG_INT, &spd_len,
			       L1_ARG_UNKNOWN, &len, spd_p ) < 0 )
	{
	    return( EEP_L1 );
	}

	if( spd_len > EEPROM_CHUNKSIZE )
	    spd_len = EEPROM_CHUNKSIZE;

	spd_p += EEPROM_CHUNKSIZE;
	offset += EEPROM_CHUNKSIZE;
    }
    return EEP_OK;
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}


int read_chassis_ia( l1sc_t *sc, int subch, int l1_compt,
		     eeprom_chassis_ia_t *ia )
{
    char eep_record[512];          /* scratch area for building up info area */
    char *eep_rec_p = eep_record;  /* thumb for moving through eep_record */
    int checksum = 0;              /* use to verify eeprom record checksum */
    int i;

    /* Read in info area record from the L1.
     */
    if( read_ia( sc, subch, l1_compt, L1_EEP_CHASSIS, eep_record )
	!= EEP_OK )
    {
	return EEP_L1;
    }

    /* Now we've got the whole info area.  Transfer it to the data structure.
     */

    eep_rec_p = eep_record;
    ia->format = *eep_rec_p++;
    ia->length = *eep_rec_p++;
    if( ia->length == 0 ) {
	/* since we're using 8*ia->length-1 as an array index later, make
	 * sure it's sane.
	 */
	db_printf(( "read_chassis_ia: eeprom length byte of ZERO\n" ));
	return EEP_L1;
    }
    ia->type = *eep_rec_p++;
   
    ia->part_num_tl = *eep_rec_p++;

    (void)BCOPY( eep_rec_p, ia->part_num, (ia->part_num_tl & FIELD_LENGTH_MASK) );
    eep_rec_p += (ia->part_num_tl & FIELD_LENGTH_MASK);

    ia->serial_num_tl = *eep_rec_p++;

    BCOPY( eep_rec_p, ia->serial_num, 
	   (ia->serial_num_tl & FIELD_LENGTH_MASK) );
    eep_rec_p += (ia->serial_num_tl & FIELD_LENGTH_MASK);

    ia->checksum = eep_record[(8 * ia->length) - 1];

    /* verify checksum */
    eep_rec_p = eep_record;
    checksum = 0;
    for( i = 0; i < (8 * ia->length); i++ ) {
	checksum += *eep_rec_p++;
    }

    if( (checksum & 0xff) != 0 )
    {
	db_printf(( "read_chassis_ia: bad checksum\n" ));
	db_printf(( "read_chassis_ia: target 0x%x  uart 0x%x\n",
			   sc->subch[subch].target, sc->uart ));
	return EEP_BAD_CHECKSUM;
    }

    return EEP_OK;
}


int read_board_ia( l1sc_t *sc, int subch, int l1_compt,
		   eeprom_board_ia_t *ia )
{
    char eep_record[512];          /* scratch area for building up info area */
    char *eep_rec_p = eep_record;  /* thumb for moving through eep_record */
    int checksum = 0;              /* running checksum total */
    int i;

    BZERO( ia, sizeof( eeprom_board_ia_t ) );

    /* Read in info area record from the L1.
     */
    if( read_ia( sc, subch, l1_compt, L1_EEP_BOARD, eep_record )
	!= EEP_OK )
    {
	db_printf(( "read_board_ia: error reading info area from L1\n" ));
	return EEP_L1;
    }

     /* Now we've got the whole info area.  Transfer it to the data structure.
      */

    eep_rec_p = eep_record;
    ia->format = *eep_rec_p++;
    ia->length = *eep_rec_p++;
    if( ia->length == 0 ) {
	/* since we're using 8*ia->length-1 as an array index later, make
	 * sure it's sane.
	 */
	db_printf(( "read_board_ia: eeprom length byte of ZERO\n" ));
	return EEP_L1;
    }
    ia->language = *eep_rec_p++;
    
    ia->mfg_date = eeprom_xlate_board_mfr_date( (uchar_t *)eep_rec_p );
    eep_rec_p += 3;

    ia->manuf_tl = *eep_rec_p++;
    
    BCOPY( eep_rec_p, ia->manuf, (ia->manuf_tl & FIELD_LENGTH_MASK) );
    eep_rec_p += (ia->manuf_tl & FIELD_LENGTH_MASK);

    ia->product_tl = *eep_rec_p++;
    
    BCOPY( eep_rec_p, ia->product, (ia->product_tl & FIELD_LENGTH_MASK) );
    eep_rec_p += (ia->product_tl & FIELD_LENGTH_MASK);

    ia->serial_num_tl = *eep_rec_p++;
    
    BCOPY(eep_rec_p, ia->serial_num, (ia->serial_num_tl & FIELD_LENGTH_MASK));
    eep_rec_p += (ia->serial_num_tl & FIELD_LENGTH_MASK);

    ia->part_num_tl = *eep_rec_p++;

    BCOPY( eep_rec_p, ia->part_num, (ia->part_num_tl & FIELD_LENGTH_MASK) );
    eep_rec_p += (ia->part_num_tl & FIELD_LENGTH_MASK);

    eep_rec_p++; /* we do not use the FRU file id */
    
    ia->board_rev_tl = *eep_rec_p++;
    
    BCOPY( eep_rec_p, ia->board_rev, (ia->board_rev_tl & FIELD_LENGTH_MASK) );
    eep_rec_p += (ia->board_rev_tl & FIELD_LENGTH_MASK);

    ia->eeprom_size_tl = *eep_rec_p++;
    ia->eeprom_size = *eep_rec_p++;

    ia->temp_waiver_tl = *eep_rec_p++;
    
    BCOPY( eep_rec_p, ia->temp_waiver, 
	   (ia->temp_waiver_tl & FIELD_LENGTH_MASK) );
    eep_rec_p += (ia->temp_waiver_tl & FIELD_LENGTH_MASK);

    /* if there's more, we must be reading a main board; get
     * additional fields
     */
    if( ((unsigned char)*eep_rec_p != (unsigned char)EEPROM_EOF) ) {

	ia->ekey_G_tl = *eep_rec_p++;
	BCOPY( eep_rec_p, (char *)&ia->ekey_G, 
	       ia->ekey_G_tl & FIELD_LENGTH_MASK );
	eep_rec_p += (ia->ekey_G_tl & FIELD_LENGTH_MASK);
	
	ia->ekey_P_tl = *eep_rec_p++;
	BCOPY( eep_rec_p, (char *)&ia->ekey_P, 
	       ia->ekey_P_tl & FIELD_LENGTH_MASK );
	eep_rec_p += (ia->ekey_P_tl & FIELD_LENGTH_MASK);
	
	ia->ekey_Y_tl = *eep_rec_p++;
	BCOPY( eep_rec_p, (char *)&ia->ekey_Y, 
	       ia->ekey_Y_tl & FIELD_LENGTH_MASK );
	eep_rec_p += (ia->ekey_Y_tl & FIELD_LENGTH_MASK);
	
	/* 
	 * need to get a couple more fields if this is an I brick 
	 */
	if( ((unsigned char)*eep_rec_p != (unsigned char)EEPROM_EOF) ) {

	    ia->mac_addr_tl = *eep_rec_p++;
	    BCOPY( eep_rec_p, ia->mac_addr, 
		   ia->mac_addr_tl & FIELD_LENGTH_MASK );
	    eep_rec_p += (ia->mac_addr_tl & FIELD_LENGTH_MASK);
	    
	    ia->ieee1394_cfg_tl = *eep_rec_p++;
	    BCOPY( eep_rec_p, ia->ieee1394_cfg,
		   ia->ieee1394_cfg_tl & FIELD_LENGTH_MASK );
	    
	}
    }

    ia->checksum = eep_record[(ia->length * 8) - 1];

    /* verify checksum */
    eep_rec_p = eep_record;
    checksum = 0;
    for( i = 0; i < (8 * ia->length); i++ ) {
	checksum += *eep_rec_p++;
    }

    if( (checksum & 0xff) != 0 )
    {
	db_printf(( "read_board_ia: bad checksum\n" ));
	db_printf(( "read_board_ia: target 0x%x  uart 0x%x\n",
		    sc->subch[subch].target, sc->uart ));
	return EEP_BAD_CHECKSUM;
    }

    return EEP_OK;
}


int _cbrick_eeprom_read( eeprom_brd_record_t *buf, l1sc_t *scp,
			 int component )
{
#if !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
    return EEP_L1;
#else
    int r;
    uint64_t uid = 0;
    char uid_str[32];
    int l1_compt, subch;

    if ( IS_RUNNING_ON_SIMULATOR() )
	return EEP_L1;

    /* make sure we're targeting a cbrick */
    if( !(component & C_BRICK) )
	return EEP_PARAM;

    /* If the promlog variable pointed to by IP27LOG_OVNIC is set,
     * use that value for the cbrick UID rather than the EEPROM
     * serial number.
     */
#ifdef LOG_GETENV
    if( ip27log_getenv( scp->nasid, IP27LOG_OVNIC, uid_str, "0", 0 ) >= 0 )
    {
	db_printf(( "_cbrick_eeprom_read: "
		    "Overriding UID with environment variable %s\n", 
		    IP27LOG_OVNIC ));
	uid = strtoull( uid_str, NULL, 0 );
    }
#endif

    if( (subch = sc_open( scp, L1_ADDR_LOCAL )) < 0 )
	return EEP_L1;

    switch( component )
    {
      case C_BRICK:
	/* c-brick motherboard */
	l1_compt = L1_EEP_NODE;
	r = read_chassis_ia( scp, subch, l1_compt, buf->chassis_ia );
	if( r != EEP_OK ) {
	    sc_close( scp, subch );
	    db_printf(( "_cbrick_eeprom_read: using a fake eeprom record\n" ));
	    return fake_an_eeprom_record( buf, component, uid );
	}
	if( uid ) {
	    /* If IP27LOG_OVNIC is set, we want to put that value
	     * in as our UID. */
	    fake_a_serial_number( buf->chassis_ia->serial_num, uid );
	    buf->chassis_ia->serial_num_tl = 6;
	}
	break;

      case C_PIMM:
	/* one of the PIMM boards */
	l1_compt = L1_EEP_PIMM( component & COMPT_MASK );
	break;

      case C_DIMM:
	/* one of the DIMMs */
	l1_compt = L1_EEP_DIMM( component & COMPT_MASK );
	r = read_spd( scp, subch, l1_compt, buf->spd );
	sc_close( scp, subch );
	return r;

      default:
	/* unsupported board type */
	sc_close( scp, subch );
	return EEP_PARAM;
    }
	      
    r = read_board_ia( scp, subch, l1_compt, buf->board_ia );
    sc_close( scp, subch );
    if( r != EEP_OK ) 
    {
	db_printf(( "_cbrick_eeprom_read: using a fake eeprom record\n" ));
	return fake_an_eeprom_record( buf, component, uid );
    }
    return EEP_OK;
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}


int cbrick_eeprom_read( eeprom_brd_record_t *buf, nasid_t nasid,
    		        int component )
{
#if !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
    return EEP_L1;
#else
    l1sc_t *scp;
    int local = (nasid == get_nasid());

    if ( IS_RUNNING_ON_SIMULATOR() )
	return EEP_L1;

    /* If this brick is retrieving its own uid, use the local l1sc_t to
     * arbitrate access to the l1; otherwise, set up a new one (prom) or
     * use an existing remote l1sc_t (kernel)
     */
    if( local ) {
	scp = get_l1sc();
    }
    else {
	elsc_t *get_elsc(void);
	scp = get_elsc();
    }

    return _cbrick_eeprom_read( buf, scp, component );
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}


int iobrick_eeprom_read( eeprom_brd_record_t *buf, nasid_t nasid,
			 int component )
{
#if !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
    return EEP_L1;
#else
    int r;
    int l1_compt, subch;
    l1sc_t *scp;
    int local = (nasid == get_nasid());

    if ( IS_RUNNING_ON_SIMULATOR() )
	return EEP_L1;

    /* make sure we're talking to an applicable brick */
    if( !(component & IO_BRICK) ) {
	return EEP_PARAM;
    }

    /* If we're talking to this c-brick's attached io brick, use
     * the local l1sc_t; otherwise, set up a new one (prom) or
     * use an existing remote l1sc_t (kernel)
     */
    if( local ) {
	scp = get_l1sc();
    }
    else {
	elsc_t *get_elsc(void);
	scp = get_elsc();
    }

    if( (subch = sc_open( scp, L1_ADDR_LOCALIO )) < 0 )
	return EEP_L1;


    switch( component )
    {
      case IO_BRICK:
	/* IO brick motherboard */
	l1_compt = L1_EEP_LOGIC;
	r = read_chassis_ia( scp, subch, l1_compt, buf->chassis_ia );

	if( r != EEP_OK ) {
	    sc_close( scp, subch );
#ifdef BRINGUP /* Once EEPROMs are universally available, remove this */
	    r = fake_an_eeprom_record( buf, component, rtc_time() );
#endif /* BRINGUP */
	    return r;
	}
	break;

      case IO_POWER:
	/* IO brick power board */
	l1_compt = L1_EEP_POWER;
	break;

      default:
	/* unsupported board type */
	sc_close( scp, subch );
	return EEP_PARAM;
    }

    r = read_board_ia( scp, subch, l1_compt, buf->board_ia );
    sc_close( scp, subch );
    if( r != EEP_OK ) {
	return r;
    }
    return EEP_OK;
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */    
}


int vector_eeprom_read( eeprom_brd_record_t *buf, nasid_t nasid,
			net_vec_t path, int component )
{
#if !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
    return EEP_L1;
#else
    int r;
    uint64_t uid = 0;
    int l1_compt, subch;
    l1sc_t sc;

    if ( IS_RUNNING_ON_SIMULATOR() )
	return EEP_L1;

    /* make sure we're targeting an applicable brick */
    if( !(component & VECTOR) )
	return EEP_PARAM;

    switch( component & BRICK_MASK )
    {
      case R_BRICK:
	ROUTER_LOCK( path );
	sc_init( &sc, nasid, path );

	if( (subch = sc_open( &sc, L1_ADDR_LOCAL )) < 0 )
	{
	    db_printf(( "vector_eeprom_read: couldn't open subch\n" ));
	    ROUTER_UNLOCK(path);
	    return EEP_L1;
	}
	switch( component )
	{
	  case R_BRICK:
	    /* r-brick motherboard */
	    l1_compt = L1_EEP_LOGIC;
    	    r = read_chassis_ia( &sc, subch, l1_compt, buf->chassis_ia );
	    if( r != EEP_OK ) {
		sc_close( &sc, subch );
		ROUTER_UNLOCK( path );
		printk( "vector_eeprom_read: couldn't get rbrick eeprom info;"
			" using current time as uid\n" );
		uid = rtc_time();
		db_printf(("vector_eeprom_read: using a fake eeprom record\n"));
		return fake_an_eeprom_record( buf, component, uid );
	    }
	    break;

	  case R_POWER:
	    /* r-brick power board */
	    l1_compt = L1_EEP_POWER;
	    break;

	  default:
	    /* unsupported board type */
	    sc_close( &sc, subch );
	    ROUTER_UNLOCK( path );
	    return EEP_PARAM;
	}
	r = read_board_ia( &sc, subch, l1_compt, buf->board_ia );
	sc_close( &sc, subch );
	ROUTER_UNLOCK( path );
	if( r != EEP_OK ) {
	    db_printf(( "vector_eeprom_read: using a fake eeprom record\n" ));
	    return fake_an_eeprom_record( buf, component, uid );
	}
	return EEP_OK;

      case C_BRICK:
	sc_init( &sc, nasid, path );
	return _cbrick_eeprom_read( buf, &sc, component );

      default:
	/* unsupported brick type */
	return EEP_PARAM;
    }
#endif /* CONFIG_SERIAL_SGI_L1_PROTOCOL */
}
