/*
 * PS/2 driver library
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 * Copyright (c) 2004 Dmitry Torokhov
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>
#include <linux/libps2.h>

#define DRIVER_DESC	"PS/2 driver library"

MODULE_AUTHOR("Dmitry Torokhov <dtor@mail.ru>");
MODULE_DESCRIPTION("PS/2 driver library");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(ps2_init);
EXPORT_SYMBOL(ps2_sendbyte);
EXPORT_SYMBOL(ps2_command);
EXPORT_SYMBOL(ps2_schedule_command);
EXPORT_SYMBOL(ps2_handle_ack);
EXPORT_SYMBOL(ps2_handle_response);
EXPORT_SYMBOL(ps2_cmd_aborted);

/* Work structure to schedule execution of a command */
struct ps2work {
	struct work_struct work;
	struct ps2dev *ps2dev;
	int command;
	unsigned char param[0];
};


/*
 * ps2_sendbyte() sends a byte to the mouse, and waits for acknowledge.
 * It doesn't handle retransmission, though it could - because when there would
 * be need for retransmissions, the mouse has to be replaced anyway.
 *
 * ps2_sendbyte() can only be called from a process context
 */

int ps2_sendbyte(struct ps2dev *ps2dev, unsigned char byte)
{
	serio_pause_rx(ps2dev->serio);
	ps2dev->nak = 1;
	ps2dev->flags |= PS2_FLAG_ACK;
	serio_continue_rx(ps2dev->serio);

	if (serio_write(ps2dev->serio, byte) == 0)
		wait_event_interruptible_timeout(ps2dev->wait,
					!(ps2dev->flags & PS2_FLAG_ACK),
					msecs_to_jiffies(200));

	serio_pause_rx(ps2dev->serio);
	ps2dev->flags &= ~PS2_FLAG_ACK;
	serio_continue_rx(ps2dev->serio);

	return -ps2dev->nak;
}

/*
 * ps2_command() sends a command and its parameters to the mouse,
 * then waits for the response and puts it in the param array.
 *
 * ps2_command() can only be called from a process context
 */

int ps2_command(struct ps2dev *ps2dev, unsigned char *param, int command)
{
	int timeout;
	int send = (command >> 12) & 0xf;
	int receive = (command >> 8) & 0xf;
	int rc = -1;
	int i;

	down(&ps2dev->cmd_sem);

	timeout = msecs_to_jiffies(command == PS2_CMD_RESET_BAT ? 4000 : 500);

	serio_pause_rx(ps2dev->serio);
	ps2dev->flags = command == PS2_CMD_GETID ? PS2_FLAG_WAITID : 0;
	ps2dev->cmdcnt = receive;
	if (receive && param)
		for (i = 0; i < receive; i++)
			ps2dev->cmdbuf[(receive - 1) - i] = param[i];
	serio_continue_rx(ps2dev->serio);

	if (command & 0xff)
		if (ps2_sendbyte(ps2dev, command & 0xff))
			goto out;

	for (i = 0; i < send; i++)
		if (ps2_sendbyte(ps2dev, param[i]))
			goto out;

	timeout = wait_event_interruptible_timeout(ps2dev->wait,
				!(ps2dev->flags & PS2_FLAG_CMD1), timeout);

	if (ps2dev->cmdcnt && timeout > 0) {
		if (command == PS2_CMD_RESET_BAT && jiffies_to_msecs(timeout) > 100)
			timeout = msecs_to_jiffies(100);

		if (command == PS2_CMD_GETID &&
		    ps2dev->cmdbuf[receive - 1] != 0xab && ps2dev->cmdbuf[receive - 1] != 0xac) {
			/*
			 * Device behind the port is not a keyboard
			 * so we don't need to wait for the 2nd byte
			 * of ID response.
			 */
			serio_pause_rx(ps2dev->serio);
			ps2dev->flags = ps2dev->cmdcnt = 0;
			serio_continue_rx(ps2dev->serio);
		}

		wait_event_interruptible_timeout(ps2dev->wait,
				!(ps2dev->flags & PS2_FLAG_CMD), timeout);
	}

	if (param)
		for (i = 0; i < receive; i++)
			param[i] = ps2dev->cmdbuf[(receive - 1) - i];

	if (ps2dev->cmdcnt && (command != PS2_CMD_RESET_BAT || ps2dev->cmdcnt != 1))
		goto out;

	rc = 0;

out:
	serio_pause_rx(ps2dev->serio);
	ps2dev->flags = 0;
	serio_continue_rx(ps2dev->serio);

	up(&ps2dev->cmd_sem);
	return rc;
}

