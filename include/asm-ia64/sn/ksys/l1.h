/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#ifndef _ASM_SN_KSYS_L1_H
#define _ASM_SN_KSYS_L1_H

#include <asm/sn/vector.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn1/bedrock.h>

#define BRL1_QSIZE	128	/* power of 2 is more efficient */
#define BRL1_BUFSZ	264	/* needs to be large enough
				 * to hold 2 flags, escaped
				 * CRC, type/subchannel byte,
				 * and escaped payload
				 */

#define BRL1_IQS          32
#define BRL1_OQS          4


typedef struct sc_cq_s {
    u_char              buf[BRL1_QSIZE];
    int                 ipos, opos, tent_next;
} sc_cq_t;

/* An l1sc_t struct can be associated with the local (C-brick) L1 or an L1
 * on an R-brick.  In the R-brick case, the l1sc_t records a vector path
 * to the R-brick's junk bus UART.  In the C-brick case, we just use the
 * following flag to denote the local uart.
 *
 * This value can't be confused with a network vector because the least-
 * significant nibble of a network vector cannot be greater than 8.
 */
#define BRL1_LOCALUART	((net_vec_t)0xf)

/* L1<->Bedrock reserved subchannels */

/* console channels */
#define SC_CONS_CPU0    0x00
#define SC_CONS_CPU1    0x01
#define SC_CONS_CPU2    0x02
#define SC_CONS_CPU3    0x03

#define L1_ELSCUART_SUBCH(p)	(p)
#define L1_ELSCUART_CPU(ch)	(ch)

#define SC_CONS_SYSTEM  CPUS_PER_NODE

/* mapping subchannels to queues */
#define MAP_IQ(s)       (s)
#define MAP_OQ(s)       (s)
     
#define BRL1_NUM_SUBCHANS 32
#define BRL1_CMD_SUBCH	  16
#define BRL1_EVENT_SUBCH  (BRL1_NUM_SUBCHANS - 1)
#define BRL1_SUBCH_RSVD   0
#define BRL1_SUBCH_FREE   (-1)

/* constants for L1 hwgraph vertex info */
#define CBRICK_L1	(__psint_t)1
#define IOBRICK_L1	(__psint_t)2
#define RBRICK_L1	(__psint_t)3


struct l1sc_s;     
typedef void (*brl1_notif_t)(struct l1sc_s *, int);
typedef int  (*brl1_uartf_t)(struct l1sc_s *);

/* structure for controlling a subchannel */
typedef struct brl1_sch_s {
    int		use;		/* if this subchannel is free,
				 * use == BRL1_SUBCH_FREE */
    uint	target;		/* type, rack and slot of component to
				 * which this subchannel is directed */
    int		packet_arrived; /* true if packet arrived on
				 * this subchannel */
    sc_cq_t *	iqp;		/* input queue for this subchannel */
    sv_t	arrive_sv;	/* used to wait for a packet */
    lock_t	data_lock;	/* synchronize access to input queues and
				 * other fields of the brl1_sch_s struct */
    brl1_notif_t tx_notify;     /* notify higher layer that transmission may 
				 * continue */
    brl1_notif_t rx_notify;	/* notify higher layer that a packet has been
				 * received */
} brl1_sch_t;

/* br<->l1 protocol states */
#define BRL1_IDLE	0
#define BRL1_FLAG	1
#define BRL1_HDR	2
#define BRL1_BODY	3
#define BRL1_ESC	4
#define BRL1_RESET	7


#ifndef _LANGUAGE_ASSEMBLY

/*
 * l1sc_t structure-- tracks protocol state, open subchannels, etc.
 */
