/*
 *  arch/s390/kernel/s390io.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 */

#ifndef __irq_h
#define __irq_h

#include <linux/config.h>
#include <asm/hardirq.h>

/*
 * How many IRQ's for S390 ?!?
 */
#define __MAX_SUBCHANNELS 65536
#define NR_IRQS           __MAX_SUBCHANNELS

#define INVALID_STORAGE_AREA ((void *)(-1 - 0x3FFF ))

extern int disable_irq(unsigned int);
extern int enable_irq(unsigned int);

/*
 * Interrupt controller descriptor. This is all we need
 * to describe about the low-level hardware.
 */
struct hw_interrupt_type {
        const __u8   *typename;
        int         (*handle)(unsigned int irq,
                              int cpu,
                              struct pt_regs * regs);
        int         (*enable) (unsigned int irq);
        int         (*disable)(unsigned int irq);
};

/*
 * Status: reason for being disabled: somebody has
 * done a "disable_irq()" or we must not re-enter the
 * already executing irq..
 */
#define IRQ_INPROGRESS  1
#define IRQ_DISABLED    2
#define IRQ_PENDING     4

/*
 * path management control word
 */
typedef struct {
      __u32 intparm;      /* interruption parameter */
      __u32 res0 : 2;     /* reserved zeros */
      __u32 isc  : 3;     /* interruption sublass */
      __u32 res5 : 3;     /* reserved zeros */
      __u32 ena  : 1;     /* enabled */
      __u32 lm   : 2;     /* limit mode */
      __u32 mme  : 2;     /* measurement-mode enable */
      __u32 mp   : 1;     /* multipath mode */
      __u32 tf   : 1;     /* timing facility */
      __u32 dnv  : 1;     /* device number valid */
      __u32 dev  : 16;    /* device number */
      __u8  lpm;          /* logical path mask */
      __u8  pnom;         /* path not operational mask */
      __u8  lpum;         /* last path used mask */
      __u8  pim;          /* path installed mask */
      __u16 mbi;          /* measurement-block index */
      __u8  pom;          /* path operational mask */
      __u8  pam;          /* path available mask */
      __u8  chpid[8];     /* CHPID 0-7 (if available) */
      __u32 unused1 : 8;  /* reserved zeros */
      __u32 st      : 3;  /* subchannel type */
      __u32 unused2 : 20; /* reserved zeros */
      __u32 csense  : 1;  /* concurrent sense; can be enabled ...*/
                          /*  ... per MSCH, however, if facility */
                          /*  ... is not installed, this results */
                          /*  ... in an operand exception.       */
   } __attribute__ ((packed)) pmcw_t;

/*
 * subchannel status word
 */
typedef struct {
      __u32 key  : 4; /* subchannel key */
      __u32 sctl : 1; /* suspend control */
      __u32 eswf : 1; /* ESW format */
      __u32 cc   : 2; /* deferred condition code */
      __u32 fmt  : 1; /* format */
      __u32 pfch : 1; /* prefetch */
      __u32 isic : 1; /* initial-status interruption control */
      __u32 alcc : 1; /* address-limit checking control */
      __u32 ssi  : 1; /* supress-suspended interruption */
      __u32 zcc  : 1; /* zero condition code */
      __u32 ectl : 1; /* extended control */
      __u32 pno  : 1;     /* path not operational */
      __u32 res  : 1;     /* reserved */
      __u32 fctl : 3;     /* function control */
      __u32 actl : 7;     /* activity control */
      __u32 stctl : 5;    /* status control */
      __u32 cpa;          /* channel program address */
      __u32 dstat : 8;    /* device status */
      __u32 cstat : 8;    /* subchannel status */
      __u32 count : 16;   /* residual count */
   } __attribute__ ((packed)) scsw_t;

#define SCSW_FCTL_CLEAR_FUNC     0x1
#define SCSW_FCTL_HALT_FUNC      0x2
#define SCSW_FCTL_START_FUNC     0x4

#define SCSW_ACTL_SUSPENDED      0x1
#define SCSW_ACTL_DEVACT         0x2
#define SCSW_ACTL_SCHACT         0x4
#define SCSW_ACTL_CLEAR_PEND     0x8
#define SCSW_ACTL_HALT_PEND      0x10
#define SCSW_ACTL_START_PEND     0x20
#define SCSW_ACTL_RESUME_PEND    0x40