/*
 * ps2_execute_scheduled_command() sends a command, previously scheduled by
 * ps2_schedule_command(), to a PS/2 device (keyboard, mouse, etc.)
 */

static void ps2_execute_scheduled_command(void *data)
{
	struct ps2work *ps2work = data;

	ps2_command(ps2work->ps2dev, ps2work->param, ps2work->command);
	kfree(ps2work);
}

/*
 * ps2_schedule_command() allows to schedule delayed execution of a PS/2
 * command and can be used to issue a command from an interrupt or softirq
 * context.
 */

int ps2_schedule_command(struct ps2dev *ps2dev, unsigned char *param, int command)
{
	struct ps2work *ps2work;
	int send = (command >> 12) & 0xf;
	int receive = (command >> 8) & 0xf;

	if (!(ps2work = kmalloc(sizeof(struct ps2work) + max(send, receive), GFP_ATOMIC)))
		return -1;

	memset(ps2work, 0, sizeof(struct ps2work));
	ps2work->ps2dev = ps2dev;
	ps2work->command = command;
	memcpy(ps2work->param, param, send);
	INIT_WORK(&ps2work->work, ps2_execute_scheduled_command, ps2work);

	if (!schedule_work(&ps2work->work)) {
		kfree(ps2work);
		return -1;
	}

	return 0;
}

/*
 * ps2_init() initializes ps2dev structure
 */

void ps2_init(struct ps2dev *ps2dev, struct serio *serio)
{
	init_MUTEX(&ps2dev->cmd_sem);
	init_waitqueue_head(&ps2dev->wait);
	ps2dev->serio = serio;
}

/*
 * ps2_handle_ack()
 */

int ps2_handle_ack(struct ps2dev *ps2dev, unsigned char data)
{
	switch (data) {
		case PS2_RET_ACK:
			ps2dev->nak = 0;
			break;

		case PS2_RET_NAK:
			ps2dev->nak = 1;
			break;

		/*
		 * Workaround for mice which don't ACK the Get ID command.
		 * These are valid mouse IDs that we recognize.
		 */
		case 0x00:
		case 0x03:
		case 0x04:
			if (ps2dev->flags & PS2_FLAG_WAITID) {
				ps2dev->nak = 0;
				break;
			}
			/* Fall through */
		default:
			return 1;
	}

	if (!ps2dev->nak && ps2dev->cmdcnt)
		ps2dev->flags |= PS2_FLAG_CMD | PS2_FLAG_CMD1;

	ps2dev->flags &= ~PS2_FLAG_ACK;
	wake_up_interruptible(&ps2dev->wait);

	return data == PS2_RET_ACK || data == PS2_RET_NAK;
}


int ps2_handle_response(struct ps2dev *ps2dev, unsigned char data)
{
	if (ps2dev->cmdcnt)
		ps2dev->cmdbuf[--ps2dev->cmdcnt] = data;

	if (ps2dev->flags & PS2_FLAG_CMD1) {
		ps2dev->flags &= ~PS2_FLAG_CMD1;
		if (ps2dev->cmdcnt)
			wake_up_interruptible(&ps2dev->wait);
	}

	if (!ps2dev->cmdcnt) {
		ps2dev->flags &= ~PS2_FLAG_CMD;
		wake_up_interruptible(&ps2dev->wait);
	}

	return 1;
}

void ps2_cmd_aborted(struct ps2dev *ps2dev)
{
	if (ps2dev->flags & PS2_FLAG_ACK)
		ps2dev->nak = 1;

	if (ps2dev->flags & (PS2_FLAG_ACK | PS2_FLAG_CMD))
		wake_up_interruptible(&ps2dev->wait);

	ps2dev->flags = 0;
}

