/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		ipv4 proc support
 *
 *		Arnaldo Carvalho de Melo <acme@conectiva.com.br>, 2002/10/10
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License as
 *		published by the Free Software Foundation; version 2 of the
 *		License
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

extern int raw_get_info(char *, char **, off_t, int);
extern int snmp_get_info(char *, char **, off_t, int);
extern int netstat_get_info(char *, char **, off_t, int);
extern int afinet_get_info(char *, char **, off_t, int);
extern int tcp_get_info(char *, char **, off_t, int);
extern int udp_get_info(char *, char **, off_t, int);

#ifdef CONFIG_PROC_FS
int __init ipv4_proc_init(void)
{
	int rc = 0;

	if (!proc_net_create("raw", 0, raw_get_info))
		goto out_raw;

	if (!proc_net_create("netstat", 0, netstat_get_info))
		goto out_netstat;

	if (!proc_net_create("snmp", 0, snmp_get_info))
		goto out_snmp;

	if (!proc_net_create("sockstat", 0, afinet_get_info))
		goto out_sockstat;

	if (!proc_net_create("tcp", 0, tcp_get_info))
		goto out_tcp;

	if (!proc_net_create("udp", 0, udp_get_info))
		goto out_udp;
out:
	return rc;
out_udp:
	proc_net_remove("tcp");
out_tcp:
	proc_net_remove("sockstat");
out_sockstat:
	proc_net_remove("snmp");
out_snmp:
	proc_net_remove("netstat");
out_netstat:
	proc_net_remove("raw");
out_raw:
	rc = -ENOMEM;
	goto out;
}
#else /* CONFIG_PROC_FS */
int __init ipv4_proc_init(void)
{
	return 0;
}
#endif /* CONFIG_PROC_FS */