#define SCSW_STCTL_STATUS_PEND   0x1
#define SCSW_STCTL_SEC_STATUS    0x2
#define SCSW_STCTL_PRIM_STATUS   0x4
#define SCSW_STCTL_INTER_STATUS  0x8
#define SCSW_STCTL_ALERT_STATUS  0x10

#define DEV_STAT_ATTENTION       0x80
#define DEV_STAT_STAT_MOD        0x40
#define DEV_STAT_CU_END          0x20
#define DEV_STAT_BUSY            0x10
#define DEV_STAT_CHN_END         0x08
#define DEV_STAT_DEV_END         0x04
#define DEV_STAT_UNIT_CHECK      0x02
#define DEV_STAT_UNIT_EXCEP      0x01

#define SCHN_STAT_PCI            0x80
#define SCHN_STAT_INCORR_LEN     0x40
#define SCHN_STAT_PROG_CHECK     0x20
#define SCHN_STAT_PROT_CHECK     0x10
#define SCHN_STAT_CHN_DATA_CHK   0x08
#define SCHN_STAT_CHN_CTRL_CHK   0x04
#define SCHN_STAT_INTF_CTRL_CHK  0x02
#define SCHN_STAT_CHAIN_CHECK    0x01

/*
 * subchannel information block
 */
typedef struct {
      pmcw_t pmcw;             /* path management control word */
      scsw_t scsw;             /* subchannel status word */
      __u8 mda[12];            /* model dependent area */
   } __attribute__ ((packed,aligned(4))) schib_t;

typedef struct {
      __u8  cmd_code;/* command code */
      __u8  flags;   /* flags, like IDA adressing, etc. */
      __u16 count;   /* byte count */
      __u32 cda;     /* data address */
   } ccw1_t __attribute__ ((packed,aligned(8)));

#define CCW_FLAG_DC             0x80
#define CCW_FLAG_CC             0x40
#define CCW_FLAG_SLI            0x20
#define CCW_FLAG_SKIP           0x10
#define CCW_FLAG_PCI            0x08
#define CCW_FLAG_IDA            0x04
#define CCW_FLAG_SUSPEND        0x02

#define CCW_CMD_READ_IPL        0x02
#define CCW_CMD_NOOP            0x03
#define CCW_CMD_BASIC_SENSE     0x04
#define CCW_CMD_TIC             0x08
#define CCW_CMD_SENSE_PGID      0x34
#define CCW_CMD_RDC             0x64
#define CCW_CMD_SET_PGID        0xAF
#define CCW_CMD_SENSE_ID        0xE4

#define SENSE_MAX_COUNT         0x20

/*
 * architectured values for first sense byte
 */
#define SNS0_CMD_REJECT         0x80
#define SNS_CMD_REJECT          SNS0_CMD_REJECT
#define SNS0_INTERVENTION_REQ   0x40
#define SNS0_BUS_OUT_CHECK      0x20
#define SNS0_EQUIPMENT_CHECK    0x10
#define SNS0_DATA_CHECK         0x08
#define SNS0_OVERRUN            0x04

/*
 * operation request block
 */
typedef struct {
      __u32 intparm;  /* interruption parameter */
      __u32 key  : 4; /* flags, like key, suspend control, etc. */
      __u32 spnd : 1; /* suspend control */
      __u32 res1 : 3; /* reserved */
      __u32 fmt  : 1; /* format control */
      __u32 pfch : 1; /* prefetch control */
      __u32 isic : 1; /* initial-status-interruption control */
      __u32 alcc : 1; /* address-limit-checking control */
      __u32 ssic : 1; /* suppress-suspended-interr. control */
      __u32 res2 : 3; /* reserved */
      __u32 lpm  : 8; /* logical path mask */
      __u32 ils  : 1; /* incorrect length */
      __u32 zero : 7; /* reserved zeros */
      __u32 cpa;      /* channel program address */
   }  __attribute__ ((packed,aligned(4))) orb_t;