typedef struct l1sc_s {
    nasid_t	 nasid;		/* nasid with which this instance
				 * of the structure is associated */
    moduleid_t	 modid;         /* module id of this brick */
    u_char	 verbose;	/* non-zero if elscuart routines should
				 * prefix output */
    net_vec_t    uart;		/* vector path to UART, or BRL1_LOCALUART */
    int		 sent;		/* number of characters sent */
    int		 send_len;	/* number of characters in send buf */
    brl1_uartf_t putc_f;	/* pointer to UART putc function */
    brl1_uartf_t getc_f;	/* pointer to UART getc function */

    lock_t	 send_lock;	/* arbitrates send synchronization */
    lock_t	 recv_lock;	/* arbitrates uart receive access */
    lock_t	 subch_lock;	/* arbitrates subchannel allocation */
    cpuid_t	 intr_cpu;	/* cpu that receives L1 interrupts */

    u_char	 send_in_use;	/* non-zero if send buffer contains an
				 * unsent or partially-sent  packet */
    u_char	 fifo_space;	/* current depth of UART send FIFO */

    u_char	 brl1_state;	/* current state of the receive side */
    u_char	 brl1_last_hdr;	/* last header byte received */

    char	 send[BRL1_BUFSZ]; /* send buffer */

    int		 sol;		/* "start of line" (see elscuart routines) */
    int		 cons_listen;	/* non-zero if the elscuart interface should
				 * also check the system console subchannel */
    brl1_sch_t	 subch[BRL1_NUM_SUBCHANS];
    				/* subchannels provided by link */

    sc_cq_t	 garbage_q;	/* a place to put unsolicited packets */
    sc_cq_t	 oq[BRL1_OQS];	/* elscuart output queues */

} l1sc_t;


/* error codes */
#define BRL1_VALID	  0
#define BRL1_FULL_Q	(-1)
#define BRL1_CRC	(-2)
#define BRL1_PROTOCOL	(-3)
#define BRL1_NO_MESSAGE	(-4)
#define BRL1_LINK	(-5)
#define BRL1_BUSY	(-6)

#define SC_SUCCESS      BRL1_VALID
#define SC_NMSG         BRL1_NO_MESSAGE
#define SC_BUSY         BRL1_BUSY
#define SC_NOPEN        (-7)
#define SC_BADSUBCH     (-8)
#define SC_TIMEDOUT	(-9)
#define SC_NSUBCH	(-10)


/* L1 Target Addresses */
/*
 * L1 commands and responses use source/target addresses that are
 * 32 bits long.  These are broken up into multiple bitfields that
 * specify the type of the target controller (could actually be L2
 * L3, not just L1), the rack and bay of the target, and the task
 * id (L1 functionality is divided into several independent "tasks"
 * that can each receive command requests and transmit responses)
 */
#define L1_ADDR_TYPE_SHFT	28
#define L1_ADDR_TYPE_MASK	0xF0000000
#define L1_ADDR_TYPE_L1		0x00	/* L1 system controller */
#define L1_ADDR_TYPE_L2		0x01	/* L2 system controller */
#define L1_ADDR_TYPE_L3		0x02	/* L3 system controller */
#define L1_ADDR_TYPE_CBRICK	0x03	/* attached C brick	*/
#define L1_ADDR_TYPE_IOBRICK	0x04	/* attached I/O brick	*/

#define L1_ADDR_RACK_SHFT	18
#define L1_ADDR_RACK_MASK	0x0FFC0000
#define	L1_ADDR_RACK_LOCAL	0x3ff	/* local brick's rack	*/

#define L1_ADDR_BAY_SHFT	12
#define L1_ADDR_BAY_MASK	0x0003F000
#define	L1_ADDR_BAY_LOCAL	0x3f	/* local brick's bay	*/

#define L1_ADDR_TASK_SHFT	0
#define L1_ADDR_TASK_MASK	0x0000001F
#define L1_ADDR_TASK_INVALID	0x00	/* invalid task 	*/
#define	L1_ADDR_TASK_IROUTER	0x01	/* iRouter		*/
#define L1_ADDR_TASK_SYS_MGMT	0x02	/* system management port */
#define L1_ADDR_TASK_CMD	0x03	/* command interpreter	*/
#define L1_ADDR_TASK_ENV	0x04	/* environmental monitor */
#define L1_ADDR_TASK_BEDROCK	0x05	/* bedrock		*/
#define L1_ADDR_TASK_GENERAL	0x06	/* general requests	*/

#define L1_ADDR_LOCAL				\
    (L1_ADDR_TYPE_L1 << L1_ADDR_TYPE_SHFT) |	\
    (L1_ADDR_RACK_LOCAL << L1_ADDR_RACK_SHFT) |	\
    (L1_ADDR_BAY_LOCAL << L1_ADDR_BAY_SHFT)

