/* Mode: C;
 * ifenslave.c: Configure network interfaces for parallel routing.
 *
 *	This program controls the Linux implementation of running multiple
 *	network interfaces in parallel.
 *
 * Usage:	ifenslave [-v] master-interface < slave-interface [metric <N>] > ...
 *
 * Author:	Donald Becker <becker@cesdis.gsfc.nasa.gov>
 *		Copyright 1994-1996 Donald Becker
 *
 *		This program is free software; you can redistribute it
 *		and/or modify it under the terms of the GNU General Public
 *		License as published by the Free Software Foundation.
 *
 *	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
 *	Center of Excellence in Space Data and Information Sciences
 *	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771
 *
 *  Changes :
 *    - 2000/10/02 Willy Tarreau <willy at meta-x.org> :
 *       - few fixes. Master's MAC address is now correctly taken from
 *         the first device when not previously set ;
 *       - detach support : call BOND_RELEASE to detach an enslaved interface.
 *       - give a mini-howto from command-line help : # ifenslave -h
 *
 *    - 2001/02/16 Chad N. Tindel <ctindel at ieee dot org> :
 *       - Master is now brought down before setting the MAC address.  In
 *         the 2.4 kernel you can't change the MAC address while the device is
 *         up because you get EBUSY.  
 *
 *    - 2001/09/13 Takao Indoh <indou dot takao at jp dot fujitsu dot com>
 *       - Added the ability to change the active interface on a mode 1 bond
 *         at runtime.
 *
 *    - 2001/10/23 Chad N. Tindel <ctindel at ieee dot org> :
 *       - No longer set the MAC address of the master.  The bond device will
 *         take care of this itself
 *       - Try the SIOC*** versions of the bonding ioctls before using the
 *         old versions
 *    - 2002/02/18 Erik Habbinga <erik_habbinga @ hp dot com> :
 *       - ifr2.ifr_flags was not initialized in the hwaddr_notset case,
 *         SIOCGIFFLAGS now called before hwaddr_notset test
 */

static char *version =
"ifenslave.c:v0.07 9/9/97  Donald Becker (becker@cesdis.gsfc.nasa.gov).\n"
"detach support added on 2000/10/02 by Willy Tarreau (willy at meta-x.org).\n"
"2.4 kernel support added on 2001/02/16 by Chad N. Tindel (ctindel at ieee dot org.\n";

static const char *usage_msg =
"Usage: ifenslave [-adfrvVh] <master-interface> < <slave-if> [metric <N>] > ...\n"
"       ifenslave -c master-interface slave-if\n";

static const char *howto_msg =
"Usage: ifenslave [-adfrvVh] <master-interface> < <slave-if> [metric <N>] > ...\n"
"       ifenslave -c master-interface slave-if\n"
"\n"
"       To create a bond device, simply follow these three steps :\n"
"       - ensure that the required drivers are properly loaded :\n"
"         # modprobe bonding ; modprobe <3c59x|eepro100|pcnet32|tulip|...>\n"
"       - assign an IP address to the bond device :\n"
"         # ifconfig bond0 <addr> netmask <mask> broadcast <bcast>\n"
"       - attach all the interfaces you need to the bond device :\n"
"         # ifenslave bond0 eth0 eth1 eth2\n"
"         If bond0 didn't have a MAC address, it will take eth0's. Then, all\n"
"         interfaces attached AFTER this assignment will get the same MAC addr.\n"
"\n"
"       To detach a dead interface without setting the bond device down :\n"
"         # ifenslave -d bond0 eth1\n"
"\n"
"       To set the bond device down and automatically release all the slaves :\n"
"         # ifconfig bond0 down\n"
"\n"
"       To change active slave :\n"
"         # ifenslave -c bond0 eth0\n"
"\n";

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_bonding.h>
#include <linux/sockios.h>

struct option longopts[] = {
 /* { name  has_arg  *flag  val } */
    {"all-interfaces", 0, 0, 'a'},	/* Show all interfaces. */
    {"force",       0, 0, 'f'},		/* Force the operation. */
    {"help", 		0, 0, '?'},		/* Give help */
	{"howto",       0, 0, 'h'},     /* Give some more help */
    {"receive-slave", 0, 0, 'r'},	/* Make a receive-only slave.  */
    {"verbose", 	0, 0, 'v'},		/* Report each action taken.  */
    {"version", 	0, 0, 'V'},		/* Emit version information.  */
    {"detach",       0, 0, 'd'},	/* Detach a slave interface. */
    {"change-active", 0, 0, 'c'},	/* Change the active slave.  */
    { 0, 0, 0, 0 }
};

