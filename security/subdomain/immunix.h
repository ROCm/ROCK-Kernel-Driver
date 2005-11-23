/*======================================================================*/
/*									*/
/*  Immunix CoDomain / CryptoMark Toolkit:				*/
/*  Copyright 2000, 2001 Wirex Communications, Inc. 			*/
/*									*/
/*	Written by Steve Beattie <steve@wirex.net>			*/
/*	and Greg Kroah-Hartman <greg@wirex.com>				*/
/*									*/
/*  linux/include/linux/immunix.h:					*/
/*									*/
/*======================================================================*/

#ifndef _IMMUNIX_H
#define _IMMUNIX_H

#ifdef __KERNEL__
#include <linux/in.h>
#else
#include <netinet/in.h>
#endif

#ifdef __KERNEL__
#include "pcre_exec.h"
#else
#include "pcre/internal.h"
#endif

#define IMMUNIX_SCAFFOLD_VERSION	"0.2.6"

/*======================================================================
 *
 *  SubDomain specific things
 *
 *======================================================================*/

#define KERN_COD_ADD		1
#define KERN_COD_DELETE		2
#define KERN_COD_REPLACE	3

#define SD_ADD_PROFILE		1
#define SD_DELETE_PROFILE	2
#define SD_REPLACE_PROFILE	3
#define SD_DEBUG_PROFILE	4

#define SD_CHANGE_HAT		10

/* $ echo -n subdomain.o | md5sum | cut -c -8 */
#define SD_ID_MAGIC		0x8c235e38

// start of system offsets 
#define POS_KERN_COD_FILE_MIN		0
#define POS_KERN_COD_MAY_EXEC		POS_KERN_COD_FILE_MIN
#define POS_KERN_COD_MAY_WRITE		(POS_KERN_COD_MAY_EXEC + 1)
#define POS_KERN_COD_MAY_READ		(POS_KERN_COD_MAY_WRITE + 1)
// not used by Subdomain
#define POS_KERN_COD_MAY_APPEND		(POS_KERN_COD_MAY_READ + 1)
// end of system offsets 

#define POS_KERN_COD_MAY_LINK		(POS_KERN_COD_MAY_APPEND + 1)
#define POS_KERN_COD_EXEC_INHERIT	(POS_KERN_COD_MAY_LINK + 1)
#define POS_KERN_COD_EXEC_UNCONSTRAINED (POS_KERN_COD_EXEC_INHERIT + 1)
#define POS_KERN_COD_EXEC_PROFILE	(POS_KERN_COD_EXEC_UNCONSTRAINED + 1)
#define POS_KERN_COD_FILE_MAX		POS_KERN_COD_EXEC_PROFILE

#define POS_KERN_COD_NET_MIN		(POS_KERN_COD_FILE_MAX + 1)
#define POS_KERN_COD_TCP_CONNECT	POS_KERN_COD_NET_MIN
#define POS_KERN_COD_TCP_ACCEPT		(POS_KERN_COD_TCP_CONNECT + 1)
#define POS_KERN_COD_TCP_CONNECTED	(POS_KERN_COD_TCP_ACCEPT + 1)
#define POS_KERN_COD_TCP_ACCEPTED	(POS_KERN_COD_TCP_CONNECTED + 1)
#define POS_KERN_COD_UDP_SEND		(POS_KERN_COD_TCP_ACCEPTED + 1)
#define POS_KERN_COD_UDP_RECEIVE	(POS_KERN_COD_UDP_SEND + 1)
#define POS_KERN_COD_NET_MAX		POS_KERN_COD_UDP_RECEIVE

/* logging only */
#define POS_KERN_COD_LOGTCP_SEND	(POS_KERN_COD_NET_MAX + 1)
#define POS_KERN_COD_LOGTCP_RECEIVE	(POS_KERN_COD_LOGTCP_SEND + 1)

/* Absolute MAX/MIN */
#define POS_KERN_COD_MIN		(POS_KERN_COD_FILE_MIN
#define POS_KERN_COD_MAX		(POS_KERN_COD_NET_MAX

/* Modeled after MAY_READ, MAY_WRITE, MAY_EXEC def'ns */
#define KERN_COD_MAY_EXEC    	(0x01 << POS_KERN_COD_MAY_EXEC)
#define KERN_COD_MAY_WRITE   	(0x01 << POS_KERN_COD_MAY_WRITE)
#define KERN_COD_MAY_READ    	(0x01 << POS_KERN_COD_MAY_READ)
#define KERN_COD_MAY_LINK	(0x01 << POS_KERN_COD_MAY_LINK) 
#define KERN_COD_EXEC_INHERIT 	(0x01 << POS_KERN_COD_EXEC_INHERIT)
#define KERN_COD_EXEC_UNCONSTRAINED	(0x01 << POS_KERN_COD_EXEC_UNCONSTRAINED) 
#define KERN_COD_EXEC_PROFILE	(0x01 << POS_KERN_COD_EXEC_PROFILE) 
#define KERN_EXEC_MODIFIERS(X)	(X & (KERN_COD_EXEC_INHERIT | \
				      KERN_COD_EXEC_UNCONSTRAINED | \
				      KERN_COD_EXEC_PROFILE))
