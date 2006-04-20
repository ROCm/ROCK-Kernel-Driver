/*
 * Serial Attached SCSI (SAS) Expander communication user space program
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * $Id: //depot/sas-class/expander_conf.c#7 $
 */

/* This is a simple program to show how to communicate with
 * expander devices in a SAS domain.
 *
 * The process is simple:
 * 1. Build the SMP frame you want to send. The format and layout
 *    is described in the SAS spec.  Leave the CRC field equal 0.
 * 2. Open the expander's SMP portal sysfs file in RW mode.
 * 3. Write the frame you built in 1.
 * 4. Read the amount of data you expect to receive for the frame you built.
 *    If you receive different amount of data you expected to receive,
 *    then there was some kind of error.
 * All this process is shown in detail in the function do_smp_func()
 * and its callers, below.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <string.h>
#include <errno.h>
#include <endian.h>
#include <byteswap.h>
#include <stdint.h>
#include <stdlib.h>

#define LEFT_FIELD_SIZE 25

#ifdef __LITTLE_ENDIAN
#define be64_to_cpu(_x)  __bswap_64(*(uint64_t *)(&(_x)))
#define be32_to_cpu(_x)  __bswap_32(*(uint32_t *)(&(_x)))
#define be16_to_cpu(_x)  __bswap_16(*(uint16_t *)(&(_x)))
#define cpu_to_be64(_x)  __bswap_64(*(uint64_t *)(&(_x)))
#define cpu_to_be32(_x)  __bswap_32(*(uint32_t *)(&(_x)))
#define cpu_to_be16(_x)  __bswap_16(*(uint16_t *)(&(_x)))
#else
#define be64_to_cpu(_x)  (_x)
#define be32_to_cpu(_x)  (_x)
#define be16_to_cpu(_x)  (_x)
#define cpu_to_be64(_x)  (_x)
#define cpu_to_be32(_x)  (_x)
#define cpu_to_be16(_x)  (_x)
#endif

#define SAS_ADDR(_x) ((unsigned long long) be64_to_cpu(*(uint64_t *)(_x)))
#define SAS_ADDR_SIZE 8

const char *prog;

struct route_table_entry {
	int      disabled;
	uint8_t  routed_sas_addr[SAS_ADDR_SIZE];
};

struct expander {
	int     num_phys;
	uint8_t *phy_attr;
	int     route_indexes;
};

static int do_smp_func(char *smp_portal_name, void *smp_req, int smp_req_size,
		       void *smp_resp, int smp_resp_size)
{
	int fd;
	ssize_t res;

	fd = open(smp_portal_name, O_RDWR);
	if (fd == -1) {
		printf("%s: opening %s: %s(%d)\n", prog, smp_portal_name,
		       strerror(errno), errno);
		return fd;
	}

	res = write(fd, smp_req, smp_req_size);
	if (!res) {
		printf("%s: nothing could be written to %s\n", prog,
		       smp_portal_name);
		goto out_err;
	} else if (res == -1) {
		printf("%s: writing to %s: %s(%d)\n", prog, smp_portal_name,
		       strerror(errno), errno);
		goto out_err;
	}

	res = read(fd, smp_resp, smp_resp_size);
	if (!res) {
		printf("%s: nothing could be read from %s\n", prog,
		       smp_portal_name);
		goto out_err;
	} else if (res == -1) {
		printf("%s: reading from %s: %s(%d)\n", prog, smp_portal_name,
		       strerror(errno), errno);
		goto out_err;
	}
	close(fd);
	return res;
 out_err:
	close(fd);
	return -1;
}

#define MI_REQ_SIZE   8
#define MI_RESP_SIZE 64

static unsigned char mi_req[MI_REQ_SIZE] = { 0x40, 1, 0, };
static unsigned char mi_resp[MI_RESP_SIZE];

#define MI_FIELD_SIZE 20
#define MI_PRINTS(a, b) printf("%*s %*s\n",LEFT_FIELD_SIZE,a,MI_FIELD_SIZE,b)
#define MI_PRINTD(a, b) printf("%*s %*u\n",LEFT_FIELD_SIZE,a,MI_FIELD_SIZE,b)
#define MI_PRINTA(a, b) printf("%*s %0*llx\n",LEFT_FIELD_SIZE,a,MI_FIELD_SIZE,b)

static int mi_expander(char *smp_portal_name, struct expander *ex)
{
	int res;

	res = do_smp_func(smp_portal_name, mi_req, MI_REQ_SIZE,
			  mi_resp, MI_RESP_SIZE);
	if (res == MI_RESP_SIZE && mi_resp[2] == 0) {
		char buf[20];

		memcpy(buf, mi_resp+12, 8);
		buf[8] = 0;
		MI_PRINTS("Vendor:", buf);

		memcpy(buf, mi_resp+20, 16);
		buf[16] = 0;
		MI_PRINTS("Product:", buf);

		memcpy(buf, mi_resp+36, 4);
		buf[4] = 0;
		MI_PRINTS("Revision:", buf);

		if (!(mi_resp[8] & 1))
			return 0;

		memcpy(buf, mi_resp+40, 8);
		buf[8] = 0;
		MI_PRINTS("Component:", buf);

		MI_PRINTD("Component ID:", be16_to_cpu(mi_resp[48]));
		MI_PRINTD("Component revision:", mi_resp[50]);
	}
	return 0;
}

#define RG_REQ_SIZE   8
#define RG_RESP_SIZE 32

static unsigned char rg_req[RG_REQ_SIZE] = { 0x40, 0, };
static unsigned char rg_resp[RG_RESP_SIZE];

static int rg_expander(char *smp_portal_name, struct expander *ex)
{
	int res;

	res = do_smp_func(smp_portal_name, rg_req, RG_REQ_SIZE, rg_resp,
			  RG_RESP_SIZE);

	if (res == RG_RESP_SIZE && rg_resp[2] == 0) {
		MI_PRINTD("Expander Change Count:", be16_to_cpu(rg_resp[4]));
		MI_PRINTD("Expander Route Indexes:", be16_to_cpu(rg_resp[6]));
		ex->route_indexes = be16_to_cpu(rg_resp[6]);
		MI_PRINTD("Number of phys:", rg_resp[9]);
		ex->num_phys = rg_resp[9];
		MI_PRINTS("Configuring:", (rg_resp[10] & 2) ? "Yes" : "No");
		MI_PRINTS("Configurable route table:",
			  (rg_resp[10] & 1) ? "Yes" : "No");
		MI_PRINTA("Enclosure Logical Identifier:",
			  SAS_ADDR(rg_resp+12));
		ex->phy_attr = malloc(ex->num_phys * sizeof(*ex->phy_attr));
	}
	return 0;
}

#define DISCOVER_REQ_SIZE  16
#define DISCOVER_RESP_SIZE 56

static unsigned char disc_req[DISCOVER_REQ_SIZE] = {0x40, 0x10, 0, };
static unsigned char disc_resp[DISCOVER_RESP_SIZE];

#define PHY_EEXIST 0x10
#define PHY_VACANT 0x16

static const char *attached_dev_type[8] = {
	[0] = "none",
	[1] = "end device",
	[2] = "edge expander",
	[3] = "fanout expander",
	[4 ... 7] = "unknown",
};

static const char *phy_link_rate[16] = {
	[0] = "unknown",
	[1] = "disabled",
	[2] = "phy reset problem",
	[3] = "spinup hold",
	[4] = "port selector",
	[5 ... 7] = "unknown",
	[8] = "G1 (1,5 Gb/s)",
	[9] = "G2 (3 GB/s)",
	[10 ... 15] = "Unknown",
};

static const char *proto_table[8] = {
	"",
	"SMP", "STP", "STP|SMP", "SSP",
	"SSP|SMP", "SSP|STP", "SSP|STP|SMP",
};

#define DIRECT_ROUTING      0
#define SUBTRACTIVE_ROUTING 1
#define TABLE_ROUTING       2

static const char *routing_attr[8] = {
	[DIRECT_ROUTING] = "D",
	[SUBTRACTIVE_ROUTING] = "S",
	[TABLE_ROUTING] = "T",
	[3 ... 7] = "x",
};

static const char *conn_type[0x80] = {
	[0] = "No information",
	[1] = "SAS external receptacle (i.e., SFF-8470)(see SAS-1.1)",
	[2] = "SAS external compact receptacle (i.e., SFF-8088)(see SAS-1.1)",
	[3 ... 0x0f ] = "External connector",
	[0x10] = "SAS internal wide plug (i.e., SFF-8484)(see SAS-1.1)",
	[0x11] = "SAS internal compact wide plug (i.e., SFF-8087)(see SAS-1.1)",
	[0x12 ... 0x1f] = "Internal wide connector",
	[0x20] = "SAS backplane receptacle (i.e., SFF-8482)(see SAS-1.1)",
	[0x21] = "SATA-style host plug (i.e., ATA/ATAPI-7 V3)(see SAS-1.1)",
	[0x22] = "SAS plug (i.e., SFF-8482)(see SAS-1.1)",
	[0x23] = "SATA device plug (i.e., ATA/ATAPI-7 V3)(see SAS-1.1)",
	[0x24 ... 0x2f] = "Internal connector to end device",
	[0x30 ... 0x6f] = "Unknown\n",
	[0x70 ... 0x7f] = "Vendor specific",
};

static int discover_phy(char *smp_portal_name, int phy_id, struct expander *ex)
{
	int res;
	const char *dev_str;

	disc_req[9] = phy_id;

	res = do_smp_func(smp_portal_name, disc_req, DISCOVER_REQ_SIZE,
			  disc_resp, DISCOVER_RESP_SIZE);

	if (res != DISCOVER_RESP_SIZE) {
		printf("%s: error disovering phy %d\n", prog, phy_id);
		goto out;
	}
	switch (disc_resp[2]) {
	case PHY_VACANT:
		printf("phy%02d: vacant\n", phy_id);
		goto out; break;
	case PHY_EEXIST:
		printf("phy%02d doesn't exist\n", phy_id);
		goto out; break;
	case 0:
		break;
	default:
		printf("phy%02d SMP function result: 0x%x\n",
		       phy_id, disc_resp[2]);
		goto out;
	}

	printf("Phy%02d:%s    attached: %016llx:%02d    chg count:%02d\n",
	       phy_id, routing_attr[disc_resp[44] & 0x0F],
	       SAS_ADDR(disc_resp+24), disc_resp[32], disc_resp[42]);

	if (ex->phy_attr)
		ex->phy_attr[phy_id] = disc_resp[44] & 0x0F;

	if (disc_resp[14] & 1)
		dev_str = "SATA Host";
	else if (disc_resp[15] & 0x80)
		dev_str = "SATA Port Selector";
	else if (disc_resp[15] & 1)
		dev_str = "SATA device";
	else
		dev_str = attached_dev_type[(disc_resp[12] & 0x70) >> 4];
	printf(" Attached device: %15s    Link rate: %15s\n",
	       dev_str, phy_link_rate[disc_resp[13] & 0xf]);

	printf(" Tproto: %15s    Iproto: %15s\n",
	       proto_table[(disc_resp[15] & 0xe) >> 1],
	       proto_table[(disc_resp[14] & 0xe) >> 1]);

	printf(" Programmed MIN-MAX linkrate: %s - %s\n",
	       phy_link_rate[(disc_resp[40] & 0xF0)>>4],
	       phy_link_rate[(disc_resp[41] & 0xF0)>>4]);

	printf(" Hardware   MIN-MAX linkrate: %s - %s\n",
	       phy_link_rate[(disc_resp[40] & 0x0F)],
	       phy_link_rate[(disc_resp[41] & 0x0F)]);

	printf(" %s phy\n", (disc_resp[43] & 0x80) ? "Virtual" : "Physical");
	printf(" Partial pathway timeout value: %d microseconds\n",
	       disc_resp[43] & 0x0F);

	printf(" Connector type: %s\n", conn_type[disc_resp[45] & 0x7F]);
	printf(" Connector element index: %d\n", disc_resp[46]);
	printf(" Connector physical link: %d\n", disc_resp[47]);
out:
	return 0;
}

#define RPEL_REQ_SIZE  16
#define RPEL_RESP_SIZE 32

static unsigned char rpel_req[RPEL_REQ_SIZE] = { 0x40, 0x11, 0, };
static unsigned char rpel_resp[RPEL_RESP_SIZE];

static int report_phy_error_log(char *smp_portal_name, int phy_id)
{
	int res;

	rpel_req[9] = phy_id;

	res = do_smp_func(smp_portal_name, rpel_req, RPEL_REQ_SIZE,
			  rpel_resp, RPEL_RESP_SIZE);
	if (res != RPEL_RESP_SIZE) {
		printf("%s: error reading error log for phy %d (%d)\n",
		       prog, phy_id, res);
		goto out;
	}
	MI_PRINTD(" Invalid DW count:", be32_to_cpu(rpel_resp[12]));
	MI_PRINTD(" RD error count:", be32_to_cpu(rpel_resp[16]));
	MI_PRINTD(" DW sync loss count:", be32_to_cpu(rpel_resp[20]));
	MI_PRINTD(" Reset problem count:", be32_to_cpu(rpel_resp[24]));
out:
	return 0;
}

#define RRI_REQ_SIZE  16
#define RRI_RESP_SIZE 44

static unsigned char rri_req[RRI_REQ_SIZE] = { 0x40, 0x13, 0, };
static unsigned char rri_resp[RRI_RESP_SIZE];

static int show_routing_table(char *smp_portal_name, int phy_id,
			      struct expander *ex)
{
	struct route_table_entry *rt;
	int res, i, last_non_zero = -1;

	if (!ex->phy_attr || ex->phy_attr[phy_id] != TABLE_ROUTING)
		return 0;

	rt = malloc(sizeof(struct route_table_entry)*ex->route_indexes);
	if (!rt)
		return 0;

	rri_req[9] = phy_id;

	for (i = 0; i < ex->route_indexes; i++) {
		*(uint16_t *)(rri_req+6) = cpu_to_be16(i);
		res = do_smp_func(smp_portal_name, rri_req, RRI_REQ_SIZE,
				  rri_resp, RRI_RESP_SIZE);
		if (res != RRI_RESP_SIZE) {
			printf("Error getting phy %d route index %d (%d)\n",
			       phy_id, i, res);
			goto out;
		}
		if (rri_resp[2] != 0)
			break;
		if (be16_to_cpu(rri_resp[6]) != i) {
			printf("Expander FW error for phy %d index %d\n",
			       phy_id, i);
			goto out;
		}
		rt[i].disabled = rri_resp[12] & 0x80 ? 1 : 0;
		memcpy(rt[i].routed_sas_addr, rri_resp+16, SAS_ADDR_SIZE);
		if (rt[i].routed_sas_addr[0])
			last_non_zero = i;
	}
	printf(" Routing Table\n");
	if (last_non_zero == -1)
		printf("  Empty (all zero)\n");
	else {
		for (i = 0; i <= last_non_zero; i++)
			printf("  %02d %8s %016llx\n",
			       i, rt[i].disabled ? "disabled" : "enabled",
			       SAS_ADDR(rt[i].routed_sas_addr));
	}
out:
	free(rt);
	return 0;
}

static int discover_expander(char *smp_portal_name, struct expander *ex)
{
	int i;

	for (i = 0; i < ex->num_phys; i++) {
		printf("\n");
		discover_phy(smp_portal_name, i, ex);
		report_phy_error_log(smp_portal_name, i);
		show_routing_table(smp_portal_name, i, ex);
	}

	return 0;
}

static void print_info(void)
{
	printf("%s <smp portal file>\n", prog);
	printf("\tWhere <smp portal file> is the binary attribute file of the"
	       "\n\texpander device in sysfs.\n");
}

int main(int argc, char *argv[])
{
	prog = strrchr(argv[0], '/');
	if (prog)
		prog++;
	else
		prog = argv[0];

	if (argc < 2) {
		print_info();
		return 0;
	} else {
		struct expander ex;

		memset(&ex, 0, sizeof(ex));

		mi_expander(argv[1], &ex);
		rg_expander(argv[1], &ex);
		discover_expander(argv[1], &ex);
		if (ex.phy_attr)
			free(ex.phy_attr);
	}
	return 0;
}