typedef struct {
      __u32 res0  : 4;  /* reserved */
      __u32 pvrf  : 1;  /* path-verification-required flag */
      __u32 cpt   : 1;  /* channel-path timeout */
      __u32 fsavf : 1;  /* Failing storage address validity flag */
      __u32 cons  : 1;  /* concurrent-sense */
      __u32 res8  : 2;  /* reserved */
      __u32 scnt  : 6;  /* sense count if cons == 1 */
      __u32 res16 : 16; /* reserved */
   } __attribute__ ((packed)) erw_t;

/*
 * subchannel logout area
 */
typedef struct {
      __u32 res0  : 1;  /* reserved */
      __u32 esf   : 7;  /* extended status flags */
      __u32 lpum  : 8;  /* last path used mask */
      __u32 res16 : 1;  /* reserved */
      __u32 fvf   : 5;  /* field-validity flags */
      __u32 sacc  : 2;  /* storage access code */
      __u32 termc : 2;  /* termination code */
      __u32 devsc : 1;  /* device-status check */
      __u32 serr  : 1;  /* secondary error */
      __u32 ioerr : 1;  /* i/o-error alert */
      __u32 seqc  : 3;  /* sequence code */
   } __attribute__ ((packed)) sublog_t ;

/*
 * Format 0 Extended Status Word (ESW)
 */
typedef struct {
      sublog_t sublog;    /* subchannel logout */
      erw_t    erw;       /* extended report word */
      __u32    faddr;     /* failing address */
      __u32    zeros[2];  /* 2 fullwords of zeros */
   } __attribute__ ((packed)) esw0_t;

/*
 * Format 1 Extended Status Word (ESW)
 */
typedef struct {
      __u8  zero0;    /* reserved zeros */
      __u8  lpum;     /* last path used mask */
      __u8  zero16;   /* reserved zeros */
      erw_t erw;      /* extended report word */
      __u32 zeros[3]; /* 2 fullwords of zeros */
   } __attribute__ ((packed)) esw1_t;

/*
 * Format 2 Extended Status Word (ESW)
 */
typedef struct {
      __u8  zero0;    /* reserved zeros */
      __u8  lpum;     /* last path used mask */
      __u16 dcti;     /* device-connect-time interval */
      erw_t erw;      /* extended report word */
      __u32 zeros[3]; /* 2 fullwords of zeros */
   } __attribute__ ((packed)) esw2_t;

/*
 * Format 3 Extended Status Word (ESW)
 */
typedef struct {
      __u8  zero0;    /* reserved zeros */
      __u8  lpum;     /* last path used mask */
      __u16 res;      /* reserved */
      erw_t erw;      /* extended report word */
      __u32 zeros[3]; /* 2 fullwords of zeros */
   } __attribute__ ((packed)) esw3_t;

typedef union {
      esw0_t esw0;
      esw1_t esw1;
      esw2_t esw2;
      esw3_t esw3;
   } __attribute__ ((packed)) esw_t;

/*
 * interruption response block
 */
typedef struct {
      scsw_t scsw;             /* subchannel status word */
      esw_t  esw;              /* extended status word */
      __u8   ecw[32];          /* extended control word */
   } irb_t __attribute__ ((packed,aligned(4)));

/*
 * TPI info structure
 */
typedef struct {
      __u32 res : 16;   /* reserved 0x00000001 */
      __u32 irq : 16;   /* aka. subchannel number */
      __u32 intparm;    /* interruption parameter */
   } __attribute__ ((packed)) tpi_info_t;


/*
 * This is the "IRQ descriptor", which contains various information
 * about the irq, including what kind of hardware handling it has,
 * whether it is disabled etc etc.
 *
 * Pad this out to 32 bytes for cache and indexing reasons.
 */
typedef struct {
      __u32                     status;    /* IRQ status - IRQ_INPROGRESS, IRQ_DISABLED */
      struct hw_interrupt_type *handler;   /* handle/enable/disable functions */
      struct irqaction         *action;    /* IRQ action list */
   } irq_desc_t;

//
// command information word  (CIW) layout
//
typedef struct _ciw {
   __u32        et       :  2; // entry type
   __u32        reserved :  2; // reserved
   __u32        ct       :  4; // command type
   __u32        cmd      :  8; // command
   __u32        count    : 16; // count
   } __attribute__ ((packed)) ciw_t;

