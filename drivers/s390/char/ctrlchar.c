/*
 *  drivers/s390/char/ctrlchar.c
 *  Unified handling of special chars.
 *
 *    Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Fritz Elfert <felfert@millenux.com> <elfert@de.ibm.com>
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/tty.h>
#include <linux/interrupt.h>

#include <linux/sysrq.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/cpcmd.h>
#include <asm/irq.h>

#ifdef CONFIG_MAGIC_SYSRQ
static int ctrlchar_sysrq_key;
static struct tq_struct ctrlchar_tq;

static void
ctrlchar_handle_sysrq(struct tty_struct *tty) {
	handle_sysrq(ctrlchar_sysrq_key, NULL, NULL, tty);
}
#endif

void ctrlchar_init(void) {
#ifdef CONFIG_MAGIC_SYSRQ
	static int init_done = 0;

	if (init_done++)
		return;
	INIT_LIST_HEAD(&ctrlchar_tq.list);
	ctrlchar_tq.sync = 0;
	ctrlchar_tq.routine = (void (*)(void *)) ctrlchar_handle_sysrq;
#endif
}

/**
 * Check for special chars at start of input.
 *
 * @param buf Console input buffer.
 * @param len Length of valid data in buffer.
 * @param tty The tty struct for this console.
 * @return NULL, if nothing matched, (char *)-1, if buffer contents
 *         should be ignored, otherwise pointer to char to be inserted.
 */
char *ctrlchar_handle(const char *buf, int len, struct tty_struct *tty) {

	static char ret;

	if ((len < 2) || (len > 3))
		return NULL;
	/* hat is 0xb1 in codepage 037 (US etc.) and thus */
	/* converted to 0x5e in ascii ('^') */
	if ((buf[0] != '^') && (buf[0] != '\252'))
		return NULL;
	switch (buf[1]) {
#ifdef CONFIG_MAGIC_SYSRQ
		case '-':
			if (len == 3) {
				ctrlchar_sysrq_key = buf[2];
				ctrlchar_tq.data = tty;
				queue_task(&ctrlchar_tq, &tq_immediate);
				mark_bh(IMMEDIATE_BH);
				return (char *)-1;
			}
			break;
#endif
		case 'c':
			if (len == 2) {
				ret = INTR_CHAR(tty);
				return &ret;
			}
			break;
		case 'd':
			if (len == 2) {
				ret = EOF_CHAR(tty);
				return &ret;
			}
			break;
		case 'z':
			if (len == 2) {
				ret = SUSP_CHAR(tty);
				return &ret;
			}
			break;
	}
	return NULL;
}
