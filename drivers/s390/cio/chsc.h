#ifndef S390_CHSC_H
#define S390_CHSC_H

#define NR_CHPIDS 256

#define CHP_STANDBY 1
#define CHP_LOGICALLY_OFFLINE 2
#define CHP_ONLINE 4

#define CHSC_SEI_ACC_CHPID        1
#define CHSC_SEI_ACC_LINKADDR     2
#define CHSC_SEI_ACC_FULLLINKADDR 3

struct chsc_header {
	u16 length;
	u16 code;
};

struct channel_path {
	int id;
	struct {
		unsigned int online:1;
		unsigned int logically_online:1;
	}__attribute__((packed)) state;
	struct device dev;
};

extern struct channel_path *chps[];

extern void s390_process_css( void );
extern void chsc_validate_chpids(struct subchannel *);
#endif
