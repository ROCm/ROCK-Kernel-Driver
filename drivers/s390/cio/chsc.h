#ifndef S390_CHSC_H
#define S390_CHSC_H

#define NR_CHPIDS 256

#define CHP_OFFLINE 0
#define CHP_LOGICALLY_OFFLINE 1
#define CHP_STANDBY 2
#define CHP_ONLINE 3

#define CHSC_SEI_ACC_CHPID        1
#define CHSC_SEI_ACC_LINKADDR     2
#define CHSC_SEI_ACC_FULLLINKADDR 3

struct sei_area {
	struct {
		/* word 0 */
		__u16 command_code1;
		__u16 command_code2;
		/* word 1 */
		__u32 reserved1;
		/* word 2 */
		__u32 reserved2;
		/* word 3 */
		__u32 reserved3;
	} __attribute__ ((packed,aligned(8))) request_block;
	struct {
		/* word 0 */
		__u16 length;
		__u16 response_code;
		/* word 1 */
		__u32 reserved1;
		/* word 2 */
		__u8  flags;
		__u8  vf;	  /* validity flags */
		__u8  rs;	  /* reporting source */
		__u8  cc;	  /* content code */
		/* word 3 */
		__u16 fla;	  /* full link address */
		__u16 rsid;	  /* reporting source id */
		/* word 4 */
		__u32 reserved2;
		/* word 5 */
		__u32 reserved3;
		/* word 6 */
		__u32 ccdf;	  /* content-code dependent field */
		/* word 7 */
		__u32 reserved4;
		/* word 8 */
		__u32 reserved5;
		/* word 9 */
		__u32 reserved6;
	} __attribute__ ((packed,aligned(8))) response_block;
} __attribute__ ((packed,aligned(PAGE_SIZE)));

struct ssd_area {
	struct {
		/* word 0 */
		__u16 command_code1;
		__u16 command_code2;
		/* word 1 */
		__u16 reserved1;
		__u16 f_sch;	 /* first subchannel */
		/* word 2 */
		__u16 reserved2;
		__u16 l_sch;	/* last subchannel */
		/* word 3 */
		__u32 reserved3;
	} __attribute__ ((packed,aligned(8))) request_block;
	struct {
		/* word 0 */
		__u16 length;
		__u16 response_code;
		/* word 1 */
		__u32 reserved1;
		/* word 2 */
		__u8 sch_valid : 1;
		__u8 dev_valid : 1;
		__u8 st	       : 3; /* subchannel type */
		__u8 zeroes    : 3;
		__u8  unit_addr;  /* unit address */
		__u16 devno;	  /* device number */
		/* word 3 */
		__u8 path_mask;
		__u8 fla_valid_mask;
		__u16 sch;	  /* subchannel */
		/* words 4-5 */
		__u8 chpid[8];	  /* chpids 0-7 */
		/* words 6-9 */
		__u16 fla[8];	  /* full link addresses 0-7 */
	} __attribute__ ((packed,aligned(8))) response_block;
} __attribute__ ((packed,aligned(PAGE_SIZE)));


struct channel_path {
	int id;
	int state;
	struct device dev;
};

extern struct channel_path *chps[];

extern void s390_process_css( void );
extern void chsc_validate_chpids(struct subchannel *);
#endif
