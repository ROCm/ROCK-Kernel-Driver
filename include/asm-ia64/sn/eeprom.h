/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Public interface for reading Atmel EEPROMs via L1 system controllers
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_EEPROM_H
#define _ASM_SN_EEPROM_H

#include <asm/sn/sgi.h>
#include <asm/sn/vector.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/nic.h>

/*
 * The following structures are an implementation of the EEPROM info
 * areas described in the SN1 EEPROM spec and the IPMI FRU Information
 * Storage definition
 */

/* Maximum lengths for EEPROM fields
 */
#define EEPROM_PARTNUM_LEN	20
#define EEPROM_SERNUM_LEN	10
#define	EEPROM_MANUF_NAME_LEN	10
#define EEPROM_PROD_NAME_LEN	14



/* The EEPROM "common header", which contains offsets to the other
 * info areas in the EEPROM
 */
typedef struct eeprom_common_hdr_t
{
    uchar_t	format;		/* common header format byte */
    uchar_t	internal_use;	/* offsets to various info areas */
    uchar_t	chassis;	/*  (in doubleword units)        */
    uchar_t	board;
    uchar_t	product;
    uchar_t	multi_record;
    uchar_t	pad;
    uchar_t	checksum;
} eeprom_common_hdr_t;


/* The chassis (brick) info area 
 */
typedef struct eeprom_chassis_ia_t
{
    uchar_t	format;		/* format byte */
    uchar_t	length;		/* info area length in doublewords */
    uchar_t	type;		/* chassis type (always 0x17 "rack mount") */
    uchar_t	part_num_tl;	/* type/length of part number field */

    char	part_num[EEPROM_PARTNUM_LEN];
    				/* ASCII part number */

    uchar_t	serial_num_tl;	/* type/length of serial number field */

    char	serial_num[EEPROM_SERNUM_LEN];
    				/* ASCII serial number */

    uchar_t	checksum;

} eeprom_chassis_ia_t;


/* The board info area
 */
typedef struct eeprom_board_ia_t
{
    uchar_t       format;         /* format byte */
    uchar_t       length;         /* info area length in doublewords */
    uchar_t	language;	/* language code, always 0x00 "English" */
    int		mfg_date;	/* date & time of manufacture, in minutes
				    since 0:00 1/1/96 */
    uchar_t	manuf_tl;	/* type/length of manufacturer name field */

    char	manuf[EEPROM_MANUF_NAME_LEN];
				/* ASCII manufacturer name */

    uchar_t	product_tl;	/* type/length of product name field */

    char	product[EEPROM_PROD_NAME_LEN];
				/* ASCII product name */

    uchar_t	serial_num_tl;	/* type/length of board serial number */

    char	serial_num[EEPROM_SERNUM_LEN];
				/* ASCII serial number */

    uchar_t	part_num_tl;	/* type/length of board part number */

    char	part_num[EEPROM_PARTNUM_LEN];
				/* ASCII part number */

    /*
     * "custom" fields -- see SN1 EEPROM Spec
     */
    uchar_t	board_rev_tl;	/* type/length of board rev (always 0xC2) */

    char	board_rev[2];	/* ASCII board revision */

    uchar_t	eeprom_size_tl; /* type/length of eeprom size field */
    uchar_t	eeprom_size;	/* size code for eeprom */
    uchar_t	temp_waiver_tl;	/* type/length of temp waiver field (0xC2) */
    char	temp_waiver[2];	/* temp waiver */

    
    /*
     * these fields only appear in main boards' EEPROMs
     */
    uchar_t	ekey_G_tl;	/* type/length of encryption key "G" */
    uint32_t	ekey_G;		/* encryption key "G" */
    uchar_t	ekey_P_tl;	/* type/length of encryption key "P" */
    uint32_t	ekey_P;		/* encryption key "P" */
    uchar_t	ekey_Y_tl;	/* type/length of encryption key "Y" */
    uint32_t	ekey_Y;		/* encryption key "Y" */

    
    /*
     * these fields are used for I bricks only
     */
    uchar_t	mac_addr_tl;	  /* type/length of MAC address */
    char	mac_addr[12];	  /* MAC address */
    uchar_t	ieee1394_cfg_tl;  /* type/length of IEEE 1394 info */
    uchar_t	ieee1394_cfg[32]; /* IEEE 1394 config info */
    

    /*
     * all boards have a checksum
     */
    uchar_t	checksum;

} eeprom_board_ia_t;

