/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: ib_legacy_types.h 36 2004-04-09 05:36:54Z roland $
*/

#ifndef _IB_LEGACY_TYPES_H
#define _IB_LEGACY_TYPES_H

/*
 * #define section
 */
#ifdef DEBUG
# ifndef PRIVATE
#  define PRIVATE
# endif
# ifndef PUBLIC
#  define PUBLIC
# endif
#else
# ifndef PRIVATE
#  define PRIVATE	static
# endif
# ifndef PUBLIC
#  define PUBLIC
# endif
#endif

#ifndef NULL
#define NULL	0
#endif

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

#ifndef STATIC
#define	STATIC	static
#endif

#define TS_EOF         TRUE
#define TS_NOT_EOF     FALSE


/* Misc min, max, size values */
#define MAX_IP_ADDR_LEN_IN_BYTE              4
#define MAX_IP_ADDR_LEN_IN_WORD              2
#define MAX_ETHER_ADDR_LEN_IN_BYTE           6
#define MAX_ETHER_ADDR_LEN_IN_WORD           3
#define MAX_LOGICAL_PORTS_IN_BITS            128 / 8
#define TS_CLI_MAX_CONTEXT_BUFSIZE           64
#define GUID_LEN_IN_BYTE                     8
#define GUID_LEN_IN_WORD                     4
#define GID_LEN_IN_BYTE                      16
#define GID_LEN_IN_WORD                      8
#define MAX_IPOIB_ADDR_LEN_IN_BYTE           26
#define MAX_IPOIB_NOGID_ADDR_LEN_IN_BYTE     10
#define MAX_IPOIB_HW_ADDR_LEN_IN_BYTE        19

/*
 * Low-level values used to distinguish between devices.
 * (In-general, your code should not reference these
 * values directly).
 */
#if defined(TS_ppc440_lt) || defined(TS_ppc440_lt_sim)
#define MAX_CC_PER_SHELF                1
#define MAX_CPU_PER_SLOT                1
#define MAX_FRU_PER_SHELF               0
#define MAX_SLOT_PER_SHELF              2
#define MAX_PORT_PER_SLOT               12
#elif defined(TS_ppc440_en_sun) || defined(TS_ppc440_fc_sun) || defined(TS_ppc440_fg_sun)
#define MAX_CC_PER_SHELF                1
#define MAX_CPU_PER_SLOT                1
#define MAX_FRU_PER_SHELF               0
#define MAX_SLOT_PER_SHELF              1
#define MAX_PORT_PER_SLOT               12  /* including internal ib ports */
#elif defined(TS_ppc440_270sc)
#define MAX_CC_PER_SHELF                2
#define MAX_CPU_PER_SLOT                1
#define MAX_FRU_PER_SHELF               0
#define MAX_SLOT_PER_SHELF              17
#define MAX_PORT_PER_SLOT               12
#elif defined(TS_ppc440_120sc)
#define MAX_CC_PER_SHELF                1
#define MAX_CPU_PER_SLOT                1
#define MAX_FRU_PER_SHELF               4
#define MAX_SLOT_PER_SHELF              1
#define MAX_PORT_PER_SLOT               24
#elif defined(TS_ppc440_bldsc)
#define MAX_CC_PER_SHELF                1
#define MAX_CPU_PER_SLOT                1
#define MAX_FRU_PER_SHELF               0
#define MAX_SLOT_PER_SHELF              1
#define MAX_PORT_PER_SLOT               24
#elif defined(TS_i386)
#define MAX_CC_PER_SHELF                2  /* simulation target */
#define MAX_CPU_PER_SLOT                1
#define MAX_FRU_PER_SHELF               0
#define MAX_SLOT_PER_SHELF              16
#define MAX_PORT_PER_SLOT               12
#else
#define MAX_CC_PER_SHELF                2  /* default target */
#define MAX_CPU_PER_SLOT                1
#define MAX_FRU_PER_SHELF               0
#define MAX_SLOT_PER_SHELF              16
#define MAX_PORT_PER_SLOT               12
#endif


/*
 * Generic values that are safe to use by all devices
 * and software modules.
 */
#define NUM_FRUS                        MAX_FRU_PER_SHELF

#define MIN_SLOT_NUM                    1
#define MAX_SLOT_NUM                    MAX_SLOT_PER_SHELF

#define MIN_PORT_NUM                    1
#define MAX_PORT_NUM                    MAX_PORT_PER_SLOT

#define MIN_VLAN_NUM                    1
#define MAX_VLAN_NUM                    127

#define MIN_BRIDGE_NUM                  1
#define MAX_BRIDGE_NUM                  127

#define MIN_TRK_NUM                     1
#define MAX_TRK_NUM                     127

#define MIN_GATEWAY_PORT_NUM            1
#define MAX_GATEWAY_PORT_NUM            2

#define GATEWAY_PORT_NUM                0
#define GATEWAY_2_PORT_NUM              63

