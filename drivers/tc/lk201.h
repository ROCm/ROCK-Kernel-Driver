/*
 *	Commands to the keyboard processor
 */

#define	LK_PARAM		0x80	/* start/end parameter list */

#define	LK_CMD_RESUME		0x8b
#define	LK_CMD_INHIBIT		0xb9
#define	LK_CMD_LEDS_ON		0x13	/* 1 param: led bitmask */
#define	LK_CMD_LEDS_OFF		0x11	/* 1 param: led bitmask */
#define	LK_CMD_DIS_KEYCLK	0x99
#define	LK_CMD_ENB_KEYCLK	0x1b	/* 1 param: volume */
#define	LK_CMD_DIS_CTLCLK	0xb9
#define	LK_CMD_ENB_CTLCLK	0xbb
#define	LK_CMD_SOUND_CLK	0x9f
#define	LK_CMD_DIS_BELL		0xa1
#define	LK_CMD_ENB_BELL		0x23	/* 1 param: volume */
#define	LK_CMD_BELL		0xa7
#define	LK_CMD_TMP_NORPT	0xc1
#define	LK_CMD_ENB_RPT		0xe3
#define	LK_CMD_DIS_RPT		0xe1
#define	LK_CMD_RPT_TO_DOWN	0xd9
#define	LK_CMD_REQ_ID		0xab
#define	LK_CMD_POWER_UP		0xfd
#define	LK_CMD_TEST_MODE	0xcb
#define	LK_CMD_SET_DEFAULTS	0xd3

/* there are 4 leds, represent them in the low 4 bits of a byte */
#define	LK_PARAM_LED_MASK(ledbmap)	(LK_PARAM|(ledbmap))

/* max volume is 0, lowest is 0x7 */
#define	LK_PARAM_VOLUME(v)		(LK_PARAM|((v)&0x7))

/* mode set command(s) details */
#define	LK_MODE_DOWN		0x0
#define	LK_MODE_RPT_DOWN	0x2
#define	LK_MODE_DOWN_UP		0x6
#define	LK_CMD_MODE(m,div)	(LK_PARAM|(div<<3)|m)

#define LK_SHIFT 1<<0
#define LK_CTRL 1<<1
#define LK_LOCK 1<<2
#define LK_COMP 1<<3

#define LK_KEY_SHIFT 174
#define LK_KEY_CTRL 175
#define LK_KEY_LOCK 176
#define LK_KEY_COMP 177
#define LK_KEY_RELEASE 179
#define LK_KEY_REPEAT 180
#define LK_KEY_ACK 186

extern unsigned char scancodeRemap[256];