/* given a pointer to the three-byte little-endian EEPROM representation
 * of date-of-manufacture, this function translates to a big-endian
 * integer format
 */
int eeprom_xlate_board_mfr_date( uchar_t *src );


/* EEPROM Serial Presence Detect record (used for DIMMs in IP35)
 */
typedef struct eeprom_spd_t
{
    /* 0*/ uchar_t spd_used; /* # of bytes written to serial memory by manufacturer */
    /* 1*/ uchar_t spd_size; /* Total # of bytes of SPD memory device */
    /* 2*/ uchar_t mem_type; /* Fundamental memory type (FPM, EDO, SDRAM..) */
    /* 3*/ uchar_t num_rows; /* # of row addresses on this assembly */
    /* 4*/ uchar_t num_cols; /* # Column Addresses on this assembly */
    /* 5*/ uchar_t mod_rows; /* # Module Rows on this assembly */
    /* 6*/ uchar_t data_width[2]; /* Data Width of this assembly (16b little-endian) */
    /* 8*/ uchar_t volt_if; /* Voltage interface standard of this assembly */
    /* 9*/ uchar_t cyc_time; /* SDRAM Cycle time, CL=X (highest CAS latency) */
    /* A*/ uchar_t acc_time; /* SDRAM Access from Clock (highest CAS latency) */
    /* B*/ uchar_t dimm_cfg; /* DIMM Configuration type (non-parity, ECC) */
    /* C*/ uchar_t refresh_rt; /* Refresh Rate/Type */
    /* D*/ uchar_t prim_width; /* Primary SDRAM Width */
    /* E*/ uchar_t ec_width; /* Error Checking SDRAM width */
    /* F*/ uchar_t min_delay; /* Min Clock Delay Back to Back Random Col Address */
    /*10*/ uchar_t burst_len; /* Burst Lengths Supported */
    /*11*/ uchar_t num_banks; /* # of Banks on Each SDRAM Device */
    /*12*/ uchar_t cas_latencies; /* CAS# Latencies Supported */
    /*13*/ uchar_t cs_latencies; /* CS# Latencies Supported */
    /*14*/ uchar_t we_latencies; /* Write Latencies Supported */
    /*15*/ uchar_t mod_attrib; /* SDRAM Module Attributes */
    /*16*/ uchar_t dev_attrib; /* SDRAM Device Attributes: General */
    /*17*/ uchar_t cyc_time2; /* Min SDRAM Cycle time at CL X-1 (2nd highest CAS latency) */
    /*18*/ uchar_t acc_time2; /* SDRAM Access from Clock at CL X-1 (2nd highest CAS latency) */
    /*19*/ uchar_t cyc_time3; /* Min SDRAM Cycle time at CL X-2 (3rd highest CAS latency) */
    /*1A*/ uchar_t acc_time3; /* Max SDRAM Access from Clock at CL X-2 (3nd highest CAS latency) */
    /*1B*/ uchar_t min_row_prechg; /* Min Row Precharge Time (Trp) */
    /*1C*/ uchar_t min_ra_to_ra; /* Min Row Active to Row Active (Trrd) */
    /*1D*/ uchar_t min_ras_to_cas; /* Min RAS to CAS Delay (Trcd) */
    /*1E*/ uchar_t min_ras_pulse; /* Minimum RAS Pulse Width (Tras) */
    /*1F*/ uchar_t row_density; /* Density of each row on module */
    /*20*/ uchar_t ca_setup; /* Command and Address signal input setup time */
    /*21*/ uchar_t ca_hold; /* Command and Address signal input hold time */
    /*22*/ uchar_t d_setup; /* Data signal input setup time */
    /*23*/ uchar_t d_hold; /* Data signal input hold time */

    /*24*/ uchar_t pad0[26]; /* unused */
    
    /*3E*/ uchar_t data_rev; /* SPD Data Revision Code */
    /*3F*/ uchar_t checksum; /* Checksum for bytes 0-62 */
    /*40*/ uchar_t jedec_id[8]; /* Manufacturer's JEDEC ID code */
    
    /*48*/ uchar_t mfg_loc; /* Manufacturing Location */
    /*49*/ uchar_t part_num[18]; /* Manufacturer's Part Number */

    /*5B*/ uchar_t rev_code[2]; /* Revision Code */

    /*5D*/ uchar_t mfg_date[2]; /* Manufacturing Date */

    /*5F*/ uchar_t ser_num[4]; /* Assembly Serial Number */

    /*63*/ uchar_t manuf_data[27]; /* Manufacturer Specific Data */

    /*7E*/ uchar_t intel_freq; /* Intel specification frequency */
    /*7F*/ uchar_t intel_100MHz; /* Intel spec details for 100MHz support */

} eeprom_spd_t;