#define L1_ADDR_LOCALIO					\
    (L1_ADDR_TYPE_IOBRICK << L1_ADDR_TYPE_SHFT) |	\
    (L1_ADDR_RACK_LOCAL << L1_ADDR_RACK_SHFT) |		\
    (L1_ADDR_BAY_LOCAL << L1_ADDR_BAY_SHFT)

#define L1_ADDR_LOCAL_SHFT	L1_ADDR_BAY_SHFT

/* response argument types */
#define L1_ARG_INT		0x00	/* 4-byte integer (big-endian)	*/
#define L1_ARG_ASCII		0x01	/* null-terminated ASCII string */
#define L1_ARG_UNKNOWN		0x80	/* unknown data type.  The low
					 * 7 bits will contain the data
					 * length.			*/

/* response codes */
#define L1_RESP_OK	    0	/* no problems encountered      */
#define L1_RESP_IROUTER	(-  1)	/* iRouter error	        */
#define L1_RESP_ARGC	(-100)	/* arg count mismatch	        */
#define L1_RESP_REQC	(-101)	/* bad request code	        */
#define L1_RESP_NAVAIL	(-104)	/* requested data not available */
#define L1_RESP_ARGVAL	(-105)  /* arg value out of range       */

/* L1 general requests */

/* request codes */
#define	L1_REQ_RDBG		0x0001	/* read debug switches	*/
#define L1_REQ_RRACK		0x0002	/* read brick rack & bay */
#define L1_REQ_RRBT		0x0003  /* read brick rack, bay & type */
#define L1_REQ_SER_NUM		0x0004  /* read brick serial number */
#define L1_REQ_FW_REV		0x0005  /* read L1 firmware revision */
#define L1_REQ_EEPROM		0x0006  /* read EEPROM info */
#define L1_REQ_EEPROM_FMT	0x0007  /* get EEPROM data format & size */
#define L1_REQ_SYS_SERIAL	0x0008	/* read system serial number */
#define L1_REQ_PARTITION_GET	0x0009	/* read partition id */
#define L1_REQ_PORTSPEED	0x000a	/* get ioport speed */

#define L1_REQ_CONS_SUBCH	0x1002  /* select this node's console 
					 * subchannel */
#define L1_REQ_CONS_NODE	0x1003  /* volunteer to be the master 
					 * (console-hosting) node */
#define L1_REQ_DISP1		0x1004  /* write line 1 of L1 display */
#define L1_REQ_DISP2		0x1005  /* write line 2 of L1 display */
#define L1_REQ_PARTITION_SET	0x1006	/* set partition id */
#define L1_REQ_EVENT_SUBCH	0x1007	/* set the subchannel for system
					   controller event transmission */

#define L1_REQ_RESET		0x2001	/* request a full system reset */

/* L1 command interpreter requests */

/* request codes */
#define L1_REQ_EXEC_CMD		0x0000	/* interpret and execute an ASCII
					   command string */


/* brick type response codes */
#define L1_BRICKTYPE_C	0x43
#define L1_BRICKTYPE_I	0x49
#define L1_BRICKTYPE_P	0x50
#define L1_BRICKTYPE_R  0x52
#define L1_BRICKTYPE_X  0x58

/* EEPROM codes (for the "read EEPROM" request) */
/* c brick */
#define L1_EEP_NODE		0x00	/* node board */
#define L1_EEP_PIMM0		0x01
#define L1_EEP_PIMM(x)		(L1_EEP_PIMM0+(x))
#define L1_EEP_DIMM0		0x03
#define L1_EEP_DIMM(x)		(L1_EEP_DIMM0+(x))

/* other brick types */
#define L1_EEP_POWER		0x00	/* power board */
#define L1_EEP_LOGIC		0x01	/* logic board */

/* info area types */
#define L1_EEP_CHASSIS		1	/* chassis info area */
#define L1_EEP_BOARD		2	/* board info area */
#define L1_EEP_IUSE		3	/* internal use area */
#define L1_EEP_SPD		4	/* serial presence detect record */

typedef uint32_t l1addr_t;

