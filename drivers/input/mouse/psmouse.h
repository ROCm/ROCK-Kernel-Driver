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
#define PSMOUSE_CMD_DISABLE	0x00f5
#define PSMOUSE_CMD_RESET_DIS	0x00f6
#define PSMOUSE_CMD_RESET_BAT	0x02ff

#define PSMOUSE_RET_BAT		0xaa
#define PSMOUSE_RET_ID		0x00
#define PSMOUSE_RET_ACK		0xfa
#define PSMOUSE_RET_NAK		0xfe

#define PSMOUSE_FLAG_ACK	0	/* Waiting for ACK/NAK */
#define PSMOUSE_FLAG_CMD	1	/* Waiting for command to finish */
#define PSMOUSE_FLAG_CMD1	2	/* Waiting for the first byte of command response */
#define PSMOUSE_FLAG_WAITID	3	/* Command execiting is GET ID */

enum psmouse_state {
	PSMOUSE_IGNORE,
	PSMOUSE_INITIALIZING,
	PSMOUSE_CMD_MODE,
	PSMOUSE_ACTIVATED,
};

/* psmouse protocol handler return codes */
typedef enum {
	PSMOUSE_BAD_DATA,
	PSMOUSE_GOOD_DATA,
	PSMOUSE_FULL_PACKET
} psmouse_ret_t;

struct psmouse {
	void *private;
	struct input_dev dev;
	struct serio *serio;
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
	enum psmouse_state state;
	unsigned char nak;
	char error;
	char devname[64];
	char phys[32];
	unsigned long flags;

	/* Used to signal completion from interrupt handler */
	wait_queue_head_t wait;

	psmouse_ret_t (*protocol_handler)(struct psmouse *psmouse, struct pt_regs *regs);
	int (*reconnect)(struct psmouse *psmouse);
	void (*disconnect)(struct psmouse *psmouse);

	void (*pt_activate)(struct psmouse *psmouse);
	void (*pt_deactivate)(struct psmouse *psmouse);
};

#define PSMOUSE_PS2		1
#define PSMOUSE_PS2PP		2
#define PSMOUSE_PS2TPP		3
#define PSMOUSE_GENPS		4
#define PSMOUSE_IMPS		5
#define PSMOUSE_IMEX		6
#define PSMOUSE_SYNAPTICS 	7

int psmouse_command(struct psmouse *psmouse, unsigned char *param, int command);
int psmouse_sliced_command(struct psmouse *psmouse, unsigned char command);
int psmouse_reset(struct psmouse *psmouse);

extern int psmouse_smartscroll;
extern unsigned int psmouse_rate;

#endif /* _PSMOUSE_H */