#define CIW_TYPE_RCD    0x0    // read configuration data
#define CIW_TYPE_SII    0x1    // set interface identifier
#define CIW_TYPE_RNI    0x2    // read node identifier

//
// sense-id response buffer layout
//
typedef struct {
  /* common part */
      __u8           reserved;     /* always 0x'FF' */
      __u16          cu_type;      /* control unit type */
      __u8           cu_model;     /* control unit model */
      __u16          dev_type;     /* device type */
      __u8           dev_model;    /* device model */
      __u8           unused;       /* padding byte */
  /* extended part */
      ciw_t    ciw[62];            /* variable # of CIWs */
   }  __attribute__ ((packed,aligned(4))) senseid_t;

/*
 * sense data
 */
typedef struct {
      __u8          res[32];   /* reserved   */
      __u8          data[32];  /* sense data */
   } __attribute__ ((packed)) sense_t;

/*
 * device status area, to be provided by the device driver
 *  when calling request_irq() as parameter "dev_id", later
 *  tied to the "action" control block.
 *
 * Note : No data area must be added after union ii or the
 *         effective devstat size calculation will fail !
 */
typedef struct {
     __u16         devno;    /* device number, aka. "cuu" from irb */
     unsigned int  intparm;  /* interrupt parameter */
     __u8          cstat;    /* channel status - accumulated */
     __u8          dstat;    /* device status - accumulated */
     __u8          lpum;     /* last path used mask from irb */
     __u8          unused;   /* not used - reserved */
     unsigned int  flag;     /* flag : see below */
     __u32         cpa;      /* CCW address from irb at primary status */
     __u32         rescnt;   /* res. count from irb at primary status */
     __u32         scnt;     /* sense count, if DEVSTAT_FLAG_SENSE_AVAIL */
     union {
        irb_t   irb;         /* interruption response block */
        sense_t sense;       /* sense information */
        } ii;                /* interrupt information */
  } devstat_t;

#define DEVSTAT_FLAG_SENSE_AVAIL   0x00000001
#define DEVSTAT_NOT_OPER           0x00000002
#define DEVSTAT_START_FUNCTION     0x00000004
#define DEVSTAT_HALT_FUNCTION      0x00000008
#define DEVSTAT_STATUS_PENDING     0x00000010
#define DEVSTAT_REVALIDATE         0x00000020
#define DEVSTAT_DEVICE_GONE        0x00000040
#define DEVSTAT_DEVICE_OWNED       0x00000080
#define DEVSTAT_CLEAR_FUNCTION     0x00000100
#define DEVSTAT_FINAL_STATUS       0x80000000

#define INTPARM_STATUS_PENDING     0xFFFFFFFF

typedef struct {
	__u8  state1    :  2;   /* path state value 1 */
	__u8  state2    :  2;   /* path state value 2 */
	__u8  state3    :  1;   /* path state value 3 */
	__u8  resvd     :  3;   /* reserved */
	} __attribute__ ((packed)) path_state_t;

typedef struct {
   union {
		__u8         fc;   /* SPID function code */
		path_state_t ps;   /* SNID path state */
	} inf;
	__u32 cpu_addr  : 16;   /* CPU address */
	__u32 cpu_id    : 24;   /* CPU identification */
	__u32 cpu_model : 16;   /* CPU model */
	__u32 tod_high;         /* high word TOD clock */
	} __attribute__ ((packed)) pgid_t;

#define SPID_FUNC_MULTI_PATH       0x80
#define SPID_FUNC_ESTABLISH        0x00
#define SPID_FUNC_RESIGN           0x40
#define SPID_FUNC_DISBAND          0x20

#define SNID_STATE1_RESET          0x0
#define SNID_STATE1_UNGROUPED      0x8
#define SNID_STATE1_GROUPED        0xC

#define SNID_STATE2_NOT_RESVD      0x0
#define SNID_STATE2_RESVD_ELSE     0x8
#define SNID_STATE2_RESVD_SELF     0xC

#define SNID_STATE3_MULTI_PATH     1

/*
 * Flags used as input parameters for do_IO()
 */
#define DOIO_EARLY_NOTIFICATION 0x01    /* allow for I/O completion ... */
                                        /* ... notification after ... */
                                        /* ... primary interrupt status */