#define L1_BUILD_ADDR(addr,at,r,s,t)					\
    (*(l1addr_t *)(addr) = ((l1addr_t)(at) << L1_ADDR_TYPE_SHFT) |	\
			     ((l1addr_t)(r)  << L1_ADDR_RACK_SHFT) |	\
			     ((l1addr_t)(s)  << L1_ADDR_BAY_SHFT) |	\
			     ((l1addr_t)(t)  << L1_ADDR_TASK_SHFT))

#define L1_ADDRESS_TO_TASK(addr,trb,tsk)				\
    (*(l1addr_t *)(addr) = (l1addr_t)(trb) |				\
    			     ((l1addr_t)(tsk) << L1_ADDR_TASK_SHFT))


#define L1_DISPLAY_LINE_LENGTH	12	/* L1 display characters/line */

#ifdef L1_DISP_2LINES
#define L1_DISPLAY_LINES	2	/* number of L1 display lines */
#else
#define L1_DISPLAY_LINES	1	/* number of L1 display lines available
					 * to system software */
#endif

#define SC_EVENT_CLASS_MASK ((unsigned short)0xff00)

#define bzero(d, n)	memset((d), 0, (n))

/* public interfaces to L1 system controller */

int	sc_open( l1sc_t *sc, uint target );
int	sc_close( l1sc_t *sc, int ch );
int	sc_construct_msg( l1sc_t *sc, int ch, 
			  char *msg, int msg_len,
			  uint addr_task, short req_code,
			  int req_nargs, ... );
int	sc_interpret_resp( char *resp, int resp_nargs, ... );
int	sc_send( l1sc_t *sc, int ch, char *msg, int len, int wait );
int	sc_recv( l1sc_t *sc, int ch, char *msg, int *len, uint64_t block );
int	sc_command( l1sc_t *sc, int ch, char *cmd, char *resp, int *len );
int	sc_command_kern( l1sc_t *sc, int ch, char *cmd, char *resp, int *len );
int	sc_poll( l1sc_t *sc, int ch );
void	sc_init( l1sc_t *sc, nasid_t nasid, net_vec_t uart );
void	sc_intr_enable( l1sc_t *sc );

#if 0
int	sc_portspeed_get( l1sc_t *sc );
#endif

int	l1_cons_poll( l1sc_t *sc );
int	l1_cons_getc( l1sc_t *sc );
void	l1_cons_init( l1sc_t *sc );
int	l1_cons_read( l1sc_t *sc, char *buf, int avail );
int	l1_cons_write( l1sc_t *sc, char *msg, int len, int wait );
void	l1_cons_tx_notif( l1sc_t *sc, brl1_notif_t func );
void	l1_cons_rx_notif( l1sc_t *sc, brl1_notif_t func );

int	_elscuart_putc( l1sc_t *sc, int c );
int	_elscuart_getc( l1sc_t *sc );
int	_elscuart_poll( l1sc_t *sc );
int	_elscuart_readc( l1sc_t *sc );
int	_elscuart_flush( l1sc_t *sc );
int	_elscuart_probe( l1sc_t *sc );
void	_elscuart_init( l1sc_t *sc );
void	elscuart_syscon_listen( l1sc_t *sc );

int	elsc_rack_bay_get(l1sc_t *e, uint *rack, uint *bay);
int	elsc_rack_bay_type_get(l1sc_t *e, uint *rack, 
			       uint *bay, uint *brick_type);
int	elsc_cons_subch(l1sc_t *e, uint ch);
int	elsc_cons_node(l1sc_t *e);
int	elsc_display_line(l1sc_t *e, char *line, int lnum);

extern l1sc_t *get_elsc( void );
extern void    set_elsc( l1sc_t *e );

#define get_l1sc	get_elsc
#define set_l1sc(e)	set_elsc(e)

#define get_master_l1sc get_l1sc

int	router_module_get( nasid_t nasid, net_vec_t path );

int	iobrick_rack_bay_type_get( l1sc_t *sc, uint *rack,
				   uint *bay, uint *brick_type );
int	iobrick_module_get( l1sc_t *sc );
int	iobrick_pci_slot_pwr( l1sc_t *sc, int bus, int slot, int up );
int	iobrick_pci_bus_pwr( l1sc_t *sc, int bus, int up );
int	iobrick_sc_version( l1sc_t *sc, char *result );


#endif /* !_LANGUAGE_ASSEMBLY */
#endif /* _ASM_SN_KSYS_L1_H */
