/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_KSYS_ELSC_H
#define _ASM_SN_KSYS_ELSC_H

#include <linux/config.h>
#if defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#include <asm/sn/ksys/l1.h>
#endif

// #include <asm/sn/ksys/i2c.h>

#define ELSC_I2C_ADDR		0x08
#define ELSC_I2C_HUB0		0x09
#define ELSC_I2C_HUB1		0x0a
#define ELSC_I2C_HUB2		0x0b
#define ELSC_I2C_HUB3		0x0c

#define ELSC_PACKET_MAX		96
#define ELSC_ACP_MAX		86		/* 84+cr+lf */
#define ELSC_LINE_MAX		(ELSC_ACP_MAX - 2)

/*
 * ELSC character queue type for I/O
 */

#define ELSC_QSIZE	128		/* Power of 2 is more efficient */

typedef sc_cq_t elsc_cq_t;

/*
 * ELSC structure passed around as handle
 */

typedef l1sc_t elsc_t;

void	elsc_init(elsc_t *e, nasid_t nasid);

int	elsc_process(elsc_t *e);
int	elsc_msg_check(elsc_t *e, char *msg, int msg_max);
int	elsc_msg_callback(elsc_t *e,
			  void (*callback)(void *callback_data, char *msg),
			  void *callback_data);
#if 0
char   *elsc_errmsg(int code);

int	elsc_nvram_write(elsc_t *e, int addr, char *buf, int len);
int	elsc_nvram_read(elsc_t *e, int addr, char *buf, int len);
int	elsc_nvram_magic(elsc_t *e);
#endif

int	elsc_command(elsc_t *e, int only_if_message);
int	elsc_parse(elsc_t *e, char *p1, char *p2, char *p3);
int	elsc_ust_write(elsc_t *e, uchar_t c);
int 	elsc_ust_read(elsc_t *e, char *c);



/*
 * System controller commands
 */

int	elsc_version(elsc_t *e, char *result);
#if 0
int	elsc_debug_set(elsc_t *e, u_char byte1, u_char byte2);
int	elsc_debug_get(elsc_t *e, u_char *byte1, u_char *byte2);
#endif
int	elsc_module_set(elsc_t *e, int module);
int	elsc_module_get(elsc_t *e);
int	elsc_partition_set(elsc_t *e, int partition);
int	elsc_partition_get(elsc_t *e);
int	elsc_domain_set(elsc_t *e, int domain);
int	elsc_domain_get(elsc_t *e);
int	elsc_cluster_set(elsc_t *e, int cluster);
int	elsc_cluster_get(elsc_t *e);
int	elsc_cell_set(elsc_t *e, int cell);
int	elsc_cell_get(elsc_t *e);
int	elsc_bist_set(elsc_t *e, char bist_status);
char	elsc_bist_get(elsc_t *e);
int	elsc_lock(elsc_t *e,
		  int retry_interval_usec,
		  int timeout_usec, u_char lock_val);
int	elsc_unlock(elsc_t *e);
int	elsc_display_char(elsc_t *e, int led, int chr);
int	elsc_display_digit(elsc_t *e, int led, int num, int l_case);
#if 0
int	elsc_display_mesg(elsc_t *e, char *chr);	/* 8-char input */
int	elsc_password_set(elsc_t *e, char *password);	/* 4-char input */
int	elsc_password_get(elsc_t *e, char *password);	/* 4-char output */
int	elsc_rpwr_query(elsc_t *e, int is_master);
int	elsc_power_query(elsc_t *e);
int	elsc_power_down(elsc_t *e, int sec);
int	elsc_power_cycle(elsc_t *e);
int	elsc_system_reset(elsc_t *e);
int	elsc_dip_switches(elsc_t *e);
int	elsc_nic_get(elsc_t *e, uint64_t *nic, int verbose);
#endif

int	_elsc_hbt(elsc_t *e, int ival, int rdly);

#define	elsc_hbt_enable(e, ival, rdly)	_elsc_hbt(e, ival, rdly)
#define	elsc_hbt_disable(e)		_elsc_hbt(e, 0, 0)
#define	elsc_hbt_send(e)		_elsc_hbt(e, 0, 1)

/*
 * Routines for using the ELSC as a UART.  There's a version of each
 * routine that takes a pointer to an elsc_t, and another version that
 * gets the pointer by calling a user-supplied global routine "get_elsc".
 * The latter version is useful when the elsc is employed for stdio.
 */

#define ELSCUART_FLASH		0x3c			/* LED pattern */

elsc_t	       *get_elsc(void);

int	elscuart_probe(void);
void	elscuart_init(void *);
int	elscuart_poll(void);
int	elscuart_readc(void);
int	elscuart_getc(void);
int	elscuart_putc(int);
int	elscuart_puts(char *);
char   *elscuart_gets(char *, int);
int	elscuart_flush(void);



/*
 * Error codes
 *
 *   The possible ELSC error codes are a superset of the I2C error codes,
 *   so ELSC error codes begin at -100.
 */

#define ELSC_ERROR_NONE			0

#define ELSC_ERROR_CMD_SEND	       -100	/* Error sending command    */
#define ELSC_ERROR_CMD_CHECKSUM	       -101	/* Command checksum bad     */
#define ELSC_ERROR_CMD_UNKNOWN	       -102	/* Unknown command          */
#define ELSC_ERROR_CMD_ARGS	       -103	/* Invalid argument(s)      */
#define ELSC_ERROR_CMD_PERM	       -104	/* Permission denied	    */
#define ELSC_ERROR_CMD_STATE	       -105	/* not allowed in this state*/

#define ELSC_ERROR_RESP_TIMEOUT	       -110	/* ELSC response timeout    */
#define ELSC_ERROR_RESP_CHECKSUM       -111	/* Response checksum bad    */
#define ELSC_ERROR_RESP_FORMAT	       -112	/* Response format error    */
#define ELSC_ERROR_RESP_DIR	       -113	/* Response direction error */

#define ELSC_ERROR_MSG_LOST	       -120	/* Queue full; msg. lost    */
#define ELSC_ERROR_LOCK_TIMEOUT	       -121	/* ELSC response timeout    */
#define ELSC_ERROR_DATA_SEND	       -122	/* Error sending data       */
#define ELSC_ERROR_NIC		       -123	/* NIC processing error     */
#define ELSC_ERROR_NVMAGIC	       -124	/* Bad magic no. in NVRAM   */
#define ELSC_ERROR_MODULE	       -125	/* Moduleid processing err  */

#endif /* _ASM_SN_KSYS_ELSC_H */
