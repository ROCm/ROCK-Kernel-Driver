#ifndef _PSMOUSE_H
#define _PSMOUSE_H

#define PSMOUSE_CMD_SETSCALE11	0x00e6
#define PSMOUSE_CMD_SETRES	0x10e8
#define PSMOUSE_CMD_GETINFO	0x03e9
#define PSMOUSE_CMD_SETSTREAM	0x00ea
#define PSMOUSE_CMD_POLL	0x03eb
#define PSMOUSE_CMD_GETID	0x02f2
#define PSMOUSE_CMD_SETRATE	0x10f3
#define PSMOUSE_CMD_ENABLE	0x00f4
#define PSMOUSE_CMD_RESET_DIS	0x00f6
#define PSMOUSE_CMD_RESET_BAT	0x02ff

#define PSMOUSE_RET_BAT		0xaa
#define PSMOUSE_RET_ID		0x00
#define PSMOUSE_RET_ACK		0xfa
#define PSMOUSE_RET_NAK		0xfe

/* psmouse states */
#define PSMOUSE_CMD_MODE	0
#define PSMOUSE_ACTIVATED	1
#define PSMOUSE_IGNORE		2

/* psmouse protocol handler return codes */
typedef enum {
	PSMOUSE_BAD_DATA,
	PSMOUSE_GOOD_DATA,
	PSMOUSE_FULL_PACKET
} psmouse_ret_t;

struct psmouse;

struct psmouse_ptport {
	struct serio serio;

	void (*activate)(struct psmouse *parent);
	void (*deactivate)(struct psmouse *parent);
};

struct psmouse {
	void *private;
	struct input_dev dev;
	struct serio *serio;
	struct psmouse_ptport *ptport;
	char *vendor;
	char *name;
	unsigned char cmdbuf[8];
	unsigned char packet[8];
	unsigned char cmdcnt;
	unsigned char pktcnt;
	unsigned char type;
	unsigned char model;
	unsigned long last;
	unsigned long out_of_sync;
	unsigned char state;
	char acking;
	volatile char ack;
	char error;
	char devname[64];
	char phys[32];

	psmouse_ret_t (*protocol_handler)(struct psmouse *psmouse, struct pt_regs *regs); 
	int (*reconnect)(struct psmouse *psmouse);
	void (*disconnect)(struct psmouse *psmouse);
};

#define PSMOUSE_PS2		1
#define PSMOUSE_PS2PP		2
#define PSMOUSE_PS2TPP		3
#define PSMOUSE_GENPS		4
#define PSMOUSE_IMPS		5
#define PSMOUSE_IMEX		6
#define PSMOUSE_SYNAPTICS 	7

int psmouse_command(struct psmouse *psmouse, unsigned char *param, int command);
int psmouse_reset(struct psmouse *psmouse);

extern int psmouse_smartscroll;
extern unsigned int psmouse_rate;

#endif /* _PSMOUSE_H */