#define DOIO_RETURN_CHAN_END       DOIO_EARLY_NOTIFICATION
#define DOIO_VALID_LPM          0x02    /* LPM input parameter is valid */
#define DOIO_WAIT_FOR_INTERRUPT 0x04    /* wait synchronously for interrupt */
#define DOIO_REPORT_ALL         0x08    /* report all interrupt conditions */
#define DOIO_ALLOW_SUSPEND      0x10    /* allow for channel prog. suspend */
#define DOIO_DENY_PREFETCH      0x20    /* don't allow for CCW prefetch */
#define DOIO_SUPPRESS_INTER     0x40    /* suppress intermediate inter. */
                                        /* ... for suspended CCWs */
#define DOIO_TIMEOUT            0x80    /* 3 secs. timeout for sync. I/O */

/*
 * do_IO()
 *
 * Start a S/390 channel program. When the interrupt arrives
 *  handle_IRQ_event() is called, which eventually calls the
 *  IRQ handler, either immediately, delayed (dev-end missing,
 *  or sense required) or never (no IRQ handler registered -
 *  should never occur, as the IRQ (subchannel ID) should be
 *  disabled if no handler is present. Depending on the action
 *  taken, do_IO() returns :  0      - Success
 *                           -EIO    - Status pending
 *                                        see : action->dev_id->cstat
 *                                              action->dev_id->dstat
 *                           -EBUSY  - Device busy
 *                           -ENODEV - Device not operational
 */
int do_IO( int            irq,          /* IRQ aka. subchannel number */
           ccw1_t        *cpa,          /* logical channel program address */
           unsigned long  intparm,      /* interruption parameter */
           __u8           lpm,          /* logical path mask */
           unsigned long  flag);        /* flags : see above */

int start_IO( int           irq,       /* IRQ aka. subchannel number */
              ccw1_t       *cpa,       /* logical channel program address */
              unsigned int  intparm,   /* interruption parameter */
              __u8          lpm,       /* logical path mask */
              unsigned int  flag);     /* flags : see above */

void do_crw_pending( void  );	         /* CRW handler */

int resume_IO( int irq);               /* IRQ aka. subchannel number */

int halt_IO( int           irq,         /* IRQ aka. subchannel number */
             unsigned long intparm,     /* dummy intparm */
             unsigned long flag);       /* possible DOIO_WAIT_FOR_INTERRUPT */

int clear_IO( int           irq,         /* IRQ aka. subchannel number */
              unsigned long intparm,     /* dummy intparm */
              unsigned long flag);       /* possible DOIO_WAIT_FOR_INTERRUPT */

int process_IRQ( struct pt_regs regs,
                 unsigned int   irq,
                 unsigned int   intparm);


int enable_cpu_sync_isc ( int irq );
int disable_cpu_sync_isc( int irq );

typedef struct {
     int          irq;                  /* irq, aka. subchannel */
     __u16        devno;                /* device number */
     unsigned int status;               /* device status */
     senseid_t    sid_data;             /* senseID data */
     } dev_info_t;

int get_dev_info( int irq, dev_info_t *);   /* to be eliminated - don't use */

int get_dev_info_by_irq  ( int irq, dev_info_t *pdi);
int get_dev_info_by_devno( __u16 devno, dev_info_t *pdi);

int          get_irq_by_devno( __u16 devno );
unsigned int get_devno_by_irq( int irq );

int get_irq_first( void );
int get_irq_next ( int irq );

int read_dev_chars( int irq, void **buffer, int length );
int read_conf_data( int irq, void **buffer, int *length );

extern int handle_IRQ_event( unsigned int irq, int cpu, struct pt_regs *);

extern int set_cons_dev(int irq);
extern int reset_cons_dev(int irq);
extern int wait_cons_dev(int irq);

/*
 * Some S390 specific IO instructions as inline
 */