/* Command-line flags. */
unsigned int
opt_a = 0,					/* Show-all-interfaces flag. */
opt_f = 0,					/* Force the operation. */
opt_r = 0,					/* Set up a Rx-only slave. */
opt_d = 0,					/* detach a slave interface. */
opt_c = 0,					/* change-active-slave flag. */
verbose = 0,					/* Verbose flag. */
opt_version = 0,
opt_howto = 0;
int skfd = -1;					/* AF_INET socket for ioctl() calls.	*/

static void if_print(char *ifname);

int
main(int argc, char **argv)
{
	struct ifreq  ifr2, if_hwaddr, if_ipaddr, if_metric, if_mtu, if_dstaddr;
	struct ifreq  if_netmask, if_brdaddr, if_flags;
	int goterr = 0;
	int c, errflag = 0;
	sa_family_t master_family;
	char **spp, *master_ifname, *slave_ifname;
	int hwaddr_notset;

	while ((c = getopt_long(argc, argv, "acdfrvV?h", longopts, 0)) != EOF)
		switch (c) {
		case 'a': opt_a++; break;
		case 'f': opt_f++; break;
		case 'r': opt_r++; break;
		case 'd': opt_d++; break;
		case 'c': opt_c++; break;
		case 'v': verbose++;		break;
		case 'V': opt_version++;	break;
		case 'h': opt_howto++;	break;
		case '?': errflag++;
		}

	/* option check */
	if (opt_c)
		if(opt_a || opt_f || opt_r || opt_d || verbose || opt_version ||
		   opt_howto || errflag ) {
			fprintf(stderr, usage_msg);
			return 2;
		}

	if (errflag) {
		fprintf(stderr, usage_msg);
		return 2;
	}

	if (opt_howto) {
		fprintf(stderr, howto_msg);
		return 0;
	}

	if (verbose || opt_version) {
		printf(version);
		if (opt_version)
			exit(0);
	}

	/* Open a basic socket. */
	if ((skfd = socket(AF_INET, SOCK_DGRAM,0)) < 0) {
		perror("socket");
		exit(-1);
	}

	if (verbose)
		fprintf(stderr, "DEBUG: argc=%d, optind=%d and argv[optind] is %s.\n",
				argc, optind, argv[optind]);

	/* No remaining args means show all interfaces. */
	if (optind == argc) {
		if_print((char *)NULL);
		(void) close(skfd);
		exit(0);
	}

	/* Copy the interface name. */
	spp = argv + optind;
	master_ifname = *spp++;
	slave_ifname = *spp++;

	/* Check command line. */
	if (opt_c) {
		char **tempp = spp;
		if ((master_ifname == NULL)||(slave_ifname == NULL)||(*tempp++ != NULL)) {
			fprintf(stderr, usage_msg);
			return 2;
		}
	}

	/* A single args means show the configuration for this interface. */
	if (slave_ifname == NULL) {
		if_print(master_ifname);
		(void) close(skfd);
		exit(0);
	}

	/* Get the vitals from the master interface. */
	{
		struct ifreq *ifra[7] = { &if_ipaddr, &if_mtu, &if_dstaddr,
								  &if_brdaddr, &if_netmask, &if_flags,
								  &if_hwaddr };
		const char *req_name[7] = {
			"IP address", "MTU", "destination address",
			"broadcast address", "netmask", "status flags",
			"hardware address" };
		const int ioctl_req_type[7] = {
			SIOCGIFADDR, SIOCGIFMTU, SIOCGIFDSTADDR,
			SIOCGIFBRDADDR, SIOCGIFNETMASK, SIOCGIFFLAGS,
			SIOCGIFHWADDR };
		int i;

		for (i = 0; i < 7; i++) {
			strncpy(ifra[i]->ifr_name, master_ifname, IFNAMSIZ);
			if (ioctl(skfd, ioctl_req_type[i], ifra[i]) < 0) {
				fprintf(stderr,
						"Something broke getting the master's %s: %s.\n",
						req_name[i], strerror(errno));
			}
		}

		hwaddr_notset = 1; /* assume master's address not set yet */
		for (i = 0; hwaddr_notset && (i < 6); i++) {
			hwaddr_notset &= ((unsigned char *)if_hwaddr.ifr_hwaddr.sa_data)[i] == 0;
		}

		/* The family '1' is ARPHRD_ETHER for ethernet. */
		if (if_hwaddr.ifr_hwaddr.sa_family != 1 && !opt_f) {
			fprintf(stderr, "The specified master interface '%s' is not"
					" ethernet-like.\n  This program is designed to work"
					" with ethernet-like network interfaces.\n"
					" Use the '-f' option to force the operation.\n",
					master_ifname);

			exit (1);
		}
		master_family = if_hwaddr.ifr_hwaddr.sa_family;
		if (verbose) {
			unsigned char *hwaddr = (unsigned char *)if_hwaddr.ifr_hwaddr.sa_data;
			printf("The current hardware address (SIOCGIFHWADDR) of %s is type %d  "
				   "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x.\n", master_ifname,
				   if_hwaddr.ifr_hwaddr.sa_family, hwaddr[0], hwaddr[1],
				   hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
		}
	}


	/* do this when enslaving interfaces */
	do {
		if (opt_d) {  /* detach a slave interface from the master */
			strncpy(if_flags.ifr_name, master_ifname, IFNAMSIZ);
			strncpy(if_flags.ifr_slave, slave_ifname, IFNAMSIZ);
			if ((ioctl(skfd, SIOCBONDRELEASE, &if_flags) < 0) &&
				(ioctl(skfd, BOND_RELEASE_OLD, &if_flags) < 0)) {
					fprintf(stderr,	"SIOCBONDRELEASE: cannot detach %s from %s. errno=%s.\n",
							slave_ifname, master_ifname, strerror(errno));
			}
			else {  /* we'll set the interface down to avoid any conflicts due to
					   same IP/MAC */
				strncpy(ifr2.ifr_name, slave_ifname, IFNAMSIZ);
				if (ioctl(skfd, SIOCGIFFLAGS, &ifr2) < 0) {
					int saved_errno = errno;
					fprintf(stderr, "SIOCGIFFLAGS on %s failed: %s\n", slave_ifname,
							strerror(saved_errno));
				}
				else {
					ifr2.ifr_flags &= ~(IFF_UP | IFF_RUNNING);
					if (ioctl(skfd, SIOCSIFFLAGS, &ifr2) < 0) {
						int saved_errno = errno;
						fprintf(stderr, "Shutting down interface %s failed: %s\n",
								slave_ifname, strerror(saved_errno));
					}
				}
			}
		}
		else {  /* attach a slave interface to the master */
			/* two possibilities :
			   - if hwaddr_notset, do nothing.  The bond will assign the
			     hwaddr from it's first slave.
			   - if !hwaddr_notset, assign the master's hwaddr to each slave
			*/

			strncpy(ifr2.ifr_name, slave_ifname, IFNAMSIZ);
			if (ioctl(skfd, SIOCGIFFLAGS, &ifr2) < 0) {
				int saved_errno = errno;
				fprintf(stderr, "SIOCGIFFLAGS on %s failed: %s\n", slave_ifname,
						strerror(saved_errno));
				return 1;
			}

			if (hwaddr_notset) { /* we do nothing */

			}
			else {  /* we'll assign master's hwaddr to this slave */
				if (ifr2.ifr_flags & IFF_UP) {
					ifr2.ifr_flags &= ~IFF_UP;
					if (ioctl(skfd, SIOCSIFFLAGS, &ifr2) < 0) {
						int saved_errno = errno;
						fprintf(stderr, "Shutting down interface %s failed: %s\n",
								slave_ifname, strerror(saved_errno));
					}
				}
	
				strncpy(if_hwaddr.ifr_name, slave_ifname, IFNAMSIZ);
				if (ioctl(skfd, SIOCSIFHWADDR, &if_hwaddr) < 0) {
					int saved_errno = errno;
					fprintf(stderr, "SIOCSIFHWADDR on %s failed: %s\n", if_hwaddr.ifr_name,
							strerror(saved_errno));
					if (saved_errno == EBUSY)
						fprintf(stderr, "  The slave device %s is busy: it must be"
								" idle before running this command.\n", slave_ifname);
					else if (saved_errno == EOPNOTSUPP)
						fprintf(stderr, "  The slave device you specified does not support"
								" setting the MAC address.\n  Your kernel likely does not"
								" support slave devices.\n");
					else if (saved_errno == EINVAL)
						fprintf(stderr, "  The slave device's address type does not match"
								" the master's address type.\n");
				} else {
					if (verbose) {
						unsigned char *hwaddr = if_hwaddr.ifr_hwaddr.sa_data;
						printf("Slave's (%s) hardware address set to "
							   "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x.\n", slave_ifname,
							   hwaddr[0], hwaddr[1], hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
					}
				}
			}
	
			if (*spp  &&  !strcmp(*spp, "metric")) {
				if (*++spp == NULL) {
					fprintf(stderr, usage_msg);
					exit(2);
				}
				if_metric.ifr_metric = atoi(*spp);
				strncpy(if_metric.ifr_name, slave_ifname, IFNAMSIZ);
				if (ioctl(skfd, SIOCSIFMETRIC, &if_metric) < 0) {
					fprintf(stderr, "SIOCSIFMETRIC on %s: %s\n", slave_ifname,
							strerror(errno));
					goterr = 1;
				}
				spp++;
			}
	
			if (strncpy(if_ipaddr.ifr_name, slave_ifname, IFNAMSIZ) <= 0
				|| ioctl(skfd, SIOCSIFADDR, &if_ipaddr) < 0) {
				fprintf(stderr,
						"Something broke setting the slave's address: %s.\n",
						strerror(errno));
			} else {
				if (verbose) {
					unsigned char *ipaddr = if_ipaddr.ifr_addr.sa_data;
					printf("Set the slave's (%s) IP address to %d.%d.%d.%d.\n",
						   slave_ifname, ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
				}
			}
	
			if (strncpy(if_mtu.ifr_name, slave_ifname, IFNAMSIZ) <= 0
				|| ioctl(skfd, SIOCSIFMTU, &if_mtu) < 0) { 
				fprintf(stderr, "Something broke setting the slave MTU: %s.\n",
						strerror(errno));
			} else {
				if (verbose)
					printf("Set the slave's (%s) MTU to %d.\n", slave_ifname, if_mtu.ifr_mtu);
			}
	
			if (strncpy(if_dstaddr.ifr_name, slave_ifname, IFNAMSIZ) <= 0
				|| ioctl(skfd, SIOCSIFDSTADDR, &if_dstaddr) < 0) {
				fprintf(stderr, "Error setting the slave (%s) with SIOCSIFDSTADDR: %s.\n",
						slave_ifname, strerror(errno));
			} else {
				if (verbose) {
					unsigned char *ipaddr = if_dstaddr.ifr_dstaddr.sa_data;
					printf("Set the slave's (%s) destination address to %d.%d.%d.%d.\n",
						   slave_ifname, ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
				}
			}
	
			if (strncpy(if_brdaddr.ifr_name, slave_ifname, IFNAMSIZ) <= 0
				|| ioctl(skfd, SIOCSIFBRDADDR, &if_brdaddr) < 0) {
				fprintf(stderr,
						"Something broke setting the slave (%s) broadcast address: %s.\n",
						slave_ifname, strerror(errno));
			} else {
				if (verbose) {
					unsigned char *ipaddr = if_brdaddr.ifr_broadaddr.sa_data;
					printf("Set the slave's (%s) broadcast address to %d.%d.%d.%d.\n",
						   slave_ifname, ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
				}
			}
			
			if (strncpy(if_netmask.ifr_name, slave_ifname, IFNAMSIZ) <= 0
				|| ioctl(skfd, SIOCSIFNETMASK, &if_netmask) < 0) {
				fprintf(stderr,
						"Something broke setting the slave (%s) netmask: %s.\n",
						slave_ifname, strerror(errno));
			} else {
				if (verbose) {
					unsigned char *ipaddr = if_netmask.ifr_netmask.sa_data;
					printf("Set the slave's (%s) netmask to %d.%d.%d.%d.\n",
						   slave_ifname, ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
				}
			}
			
			ifr2.ifr_flags |= IFF_UP; /* the interface will need to be up to be bonded */
			if ((ifr2.ifr_flags &= ~(IFF_SLAVE | IFF_MASTER)) == 0
				|| strncpy(ifr2.ifr_name, slave_ifname, IFNAMSIZ) <= 0
				|| ioctl(skfd, SIOCSIFFLAGS, &ifr2) < 0) {
				fprintf(stderr,
						"Something broke setting the slave (%s) flags: %s.\n",
						slave_ifname, strerror(errno));
			} else {
				if (verbose)
					printf("Set the slave's (%s) flags %4.4x.\n", slave_ifname, if_flags.ifr_flags);
			}
	
			/* Do the real thing */
			if ( ! opt_r) {
				strncpy(if_flags.ifr_name, master_ifname, IFNAMSIZ);
				strncpy(if_flags.ifr_slave, slave_ifname, IFNAMSIZ);
				if (!opt_c) {
					if ((ioctl(skfd, SIOCBONDENSLAVE, &if_flags) < 0) &&
					    (ioctl(skfd, BOND_ENSLAVE_OLD, &if_flags) < 0)) {
						fprintf(stderr,	"SIOCBONDENSLAVE: %s.\n", strerror(errno));
					}
				}
				else {
					if ((ioctl(skfd, SIOCBONDCHANGEACTIVE, &if_flags) < 0) &&
					    (ioctl(skfd, BOND_CHANGE_ACTIVE_OLD, &if_flags) < 0)) {
						fprintf(stderr,	"SIOCBONDCHANGEACTIVE: %s.\n", strerror(errno));
					}
				}
			}
		}
	} while ( (slave_ifname = *spp++) != NULL);

	/* Close the socket. */
	(void) close(skfd);

	return(goterr);
}

static short mif_flags;

/* Get the inteface configuration from the kernel. */
static int if_getconfig(char *ifname)
{
	struct ifreq ifr;
	int metric, mtu;			/* Parameters of the master interface. */
	struct sockaddr dstaddr, broadaddr, netmask;

	strcpy(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0)
		return -1;
	mif_flags = ifr.ifr_flags;
	printf("The result of SIOCGIFFLAGS on %s is %x.\n",
		   ifname, ifr.ifr_flags);

	strcpy(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFADDR, &ifr) < 0)
		return -1;
	printf("The result of SIOCGIFADDR is %2.2x.%2.2x.%2.2x.%2.2x.\n",
		   ifr.ifr_addr.sa_data[0], ifr.ifr_addr.sa_data[1],
		   ifr.ifr_addr.sa_data[2], ifr.ifr_addr.sa_data[3]);

	strcpy(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFHWADDR, &ifr) < 0)
		return -1;

	{
		/* Gotta convert from 'char' to unsigned for printf().  */
		unsigned char *hwaddr = (unsigned char *)ifr.ifr_hwaddr.sa_data;
		printf("The result of SIOCGIFHWADDR is type %d  "
			   "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x.\n",
			   ifr.ifr_hwaddr.sa_family, hwaddr[0], hwaddr[1],
			   hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
	}

	strcpy(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFMETRIC, &ifr) < 0) {
		metric = 0;
	} else
		metric = ifr.ifr_metric;

	strcpy(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFMTU, &ifr) < 0)
		mtu = 0;
	else
		mtu = ifr.ifr_mtu;

	strcpy(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFDSTADDR, &ifr) < 0) {
		memset(&dstaddr, 0, sizeof(struct sockaddr));
	} else
		dstaddr = ifr.ifr_dstaddr;

	strcpy(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFBRDADDR, &ifr) < 0) {
		memset(&broadaddr, 0, sizeof(struct sockaddr));
	} else
		broadaddr = ifr.ifr_broadaddr;

	strcpy(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFNETMASK, &ifr) < 0) {
		memset(&netmask, 0, sizeof(struct sockaddr));
	} else
		netmask = ifr.ifr_netmask;

	return(0);
}

static void if_print(char *ifname)
{
	char buff[1024];
	struct ifconf ifc;
	struct ifreq *ifr;
	int i;

	if (ifname == (char *)NULL) {
		ifc.ifc_len = sizeof(buff);
		ifc.ifc_buf = buff;
		if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
			fprintf(stderr, "SIOCGIFCONF: %s\n", strerror(errno));
			return;
		}

		ifr = ifc.ifc_req;
		for (i = ifc.ifc_len / sizeof(struct ifreq); --i >= 0; ifr++) {
			if (if_getconfig(ifr->ifr_name) < 0) {
				fprintf(stderr, "%s: unknown interface.\n",
						ifr->ifr_name);
				continue;
			}

			if (((mif_flags & IFF_UP) == 0) && !opt_a) continue;
			/*ife_print(&ife);*/
		}
	} else {
		if (if_getconfig(ifname) < 0)
			fprintf(stderr, "%s: unknown interface.\n", ifname);
	}
}


/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 *  compile-command: "gcc -Wall -Wstrict-prototypes -O -I/usr/src/linux/include ifenslave.c -o ifenslave"
 * End:
 */