#define EEPROM_SPD_RECORD_MAXLEN	256

typedef union eeprom_spd_u
{
    eeprom_spd_t fields;
    char         bytes[EEPROM_SPD_RECORD_MAXLEN];

} eeprom_spd_u;


/* EEPROM board record
 */
typedef struct eeprom_brd_record_t 
{
    eeprom_chassis_ia_t		*chassis_ia;
    eeprom_board_ia_t		*board_ia;
    eeprom_spd_u		*spd;

} eeprom_brd_record_t;


/* End-of-fields marker
 */
#define EEPROM_EOF	        0xc1


/* masks for dissecting the type/length bytes
 */
#define FIELD_FORMAT_MASK       0xc0
#define FIELD_LENGTH_MASK       0x3f


/* field format codes (used in type/length bytes)
 */
#define FIELD_FORMAT_BINARY     0x00 /* binary format */
#define FIELD_FORMAT_BCD        0x40 /* BCD */
#define FIELD_FORMAT_PACKED     0x80 /* packed 6-bit ASCII */
#define FIELD_FORMAT_ASCII      0xC0 /* 8-bit ASCII */




/* codes specifying brick and board type
 */
#define C_BRICK		0x100

#define C_PIMM		(C_BRICK | 0x10)
#define C_PIMM_0	(C_PIMM) /* | 0x0 */
#define C_PIMM_1	(C_PIMM | 0x1)

#define C_DIMM		(C_BRICK | 0x20)
#define C_DIMM_0	(C_DIMM) /* | 0x0 */
#define C_DIMM_1	(C_DIMM | 0x1)
#define C_DIMM_2	(C_DIMM | 0x2)
#define C_DIMM_3	(C_DIMM | 0x3)
#define C_DIMM_4	(C_DIMM | 0x4)
#define C_DIMM_5	(C_DIMM | 0x5)
#define C_DIMM_6	(C_DIMM | 0x6)
#define C_DIMM_7	(C_DIMM | 0x7)

#define R_BRICK		0x200
#define R_POWER		(R_BRICK | 0x10)

#define VECTOR		0x300 /* used in vector ops when the destination
			       * could be a cbrick or an rbrick */

#define IO_BRICK	0x400
#define IO_POWER	(IO_BRICK | 0x10)

#define BRICK_MASK	0xf00
#define SUBORD_MASK	0xf0  /* AND with component specification; if the
			         the result is non-zero, then the component
			         is a subordinate board of some kind */
#define COMPT_MASK	0xf   /* if there's more than one instance of a
				 particular type of subordinate board, this 
				 masks out which one we're talking about */



/* functions & macros for obtaining "NIC-like" strings from EEPROMs
 */

int eeprom_str( char *nic_str, nasid_t nasid, int component );
int vector_eeprom_str( char *nic_str, nasid_t nasid,
		       int component, net_vec_t path );