extern __inline__ int stsch(int irq, volatile schib_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "STSCH 0(%2)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L), "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int msch(int irq, volatile schib_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "MSCH 0(%2)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L), "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int msch_err(int irq, volatile schib_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "    lr   1,%1\n"
                "    msch 0(%2)\n"
                "0:  ipm  %0\n"
                "    srl  %0,28\n"
                "1:\n"
                ".section .fixup,\"ax\"\n"
                "2:  l    %0,%3\n"
                "    bras 1,3f\n"
                "    .long 1b\n"
                "3:  l    1,0(1)\n"
                "    br   1\n"
                ".previous\n"
                ".section __ex_table,\"a\"\n"
                "   .align 4\n"
                "   .long 0b,2b\n"
                ".previous"
                : "=d" (ccode)
                : "r" (irq | 0x10000L), "a" (addr), "i" (__LC_PGM_ILC)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int tsch(int irq, volatile irb_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "TSCH 0(%2)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L), "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int tpi( volatile tpi_info_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "TPI 0(%1)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int ssch(int irq, volatile orb_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "SSCH 0(%2)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L), "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int rsch(int irq)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "RSCH\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int csch(int irq)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "CSCH\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int hsch(int irq)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "HSCH\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int iac( void)
{
        int ccode;

        __asm__ __volatile__(
                "IAC 1\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : : "cc", "1" );
        return ccode;
}

typedef struct {
     __u16 vrdcdvno : 16;   /* device number (input) */
     __u16 vrdclen  : 16;   /* data block length (input) */
     __u32 vrdcvcla : 8;    /* virtual device class (output) */
     __u32 vrdcvtyp : 8;    /* virtual device type (output) */
     __u32 vrdcvsta : 8;    /* virtual device status (output) */
     __u32 vrdcvfla : 8;    /* virtual device flags (output) */
     __u32 vrdcrccl : 8;    /* real device class (output) */
     __u32 vrdccrty : 8;    /* real device type (output) */
     __u32 vrdccrmd : 8;    /* real device model (output) */
     __u32 vrdccrft : 8;    /* real device feature (output) */
     } __attribute__ ((packed,aligned(4))) diag210_t;

void VM_virtual_device_info( __u16      devno,   /* device number */
                             senseid_t *ps );    /* ptr to senseID data */

extern __inline__ int diag210( diag210_t * addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                ".long 0x83110210\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "a" (addr)
                : "cc", "1" );
        return ccode;
}

/*
 * Various low-level irq details needed by irq.c, process.c,
 * time.c, io_apic.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

void mask_irq(unsigned int irq);
void unmask_irq(unsigned int irq);

#define MAX_IRQ_SOURCES 128

extern spinlock_t irq_controller_lock;

#ifdef CONFIG_SMP

#include <asm/atomic.h>

static inline void irq_enter(int cpu, unsigned int irq)
{
        hardirq_enter(cpu);
        while (test_bit(0,&global_irq_lock)) {
                eieio();
        }
}

static inline void irq_exit(int cpu, unsigned int irq)
{
        hardirq_exit(cpu);
        release_irqlock(cpu);
}


#else

#define irq_enter(cpu, irq)     (++local_irq_count(cpu))
#define irq_exit(cpu, irq)      (--local_irq_count(cpu))

#endif

#define __STR(x) #x
#define STR(x) __STR(x)

#ifdef CONFIG_SMP

/*
 *      SMP has a few special interrupts for IPI messages
 */

#endif /* CONFIG_SMP */

/*
 * x86 profiling function, SMP safe. We might want to do this in
 * assembly totally?
 */
static inline void s390_do_profile (unsigned long addr)
{
#if 0
        if (prof_buffer && current->pid) {
                addr -= (unsigned long) &_stext;
                addr >>= prof_shift;
                /*
                 * Don't ignore out-of-bounds EIP values silently,
                 * put them into the last histogram slot, so if
                 * present, they will show up as a sharp peak.
                 */
                if (addr > prof_len-1)
                        addr = prof_len-1;
                atomic_inc((atomic_t *)&prof_buffer[addr]);
        }
#endif
}

#include <asm/s390io.h>

#define s390irq_spin_lock(irq) \
        spin_lock(&(ioinfo[irq]->irq_lock))

#define s390irq_spin_unlock(irq) \
        spin_unlock(&(ioinfo[irq]->irq_lock))

#define s390irq_spin_lock_irqsave(irq,flags) \
        spin_lock_irqsave(&(ioinfo[irq]->irq_lock), flags)
#define s390irq_spin_unlock_irqrestore(irq,flags) \
        spin_unlock_irqrestore(&(ioinfo[irq]->irq_lock), flags)
#endif

