#ifndef S390_CHSC_H
#define S390_CHSC_H

#define NR_CHPIDS 256

#define CHSC_SEI_ACC_CHPID        1
#define CHSC_SEI_ACC_LINKADDR     2
#define CHSC_SEI_ACC_FULLLINKADDR 3

struct chsc_header {
	u16 length;
	u16 code;
};

struct channel_path {
	int id;
	int state;
	struct device dev;
};

extern struct channel_path *chps[];

extern void s390_process_css( void );
extern void chsc_validate_chpids(struct subchannel *);
extern void chpid_is_actually_online(int);
#endif