#define CBRICK_EEPROM_STR(s,n)	eeprom_str((s),(n),C_BRICK)
#define IOBRICK_EEPROM_STR(s,n)	eeprom_str((s),(n),IO_BRICK)
#define RBRICK_EEPROM_STR(s,n,p)  vector_eeprom_str((s),(n),R_BRICK,p)
#define VECTOR_EEPROM_STR(s,n,p)  vector_eeprom_str((s),(n),VECTOR,p)



/* functions for obtaining formatted records from EEPROMs
 */

int cbrick_eeprom_read( eeprom_brd_record_t *buf, nasid_t nasid,
			int component );
int iobrick_eeprom_read( eeprom_brd_record_t *buf, nasid_t nasid,
			 int component );
int vector_eeprom_read( eeprom_brd_record_t *buf, nasid_t nasid,
			net_vec_t path, int component );


/* functions providing unique id's for duplonet and i/o discovery
 */

int cbrick_uid_get( nasid_t nasid, uint64_t *uid );
int rbrick_uid_get( nasid_t nasid, net_vec_t path, uint64_t *uid );
int iobrick_uid_get( nasid_t nasid, uint64_t *uid );


/* retrieve the ethernet MAC address for an I-brick
 */

int ibrick_mac_addr_get( nasid_t nasid, char *eaddr );


/* error codes
 */

#define EEP_OK			0
#define EEP_L1			1
#define EEP_FAIL		2
#define EEP_BAD_CHECKSUM	3
#define EEP_NICIFY		4
#define EEP_PARAM		6
#define EEP_NOMEM		7



/* given a hardware graph vertex and an indication of the brick type,
 * brick and board to be read, this functions reads the eeprom and
 * attaches a "NIC"-format string of manufacturing information to the 
 * vertex.  If the vertex already has the string, just returns the
 * string.  If component is not VECTOR or R_BRICK, the path parameter
 * is ignored.
 */

#ifdef IRIX
char *eeprom_vertex_info_set( int component, int nasid, devfs_handle_t v,
			      net_vec_t path );
#endif



/* We may need to differentiate between an XBridge and other types of
 * bridges during discovery to tell whether the bridge in question
 * is part of an IO brick.  The following function reads the WIDGET_ID
 * register of the bridge under examination and returns a positive value
 * if the part and mfg numbers stored there indicate that this widget
 * is an XBridge (and so must be part of a brick).
 */
#ifdef IRIX
int is_iobrick( int nasid, int widget_num );
#endif

/* the following macro derives the widget number from the register
 * address passed to it and uses is_iobrick to determine whether
 * the widget in question is part of an SN1 IO brick.
 */
#ifdef IRIX
#define IS_IOBRICK(rg)	is_iobrick( NASID_GET((rg)), SWIN_WIDGETNUM((rg)) )
#else
#define IS_IOBRICK(rg)	1
#endif



/* macros for NIC compatability */
/* always invoked on "this" cbrick */
#define HUB_VERTEX_MFG_INFO(v) \
    eeprom_vertex_info_set( C_BRICK, get_nasid(), (v), 0 )

#define BRIDGE_VERTEX_MFG_INFO(v, r) \
    ( IS_IOBRICK((r)) ? eeprom_vertex_info_set \
		          ( IO_BRICK, NASID_GET((r)), (v), 0 ) \
		      : nic_bridge_vertex_info((v), (r)) )

#ifdef BRINGUP /* will we read mfg info from IOC3's that aren't
		* part of IO7 cards, or aren't in I/O bricks? */
#define IOC3_VERTEX_MFG_INFO(v, r, e) \
    eeprom_vertex_info_set( IO_IO7, NASID_GET((r)), (v), 0 )
#endif /* BRINGUP */

#define HUB_UID_GET(n,v,p)	cbrick_uid_get((n),(p))
#define ROUTER_UID_GET(d,p)	rbrick_uid_get(get_nasid(),(d),(p))
#define XBOW_UID_GET(n,p)	iobrick_uid_get((n),(p))

#endif /* _ASM_SN_EEPROM_H */