/* Network subdomain extensions.  */
#define KERN_COD_TCP_CONNECT    (0x01 << POS_KERN_COD_TCP_CONNECT)
#define KERN_COD_TCP_ACCEPT     (0x01 << POS_KERN_COD_TCP_ACCEPT)
#define KERN_COD_TCP_CONNECTED  (0x01 << POS_KERN_COD_TCP_CONNECTED)
#define KERN_COD_TCP_ACCEPTED   (0x01 << POS_KERN_COD_TCP_ACCEPTED)
#define KERN_COD_UDP_SEND       (0x01 << POS_KERN_COD_UDP_SEND)
#define KERN_COD_UDP_RECEIVE    (0x01 << POS_KERN_COD_UDP_RECEIVE)

#define KERN_COD_LOGTCP_SEND    (0x01 << POS_KERN_COD_LOGTCP_SEND)	
#define KERN_COD_LOGTCP_RECEIVE (0x01 << POS_KERN_COD_LOGTCP_RECEIVE)

#define KERN_COD_HAT_SIZE	975	/* Maximum size of a subdomain
					 * ident (hat) */
#ifdef __KERNEL__
typedef __u32 i_addr;
#else
typedef unsigned int i_addr;
#endif

/* Number of (unsigned) bytes in a digital signature - md5 is 16 bytes */
/* XXX not really used, needed for comilation */
#define IMMUNIX_DIGI_SIG_SIZE	16
struct immunix_digital_sig {
	unsigned char	md5[IMMUNIX_DIGI_SIG_SIZE];
};

typedef enum{
	ePatternBasic,
	ePatternTailGlob,
	ePatternRegex,
	ePatternInvalid,
} pattern_t;

struct cod_pattern {
	char *regex;		// posix regex
	pcre *compiled;		// compiled regex, size is compiled->size
};

struct flagval{
	int debug;
	int complain;
	int audit;
};

struct cod_entry_user {
	char * name ;
	struct codomain_user * codomain ; 	/* Special codomain defined
						 * just for this executable */
	int mode ;	/* mode is 'or' of KERN_COD_* bits */
	int deny ;	/* TRUE or FALSE */

	pattern_t pattern_type;
	struct cod_pattern pat;

	struct immunix_digital_sig sig;
	struct cod_entry_user * next;
};

struct cod_net_entry_user {
	struct in_addr *saddr, *smask;
	struct in_addr *daddr, *dmask;
	unsigned short src_port[2], dst_port[2];
	char * iface;
	int mode;
	struct codomain_user * codomain ;
	struct cod_net_entry_user * next;
};

struct codomain_user {
	char * name ;				/* codomain name */
	char * sub_name ;			/* subdomain name or NULL */
	int default_deny ;			/* TRUE or FALSE */

	struct flagval flags;

	unsigned int capabilities;

	struct cod_entry_user * entries ;
	struct cod_net_entry_user * net_entries;
	struct codomain_user * subdomain ;
	struct codomain_user * next ;
} ;

struct cod_global_entry_user {
	struct cod_entry_user *entry;
	struct cod_net_entry_user *net_entry;
	struct codomain_user * hats ;
	unsigned int capabilities;
};

/* Rev this every time the codomain_sysctl structure
 * and it's substructures change 
 */
#define CODOMAIN_SYSCTL_VERSION	0x00000211	

struct codomain_sysctl {
	int			version;
	int			action;		/* KERN_COD_[ADD|DEL|REPL] */
	struct codomain_user	*codomain;
} ;

struct sd_hat {
	char *hat_name;
	unsigned int hat_magic;
};


#ifdef __KERNEL__

/*======================================================================
 *
 *  Place holder for sysctl value
 *
 *======================================================================*/
extern struct codomain_struct * codomain_sysctl_val ;


/*======================================================================
 *
 *  codomain_paranoid - if TRUE, every process that starts must have a
 *  			codomain defined. Can be set through sysctl
 *  			KERN_COD_PARANOID and through
 *  			/proc/kernel/codomain_paranoid.
 *
 *======================================================================*/
extern int codomain_paranoid;

#endif /* __KERNEL__ */

#endif /* ! _IMMUNIX_H */

/*======================================================================*/