#if defined(TS_ppc440_lt) || defined(TS_ppc440_lt_sim)
#define FIRST_SWITCH_CARD_SLOT          1
#else
#define FIRST_SWITCH_CARD_SLOT          16
#endif

#define TS_CONTROLLER_CARD_NUMBER       1

#define PKT_TCP_PORT_NUM		(MAX_PORT_NUM + 1)



/*
 * typedef section
 */

/*
 * Common types used by all proprietary TopSpin code (native C types
 * should not be used).
 */
typedef int                     tBOOLEAN;
typedef void                    tVOID;
typedef void*                   tpVOID;
typedef char                    tINT8;
typedef unsigned char           tUINT8;
typedef short                   tINT16;
typedef unsigned short          tUINT16;
typedef int                     tINT32;
typedef unsigned int            tUINT32;

#ifdef W2K_OS // Vipul
typedef __int64                 tINT64;
typedef unsigned __int64        tUINT64;
#else
typedef long long               tINT64;
typedef unsigned long long      tUINT64;
#endif

typedef tUINT32                 tSTG_ID;
typedef tUINT16                 tVLAN_ID;
typedef tUINT8			tSHELF;		/* 1 based value */
typedef tUINT8			tSLOT;		/* 1 based value */
typedef tUINT8			tCPU;		/* 1 based value */
typedef tUINT8			tPORT;
typedef void *                  tPTR;
typedef const void *            tCONST_PTR;
typedef char *                  tSTR;
typedef const char *            tCONST_STR;
typedef tUINT32			tIFINDEX;

typedef tUINT32                 tIB_RKEY;
typedef tUINT16                 tIB_PKEY;
typedef tUINT32                 tIB_QKEY;
typedef tUINT8                  tIB_GUID[8];
typedef tUINT8                  tIB_GID[16];
typedef tUINT8                  tIB_LID[2];
typedef tUINT32                 tIB_QPN;

typedef tIB_GUID *              tpIB_GUID;


/*
 * Generic type for returning pass/fail information back from subroutines
 * Note that this is the *opposite* semantics from BOOLEAN. I.e. a zero
 * (False) indicates success. This is consistent with the VxWorks stds.
 */
typedef enum
{
   TS_FAIL    = -1,
   TS_SUCCESS = 0      /* must be consistant with "OK" defined in */
                       /* rl_rlstddef.h - RAPIDLOGIC */

} tSTATUS;

/*
 * Used to store error codes defined in "all/common/include/error_codes.h"
 */
typedef unsigned int            tERROR_CODE;



/* MAC address */
typedef struct
{
    union
    {
        tUINT8  MacAddrByte[MAX_ETHER_ADDR_LEN_IN_BYTE];
        tUINT16 MacAddrWord[MAX_ETHER_ADDR_LEN_IN_WORD];
    } u;

} tMAC_ADDR;

/* IP v4 address */
typedef struct
{
    union
    {
        tUINT8  IpAddrByte[MAX_IP_ADDR_LEN_IN_BYTE];
        tUINT16 IpAddrWord[MAX_IP_ADDR_LEN_IN_WORD];
        tUINT32 IpAddr;
    } u;

} tIP_ADDR, *tpIP_ADDR;

#define TS_GET_IP_ADDR32(addr) ((addr).u.IpAddr)
#define TS_GET_IP_ADDR_HOST32(addr) ntohl((addr).u.IpAddr)
#define TS_GET_IP_ADDR_NET32(addr) htonl((addr).u.IpAddr)

struct ipoib_struct {
  tIB_GID gid;
  tUINT32 cap_flags_qpn;        /* low 3 bytes=QPN */
} __attribute__ ((packed));


/* IP over IB physical address */
typedef struct ipoib_struct tIPOIB_ADDR, *tpIPOIB_ADDR;


/* Ethernet II Frame Header */
typedef struct EthHdr
{
   tMAC_ADDR   eth_daddr;                                /* off=0 */
   tMAC_ADDR   eth_saddr __attribute__ ((packed));       /* off=6 */
   tUINT16     eth_type __attribute__ ((packed));        /* off=12 */
   tUINT8      eth_data[0] __attribute__ ((packed));     /* off=14 */

} tETH_HDR;

typedef struct valueDescPairSt
{
   tINT32 iValue;
   char* sDesc;
} tValueDescPair, *tpValueDescPair;


/*
 * Table entry status
 */
typedef enum
{
   TS_ENTRY_DESTROY = 0,   /* this is a command not a state */
   TS_ENTRY_STANDBY = 1,
   TS_ENTRY_ACTIVE = 2,
   TS_ENTRY_CREATE = 3    /* this is a command not a state */

} tTS_TBL_ENTRY_STATUS;


#define TS_EN4P1G_NUM_PORTS  6

//#define sim_ppc440_bldsc    // to be commented out

#endif /* _IB_LEGACY_TYPES_H */
