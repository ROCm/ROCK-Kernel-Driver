// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 Intel Corporation. */

/*
 * Some functions in this program are taken from
 * Linux kernel samples/bpf/xdpsock* and modified
 * for use.
 *
 * See test_xsk.sh for detailed information on test topology
 * and prerequisite network setup.
 *
 * This test program contains two threads, each thread is single socket with
 * a unique UMEM. It validates in-order packet delivery and packet content
 * by sending packets to each other.
 *
 * Tests Information:
 * ------------------
 * These selftests test AF_XDP SKB and Native/DRV modes using veth
 * Virtual Ethernet interfaces.
 *
 * For each mode, the following tests are run:
 *    a. nopoll - soft-irq processing in run-to-completion mode
 *    b. poll - using poll() syscall
 *    c. Socket Teardown
 *       Create a Tx and a Rx socket, Tx from one socket, Rx on another. Destroy
 *       both sockets, then repeat multiple times. Only nopoll mode is used
 *    d. Bi-directional sockets
 *       Configure sockets as bi-directional tx/rx sockets, sets up fill and
 *       completion rings on each socket, tx/rx in both directions. Only nopoll
 *       mode is used
 *    e. Statistics
 *       Trigger some error conditions and ensure that the appropriate statistics
 *       are incremented. Within this test, the following statistics are tested:
 *       i.   rx dropped
 *            Increase the UMEM frame headroom to a value which results in
 *            insufficient space in the rx buffer for both the packet and the headroom.
 *       ii.  tx invalid
 *            Set the 'len' field of tx descriptors to an invalid value (umem frame
 *            size + 1).
 *       iii. rx ring full
 *            Reduce the size of the RX ring to a fraction of the fill ring size.
 *       iv.  fill queue empty
 *            Do not populate the fill queue and then try to receive pkts.
 *    f. bpf_link resource persistence
 *       Configure sockets at indexes 0 and 1, run a traffic on queue ids 0,
 *       then remove xsk sockets from queue 0 on both veth interfaces and
 *       finally run a traffic on queues ids 1
 *    g. unaligned mode
 *    h. tests for invalid and corner case Tx descriptors so that the correct ones
 *       are discarded and let through, respectively.
 *    i. 2K frame size tests
 *
 * Total tests: 12
 *
 * Flow:
 * -----
 * - Single process spawns two threads: Tx and Rx
 * - Each of these two threads attach to a veth interface
 * - Each thread creates one AF_XDP socket connected to a unique umem for each
 *   veth interface
 * - Tx thread Transmits a number of packets from veth<xxxx> to veth<yyyy>
 * - Rx thread verifies if all packets were received and delivered in-order,
 *   and have the right content
 *
 * Enable/disable packet dump mode:
 * --------------------------
 * To enable L2 - L4 headers and payload dump of each packet on STDOUT, add
 * parameter -D to params array in test_xsk.sh, i.e. params=("-S" "-D")
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <asm/barrier.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#include "xsk_xdp_progs.skel.h"
#include "xsk.h"
#include "xskxceiver.h"
#include <bpf/bpf.h>
#include <linux/filter.h>
#include "../kselftest.h"

static const char *MAC1 = "\x00\x0A\x56\x9E\xEE\x62";
static const char *MAC2 = "\x00\x0A\x56\x9E\xEE\x61";
static const char *IP1 = "192.168.100.162";
static const char *IP2 = "192.168.100.161";
static const u16 UDP_PORT1 = 2020;
static const u16 UDP_PORT2 = 2121;

static void __exit_with_error(int error, const char *file, const char *func, int line)
{
	ksft_test_result_fail("[%s:%s:%i]: ERROR: %d/\"%s\"\n", file, func, line, error,
			      strerror(error));
	ksft_exit_xfail();
}

#define exit_with_error(error) __exit_with_error(error, __FILE__, __func__, __LINE__)
#define busy_poll_string(test) (test)->ifobj_tx->busy_poll ? "BUSY-POLL " : ""
static char *mode_string(struct test_spec *test)
{
	switch (test->mode) {
	case TEST_MODE_SKB:
		return "SKB";
	case TEST_MODE_DRV:
		return "DRV";
	case TEST_MODE_ZC:
		return "ZC";
	default:
		return "BOGUS";
	}
}

static void report_failure(struct test_spec *test)
{
	if (test->fail)
		return;

	ksft_test_result_fail("FAIL: %s %s%s\n", mode_string(test), busy_poll_string(test),
			      test->name);
	test->fail = true;
}

static void memset32_htonl(void *dest, u32 val, u32 size)
{
	u32 *ptr = (u32 *)dest;
	int i;

	val = htonl(val);

	for (i = 0; i < (size & (~0x3)); i += 4)
		ptr[i >> 2] = val;
}

/*
 * Fold a partial checksum
 * This function code has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static __u16 csum_fold(__u32 csum)
{
	u32 sum = (__force u32)csum;

	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return (__force __u16)~sum;
}

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static u32 from64to32(u64 x)
{
	/* add up 32-bit and 32-bit for 32+c bit */
	x = (x & 0xffffffff) + (x >> 32);
	/* add up carry.. */
	x = (x & 0xffffffff) + (x >> 32);
	return (u32)x;
}

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static __u32 csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len, __u8 proto, __u32 sum)
{
	unsigned long long s = (__force u32)sum;

	s += (__force u32)saddr;
	s += (__force u32)daddr;
#ifdef __BIG_ENDIAN__
	s += proto + len;
#else
	s += (proto + len) << 8;
#endif
	return (__force __u32)from64to32(s);
}

/*
 * This function has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static __u16 csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len, __u8 proto, __u32 sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

static u16 udp_csum(u32 saddr, u32 daddr, u32 len, u8 proto, u16 *udp_pkt)
{
	u32 csum = 0;
	u32 cnt = 0;

	/* udp hdr and data */
	for (; cnt < len; cnt += 2)
		csum += udp_pkt[cnt >> 1];

	return csum_tcpudp_magic(saddr, daddr, len, proto, csum);
}

static void gen_eth_hdr(struct ifobject *ifobject, struct ethhdr *eth_hdr)
{
	memcpy(eth_hdr->h_dest, ifobject->dst_mac, ETH_ALEN);
	memcpy(eth_hdr->h_source, ifobject->src_mac, ETH_ALEN);
	eth_hdr->h_proto = htons(ETH_P_IP);
}

static void gen_ip_hdr(struct ifobject *ifobject, struct iphdr *ip_hdr)
{
	ip_hdr->version = IP_PKT_VER;
	ip_hdr->ihl = 0x5;
	ip_hdr->tos = IP_PKT_TOS;
	ip_hdr->tot_len = htons(IP_PKT_SIZE);
	ip_hdr->id = 0;
	ip_hdr->frag_off = 0;
	ip_hdr->ttl = IPDEFTTL;
	ip_hdr->protocol = IPPROTO_UDP;
	ip_hdr->saddr = ifobject->src_ip;
	ip_hdr->daddr = ifobject->dst_ip;
	ip_hdr->check = 0;
}

static void gen_udp_hdr(u32 payload, void *pkt, struct ifobject *ifobject,
			struct udphdr *udp_hdr)
{
	udp_hdr->source = htons(ifobject->src_port);
	udp_hdr->dest = htons(ifobject->dst_port);
	udp_hdr->len = htons(UDP_PKT_SIZE);
	memset32_htonl(pkt + PKT_HDR_SIZE, payload, UDP_PKT_DATA_SIZE);
}

static bool is_umem_valid(struct ifobject *ifobj)
{
	return !!ifobj->umem->umem;
}

static void gen_udp_csum(struct udphdr *udp_hdr, struct iphdr *ip_hdr)
{
	udp_hdr->check = 0;
	udp_hdr->check =
	    udp_csum(ip_hdr->saddr, ip_hdr->daddr, UDP_PKT_SIZE, IPPROTO_UDP, (u16 *)udp_hdr);
}

static u32 mode_to_xdp_flags(enum test_mode mode)
{
	return (mode == TEST_MODE_SKB) ? XDP_FLAGS_SKB_MODE : XDP_FLAGS_DRV_MODE;
}

static int xsk_configure_umem(struct xsk_umem_info *umem, void *buffer, u64 size)
{
	struct xsk_umem_config cfg = {
		.fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
		.frame_size = umem->frame_size,
		.frame_headroom = umem->frame_headroom,
		.flags = XSK_UMEM__DEFAULT_FLAGS
	};
	int ret;

	if (umem->unaligned_mode)
		cfg.flags |= XDP_UMEM_UNALIGNED_CHUNK_FLAG;

	ret = xsk_umem__create(&umem->umem, buffer, size,
			       &umem->fq, &umem->cq, &cfg);
	if (ret)
		return ret;

	umem->buffer = buffer;
	return 0;
}

static void enable_busy_poll(struct xsk_socket_info *xsk)
{
	int sock_opt;

	sock_opt = 1;
	if (setsockopt(xsk_socket__fd(xsk->xsk), SOL_SOCKET, SO_PREFER_BUSY_POLL,
		       (void *)&sock_opt, sizeof(sock_opt)) < 0)
		exit_with_error(errno);

	sock_opt = 20;
	if (setsockopt(xsk_socket__fd(xsk->xsk), SOL_SOCKET, SO_BUSY_POLL,
		       (void *)&sock_opt, sizeof(sock_opt)) < 0)
		exit_with_error(errno);

	sock_opt = BATCH_SIZE;
	if (setsockopt(xsk_socket__fd(xsk->xsk), SOL_SOCKET, SO_BUSY_POLL_BUDGET,
		       (void *)&sock_opt, sizeof(sock_opt)) < 0)
		exit_with_error(errno);
}

static int __xsk_configure_socket(struct xsk_socket_info *xsk, struct xsk_umem_info *umem,
				  struct ifobject *ifobject, bool shared)
{
	struct xsk_socket_config cfg = {};
	struct xsk_ring_cons *rxr;
	struct xsk_ring_prod *txr;

	xsk->umem = umem;
	cfg.rx_size = xsk->rxqsize;
	cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
	cfg.bind_flags = ifobject->bind_flags;
	if (shared)
		cfg.bind_flags |= XDP_SHARED_UMEM;

	txr = ifobject->tx_on ? &xsk->tx : NULL;
	rxr = ifobject->rx_on ? &xsk->rx : NULL;
	return xsk_socket__create(&xsk->xsk, ifobject->ifindex, 0, umem->umem, rxr, txr, &cfg);
}

static bool ifobj_zc_avail(struct ifobject *ifobject)
{
	size_t umem_sz = DEFAULT_UMEM_BUFFERS * XSK_UMEM__DEFAULT_FRAME_SIZE;
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
	struct xsk_socket_info *xsk;
	struct xsk_umem_info *umem;
	bool zc_avail = false;
	void *bufs;
	int ret;

	bufs = mmap(NULL, umem_sz, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
	if (bufs == MAP_FAILED)
		exit_with_error(errno);

	umem = calloc(1, sizeof(struct xsk_umem_info));
	if (!umem) {
		munmap(bufs, umem_sz);
		exit_with_error(ENOMEM);
	}
	umem->frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE;
	ret = xsk_configure_umem(umem, bufs, umem_sz);
	if (ret)
		exit_with_error(-ret);

	xsk = calloc(1, sizeof(struct xsk_socket_info));
	if (!xsk)
		goto out;
	ifobject->bind_flags = XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY;
	ifobject->rx_on = true;
	xsk->rxqsize = XSK_RING_CONS__DEFAULT_NUM_DESCS;
	ret = __xsk_configure_socket(xsk, umem, ifobject, false);
	if (!ret)
		zc_avail = true;

	xsk_socket__delete(xsk->xsk);
	free(xsk);
out:
	munmap(umem->buffer, umem_sz);
	xsk_umem__delete(umem->umem);
	free(umem);
	return zc_avail;
}

static struct option long_options[] = {
	{"interface", required_argument, 0, 'i'},
	{"busy-poll", no_argument, 0, 'b'},
	{"dump-pkts", no_argument, 0, 'D'},
	{"verbose", no_argument, 0, 'v'},
	{0, 0, 0, 0}
};

static void usage(const char *prog)
{
	const char *str =
		"  Usage: %s [OPTIONS]\n"
		"  Options:\n"
		"  -i, --interface      Use interface\n"
		"  -D, --dump-pkts      Dump packets L2 - L5\n"
		"  -v, --verbose        Verbose output\n"
		"  -b, --busy-poll      Enable busy poll\n";

	ksft_print_msg(str, prog);
}

static bool validate_interface(struct ifobject *ifobj)
{
	if (!strcmp(ifobj->ifname, ""))
		return false;
	return true;
}

static void parse_command_line(struct ifobject *ifobj_tx, struct ifobject *ifobj_rx, int argc,
			       char **argv)
{
	struct ifobject *ifobj;
	u32 interface_nb = 0;
	int option_index, c;

	opterr = 0;

	for (;;) {
		c = getopt_long(argc, argv, "i:Dvb", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			if (interface_nb == 0)
				ifobj = ifobj_tx;
			else if (interface_nb == 1)
				ifobj = ifobj_rx;
			else
				break;

			memcpy(ifobj->ifname, optarg,
			       min_t(size_t, MAX_INTERFACE_NAME_CHARS, strlen(optarg)));

			ifobj->ifindex = if_nametoindex(ifobj->ifname);
			if (!ifobj->ifindex)
				exit_with_error(errno);

			interface_nb++;
			break;
		case 'D':
			opt_pkt_dump = true;
			break;
		case 'v':
			opt_verbose = true;
			break;
		case 'b':
			ifobj_tx->busy_poll = true;
			ifobj_rx->busy_poll = true;
			break;
		default:
			usage(basename(argv[0]));
			ksft_exit_xfail();
		}
	}
}

static void __test_spec_init(struct test_spec *test, struct ifobject *ifobj_tx,
			     struct ifobject *ifobj_rx)
{
	u32 i, j;

	for (i = 0; i < MAX_INTERFACES; i++) {
		struct ifobject *ifobj = i ? ifobj_rx : ifobj_tx;

		ifobj->xsk = &ifobj->xsk_arr[0];
		ifobj->use_poll = false;
		ifobj->use_fill_ring = true;
		ifobj->release_rx = true;
		ifobj->validation_func = NULL;

		if (i == 0) {
			ifobj->rx_on = false;
			ifobj->tx_on = true;
			ifobj->pkt_stream = test->tx_pkt_stream_default;
		} else {
			ifobj->rx_on = true;
			ifobj->tx_on = false;
			ifobj->pkt_stream = test->rx_pkt_stream_default;
		}

		memset(ifobj->umem, 0, sizeof(*ifobj->umem));
		ifobj->umem->num_frames = DEFAULT_UMEM_BUFFERS;
		ifobj->umem->frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE;
		if (ifobj->shared_umem && ifobj->rx_on)
			ifobj->umem->base_addr = DEFAULT_UMEM_BUFFERS *
				XSK_UMEM__DEFAULT_FRAME_SIZE;

		for (j = 0; j < MAX_SOCKETS; j++) {
			memset(&ifobj->xsk_arr[j], 0, sizeof(ifobj->xsk_arr[j]));
			ifobj->xsk_arr[j].rxqsize = XSK_RING_CONS__DEFAULT_NUM_DESCS;
		}
	}

	test->ifobj_tx = ifobj_tx;
	test->ifobj_rx = ifobj_rx;
	test->current_step = 0;
	test->total_steps = 1;
	test->nb_sockets = 1;
	test->fail = false;
	test->xdp_prog_rx = ifobj_rx->xdp_progs->progs.xsk_def_prog;
	test->xskmap_rx = ifobj_rx->xdp_progs->maps.xsk;
	test->xdp_prog_tx = ifobj_tx->xdp_progs->progs.xsk_def_prog;
	test->xskmap_tx = ifobj_tx->xdp_progs->maps.xsk;
}

static void test_spec_init(struct test_spec *test, struct ifobject *ifobj_tx,
			   struct ifobject *ifobj_rx, enum test_mode mode)
{
	struct pkt_stream *tx_pkt_stream;
	struct pkt_stream *rx_pkt_stream;
	u32 i;

	tx_pkt_stream = test->tx_pkt_stream_default;
	rx_pkt_stream = test->rx_pkt_stream_default;
	memset(test, 0, sizeof(*test));
	test->tx_pkt_stream_default = tx_pkt_stream;
	test->rx_pkt_stream_default = rx_pkt_stream;

	for (i = 0; i < MAX_INTERFACES; i++) {
		struct ifobject *ifobj = i ? ifobj_rx : ifobj_tx;

		ifobj->bind_flags = XDP_USE_NEED_WAKEUP;
		if (mode == TEST_MODE_ZC)
			ifobj->bind_flags |= XDP_ZEROCOPY;
		else
			ifobj->bind_flags |= XDP_COPY;
	}

	test->mode = mode;
	__test_spec_init(test, ifobj_tx, ifobj_rx);
}

static void test_spec_reset(struct test_spec *test)
{
	__test_spec_init(test, test->ifobj_tx, test->ifobj_rx);
}

static void test_spec_set_name(struct test_spec *test, const char *name)
{
	strncpy(test->name, name, MAX_TEST_NAME_SIZE);
}

static void test_spec_set_xdp_prog(struct test_spec *test, struct bpf_program *xdp_prog_rx,
				   struct bpf_program *xdp_prog_tx, struct bpf_map *xskmap_rx,
				   struct bpf_map *xskmap_tx)
{
	test->xdp_prog_rx = xdp_prog_rx;
	test->xdp_prog_tx = xdp_prog_tx;
	test->xskmap_rx = xskmap_rx;
	test->xskmap_tx = xskmap_tx;
}

static void pkt_stream_reset(struct pkt_stream *pkt_stream)
{
	if (pkt_stream)
		pkt_stream->rx_pkt_nb = 0;
}

static struct pkt *pkt_stream_get_pkt(struct pkt_stream *pkt_stream, u32 pkt_nb)
{
	if (pkt_nb >= pkt_stream->nb_pkts)
		return NULL;

	return &pkt_stream->pkts[pkt_nb];
}

static struct pkt *pkt_stream_get_next_rx_pkt(struct pkt_stream *pkt_stream, u32 *pkts_sent)
{
	while (pkt_stream->rx_pkt_nb < pkt_stream->nb_pkts) {
		(*pkts_sent)++;
		if (pkt_stream->pkts[pkt_stream->rx_pkt_nb].valid)
			return &pkt_stream->pkts[pkt_stream->rx_pkt_nb++];
		pkt_stream->rx_pkt_nb++;
	}
	return NULL;
}

static void pkt_stream_delete(struct pkt_stream *pkt_stream)
{
	free(pkt_stream->pkts);
	free(pkt_stream);
}

static void pkt_stream_restore_default(struct test_spec *test)
{
	struct pkt_stream *tx_pkt_stream = test->ifobj_tx->pkt_stream;
	struct pkt_stream *rx_pkt_stream = test->ifobj_rx->pkt_stream;

	if (tx_pkt_stream != test->tx_pkt_stream_default) {
		pkt_stream_delete(test->ifobj_tx->pkt_stream);
		test->ifobj_tx->pkt_stream = test->tx_pkt_stream_default;
	}

	if (rx_pkt_stream != test->rx_pkt_stream_default) {
		pkt_stream_delete(test->ifobj_rx->pkt_stream);
		test->ifobj_rx->pkt_stream = test->rx_pkt_stream_default;
	}
}

static struct pkt_stream *__pkt_stream_alloc(u32 nb_pkts)
{
	struct pkt_stream *pkt_stream;

	pkt_stream = calloc(1, sizeof(*pkt_stream));
	if (!pkt_stream)
		return NULL;

	pkt_stream->pkts = calloc(nb_pkts, sizeof(*pkt_stream->pkts));
	if (!pkt_stream->pkts) {
		free(pkt_stream);
		return NULL;
	}

	pkt_stream->nb_pkts = nb_pkts;
	return pkt_stream;
}

static void pkt_set(struct xsk_umem_info *umem, struct pkt *pkt, u64 addr, u32 len)
{
	pkt->addr = addr + umem->base_addr;
	pkt->len = len;
	if (len > umem->frame_size - XDP_PACKET_HEADROOM - MIN_PKT_SIZE * 2 - umem->frame_headroom)
		pkt->valid = false;
	else
		pkt->valid = true;
}

static struct pkt_stream *pkt_stream_generate(struct xsk_umem_info *umem, u32 nb_pkts, u32 pkt_len)
{
	struct pkt_stream *pkt_stream;
	u32 i;

	pkt_stream = __pkt_stream_alloc(nb_pkts);
	if (!pkt_stream)
		exit_with_error(ENOMEM);

	for (i = 0; i < nb_pkts; i++) {
		pkt_set(umem, &pkt_stream->pkts[i], (i % umem->num_frames) * umem->frame_size,
			pkt_len);
		pkt_stream->pkts[i].payload = i;
	}

	return pkt_stream;
}

static struct pkt_stream *pkt_stream_clone(struct xsk_umem_info *umem,
					   struct pkt_stream *pkt_stream)
{
	return pkt_stream_generate(umem, pkt_stream->nb_pkts, pkt_stream->pkts[0].len);
}

static void pkt_stream_replace(struct test_spec *test, u32 nb_pkts, u32 pkt_len)
{
	struct pkt_stream *pkt_stream;

	pkt_stream = pkt_stream_generate(test->ifobj_tx->umem, nb_pkts, pkt_len);
	test->ifobj_tx->pkt_stream = pkt_stream;
	pkt_stream = pkt_stream_generate(test->ifobj_rx->umem, nb_pkts, pkt_len);
	test->ifobj_rx->pkt_stream = pkt_stream;
}

static void __pkt_stream_replace_half(struct ifobject *ifobj, u32 pkt_len,
				      int offset)
{
	struct xsk_umem_info *umem = ifobj->umem;
	struct pkt_stream *pkt_stream;
	u32 i;

	pkt_stream = pkt_stream_clone(umem, ifobj->pkt_stream);
	for (i = 1; i < ifobj->pkt_stream->nb_pkts; i += 2)
		pkt_set(umem, &pkt_stream->pkts[i],
			(i % umem->num_frames) * umem->frame_size + offset, pkt_len);

	ifobj->pkt_stream = pkt_stream;
}

static void pkt_stream_replace_half(struct test_spec *test, u32 pkt_len, int offset)
{
	__pkt_stream_replace_half(test->ifobj_tx, pkt_len, offset);
	__pkt_stream_replace_half(test->ifobj_rx, pkt_len, offset);
}

static void pkt_stream_receive_half(struct test_spec *test)
{
	struct xsk_umem_info *umem = test->ifobj_rx->umem;
	struct pkt_stream *pkt_stream = test->ifobj_tx->pkt_stream;
	u32 i;

	test->ifobj_rx->pkt_stream = pkt_stream_generate(umem, pkt_stream->nb_pkts,
							 pkt_stream->pkts[0].len);
	pkt_stream = test->ifobj_rx->pkt_stream;
	for (i = 1; i < pkt_stream->nb_pkts; i += 2)
		pkt_stream->pkts[i].valid = false;
}

static struct pkt *pkt_generate(struct ifobject *ifobject, u32 pkt_nb)
{
	struct pkt *pkt = pkt_stream_get_pkt(ifobject->pkt_stream, pkt_nb);
	struct udphdr *udp_hdr;
	struct ethhdr *eth_hdr;
	struct iphdr *ip_hdr;
	void *data;

	if (!pkt)
		return NULL;
	if (!pkt->valid || pkt->len < MIN_PKT_SIZE)
		return pkt;

	data = xsk_umem__get_data(ifobject->umem->buffer, pkt->addr);
	udp_hdr = (struct udphdr *)(data + sizeof(struct ethhdr) + sizeof(struct iphdr));
	ip_hdr = (struct iphdr *)(data + sizeof(struct ethhdr));
	eth_hdr = (struct ethhdr *)data;

	gen_udp_hdr(pkt_nb, data, ifobject, udp_hdr);
	gen_ip_hdr(ifobject, ip_hdr);
	gen_udp_csum(udp_hdr, ip_hdr);
	gen_eth_hdr(ifobject, eth_hdr);

	return pkt;
}

static void __pkt_stream_generate_custom(struct ifobject *ifobj,
					 struct pkt *pkts, u32 nb_pkts)
{
	struct pkt_stream *pkt_stream;
	u32 i;

	pkt_stream = __pkt_stream_alloc(nb_pkts);
	if (!pkt_stream)
		exit_with_error(ENOMEM);

	for (i = 0; i < nb_pkts; i++) {
		pkt_stream->pkts[i].addr = pkts[i].addr + ifobj->umem->base_addr;
		pkt_stream->pkts[i].len = pkts[i].len;
		pkt_stream->pkts[i].payload = i;
		pkt_stream->pkts[i].valid = pkts[i].valid;
	}

	ifobj->pkt_stream = pkt_stream;
}

static void pkt_stream_generate_custom(struct test_spec *test, struct pkt *pkts, u32 nb_pkts)
{
	__pkt_stream_generate_custom(test->ifobj_tx, pkts, nb_pkts);
	__pkt_stream_generate_custom(test->ifobj_rx, pkts, nb_pkts);
}

static void pkt_dump(void *pkt, u32 len)
{
	char s[INET_ADDRSTRLEN];
	struct ethhdr *ethhdr;
	struct udphdr *udphdr;
	struct iphdr *iphdr;
	u32 payload, i;

	ethhdr = pkt;
	iphdr = pkt + sizeof(*ethhdr);
	udphdr = pkt + sizeof(*ethhdr) + sizeof(*iphdr);

	/*extract L2 frame */
	fprintf(stdout, "DEBUG>> L2: dst mac: ");
	for (i = 0; i < ETH_ALEN; i++)
		fprintf(stdout, "%02X", ethhdr->h_dest[i]);

	fprintf(stdout, "\nDEBUG>> L2: src mac: ");
	for (i = 0; i < ETH_ALEN; i++)
		fprintf(stdout, "%02X", ethhdr->h_source[i]);

	/*extract L3 frame */
	fprintf(stdout, "\nDEBUG>> L3: ip_hdr->ihl: %02X\n", iphdr->ihl);
	fprintf(stdout, "DEBUG>> L3: ip_hdr->saddr: %s\n",
		inet_ntop(AF_INET, &iphdr->saddr, s, sizeof(s)));
	fprintf(stdout, "DEBUG>> L3: ip_hdr->daddr: %s\n",
		inet_ntop(AF_INET, &iphdr->daddr, s, sizeof(s)));
	/*extract L4 frame */
	fprintf(stdout, "DEBUG>> L4: udp_hdr->src: %d\n", ntohs(udphdr->source));
	fprintf(stdout, "DEBUG>> L4: udp_hdr->dst: %d\n", ntohs(udphdr->dest));
	/*extract L5 frame */
	payload = ntohl(*((u32 *)(pkt + PKT_HDR_SIZE)));

	fprintf(stdout, "DEBUG>> L5: payload: %d\n", payload);
	fprintf(stdout, "---------------------------------------\n");
}

static bool is_offset_correct(struct xsk_umem_info *umem, struct pkt_stream *pkt_stream, u64 addr,
			      u64 pkt_stream_addr)
{
	u32 headroom = umem->unaligned_mode ? 0 : umem->frame_headroom;
	u32 offset = addr % umem->frame_size, expected_offset = 0;

	if (!pkt_stream->use_addr_for_fill)
		pkt_stream_addr = 0;

	expected_offset += (pkt_stream_addr + headroom + XDP_PACKET_HEADROOM) % umem->frame_size;

	if (offset == expected_offset)
		return true;

	ksft_print_msg("[%s] expected [%u], got [%u]\n", __func__, expected_offset, offset);
	return false;
}

static bool is_pkt_valid(struct pkt *pkt, void *buffer, u64 addr, u32 len)
{
	void *data = xsk_umem__get_data(buffer, addr);
	struct iphdr *iphdr = (struct iphdr *)(data + sizeof(struct ethhdr));

	if (!pkt) {
		ksft_print_msg("[%s] too many packets received\n", __func__);
		return false;
	}

	if (len < MIN_PKT_SIZE || pkt->len < MIN_PKT_SIZE) {
		/* Do not try to verify packets that are smaller than minimum size. */
		return true;
	}

	if (pkt->len != len) {
		ksft_print_msg("[%s] expected length [%d], got length [%d]\n",
			       __func__, pkt->len, len);
		return false;
	}

	if (iphdr->version == IP_PKT_VER && iphdr->tos == IP_PKT_TOS) {
		u32 seqnum = ntohl(*((u32 *)(data + PKT_HDR_SIZE)));

		if (opt_pkt_dump)
			pkt_dump(data, PKT_SIZE);

		if (pkt->payload != seqnum) {
			ksft_print_msg("[%s] expected seqnum [%d], got seqnum [%d]\n",
				       __func__, pkt->payload, seqnum);
			return false;
		}
	} else {
		ksft_print_msg("Invalid frame received: ");
		ksft_print_msg("[IP_PKT_VER: %02X], [IP_PKT_TOS: %02X]\n", iphdr->version,
			       iphdr->tos);
		return false;
	}

	return true;
}

static void kick_tx(struct xsk_socket_info *xsk)
{
	int ret;

	ret = sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
	if (ret >= 0)
		return;
	if (errno == ENOBUFS || errno == EAGAIN || errno == EBUSY || errno == ENETDOWN) {
		usleep(100);
		return;
	}
	exit_with_error(errno);
}

static void kick_rx(struct xsk_socket_info *xsk)
{
	int ret;

	ret = recvfrom(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, NULL);
	if (ret < 0)
		exit_with_error(errno);
}

static int complete_pkts(struct xsk_socket_info *xsk, int batch_size)
{
	unsigned int rcvd;
	u32 idx;

	if (xsk_ring_prod__needs_wakeup(&xsk->tx))
		kick_tx(xsk);

	rcvd = xsk_ring_cons__peek(&xsk->umem->cq, batch_size, &idx);
	if (rcvd) {
		if (rcvd > xsk->outstanding_tx) {
			u64 addr = *xsk_ring_cons__comp_addr(&xsk->umem->cq, idx + rcvd - 1);

			ksft_print_msg("[%s] Too many packets completed\n", __func__);
			ksft_print_msg("Last completion address: %llx\n", addr);
			return TEST_FAILURE;
		}

		xsk_ring_cons__release(&xsk->umem->cq, rcvd);
		xsk->outstanding_tx -= rcvd;
	}

	return TEST_PASS;
}

static int receive_pkts(struct test_spec *test, struct pollfd *fds)
{
	struct timeval tv_end, tv_now, tv_timeout = {THREAD_TMOUT, 0};
	struct pkt_stream *pkt_stream = test->ifobj_rx->pkt_stream;
	u32 idx_rx = 0, idx_fq = 0, rcvd, i, pkts_sent = 0;
	struct xsk_socket_info *xsk = test->ifobj_rx->xsk;
	struct ifobject *ifobj = test->ifobj_rx;
	struct xsk_umem_info *umem = xsk->umem;
	struct pkt *pkt;
	int ret;

	ret = gettimeofday(&tv_now, NULL);
	if (ret)
		exit_with_error(errno);
	timeradd(&tv_now, &tv_timeout, &tv_end);

	pkt = pkt_stream_get_next_rx_pkt(pkt_stream, &pkts_sent);
	while (pkt) {
		ret = gettimeofday(&tv_now, NULL);
		if (ret)
			exit_with_error(errno);
		if (timercmp(&tv_now, &tv_end, >)) {
			ksft_print_msg("ERROR: [%s] Receive loop timed out\n", __func__);
			return TEST_FAILURE;
		}

		kick_rx(xsk);
		if (ifobj->use_poll) {
			ret = poll(fds, 1, POLL_TMOUT);
			if (ret < 0)
				exit_with_error(errno);

			if (!ret) {
				if (!is_umem_valid(test->ifobj_tx))
					return TEST_PASS;

				ksft_print_msg("ERROR: [%s] Poll timed out\n", __func__);
				return TEST_FAILURE;

			}

			if (!(fds->revents & POLLIN))
				continue;
		}

		rcvd = xsk_ring_cons__peek(&xsk->rx, BATCH_SIZE, &idx_rx);
		if (!rcvd)
			continue;

		if (ifobj->use_fill_ring) {
			ret = xsk_ring_prod__reserve(&umem->fq, rcvd, &idx_fq);
			while (ret != rcvd) {
				if (ret < 0)
					exit_with_error(-ret);
				if (xsk_ring_prod__needs_wakeup(&umem->fq)) {
					ret = poll(fds, 1, POLL_TMOUT);
					if (ret < 0)
						exit_with_error(errno);
				}
				ret = xsk_ring_prod__reserve(&umem->fq, rcvd, &idx_fq);
			}
		}

		for (i = 0; i < rcvd; i++) {
			const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++);
			u64 addr = desc->addr, orig;

			orig = xsk_umem__extract_addr(addr);
			addr = xsk_umem__add_offset_to_addr(addr);

			if (!is_pkt_valid(pkt, umem->buffer, addr, desc->len) ||
			    !is_offset_correct(umem, pkt_stream, addr, pkt->addr))
				return TEST_FAILURE;

			if (ifobj->use_fill_ring)
				*xsk_ring_prod__fill_addr(&umem->fq, idx_fq++) = orig;
			pkt = pkt_stream_get_next_rx_pkt(pkt_stream, &pkts_sent);
		}

		if (ifobj->use_fill_ring)
			xsk_ring_prod__submit(&umem->fq, rcvd);
		if (ifobj->release_rx)
			xsk_ring_cons__release(&xsk->rx, rcvd);

		pthread_mutex_lock(&pacing_mutex);
		pkts_in_flight -= pkts_sent;
		if (pkts_in_flight < umem->num_frames)
			pthread_cond_signal(&pacing_cond);
		pthread_mutex_unlock(&pacing_mutex);
		pkts_sent = 0;
	}

	return TEST_PASS;
}

static int __send_pkts(struct ifobject *ifobject, u32 *pkt_nb, struct pollfd *fds,
		       bool timeout)
{
	struct xsk_socket_info *xsk = ifobject->xsk;
	bool use_poll = ifobject->use_poll;
	u32 i, idx = 0, valid_pkts = 0;
	int ret;

	while (xsk_ring_prod__reserve(&xsk->tx, BATCH_SIZE, &idx) < BATCH_SIZE) {
		if (use_poll) {
			ret = poll(fds, 1, POLL_TMOUT);
			if (timeout) {
				if (ret < 0) {
					ksft_print_msg("ERROR: [%s] Poll error %d\n",
						       __func__, errno);
					return TEST_FAILURE;
				}
				if (ret == 0)
					return TEST_PASS;
				break;
			}
			if (ret <= 0) {
				ksft_print_msg("ERROR: [%s] Poll error %d\n",
					       __func__, errno);
				return TEST_FAILURE;
			}
		}

		complete_pkts(xsk, BATCH_SIZE);
	}

	for (i = 0; i < BATCH_SIZE; i++) {
		struct xdp_desc *tx_desc = xsk_ring_prod__tx_desc(&xsk->tx, idx + i);
		struct pkt *pkt = pkt_generate(ifobject, *pkt_nb);

		if (!pkt)
			break;

		tx_desc->addr = pkt->addr;
		tx_desc->len = pkt->len;
		(*pkt_nb)++;
		if (pkt->valid)
			valid_pkts++;
	}

	pthread_mutex_lock(&pacing_mutex);
	pkts_in_flight += valid_pkts;
	/* pkts_in_flight might be negative if many invalid packets are sent */
	if (pkts_in_flight >= (int)(ifobject->umem->num_frames - BATCH_SIZE)) {
		kick_tx(xsk);
		pthread_cond_wait(&pacing_cond, &pacing_mutex);
	}
	pthread_mutex_unlock(&pacing_mutex);

	xsk_ring_prod__submit(&xsk->tx, i);
	xsk->outstanding_tx += valid_pkts;

	if (use_poll) {
		ret = poll(fds, 1, POLL_TMOUT);
		if (ret <= 0) {
			if (ret == 0 && timeout)
				return TEST_PASS;

			ksft_print_msg("ERROR: [%s] Poll error %d\n", __func__, ret);
			return TEST_FAILURE;
		}
	}

	if (!timeout) {
		if (complete_pkts(xsk, i))
			return TEST_FAILURE;

		usleep(10);
		return TEST_PASS;
	}

	return TEST_CONTINUE;
}

static void wait_for_tx_completion(struct xsk_socket_info *xsk)
{
	while (xsk->outstanding_tx)
		complete_pkts(xsk, BATCH_SIZE);
}

static int send_pkts(struct test_spec *test, struct ifobject *ifobject)
{
	bool timeout = !is_umem_valid(test->ifobj_rx);
	struct pollfd fds = { };
	u32 pkt_cnt = 0, ret;

	fds.fd = xsk_socket__fd(ifobject->xsk->xsk);
	fds.events = POLLOUT;

	while (pkt_cnt < ifobject->pkt_stream->nb_pkts) {
		ret = __send_pkts(ifobject, &pkt_cnt, &fds, timeout);
		if ((ret || test->fail) && !timeout)
			return TEST_FAILURE;
		else if (ret == TEST_PASS && timeout)
			return ret;
	}

	wait_for_tx_completion(ifobject->xsk);
	return TEST_PASS;
}

static int get_xsk_stats(struct xsk_socket *xsk, struct xdp_statistics *stats)
{
	int fd = xsk_socket__fd(xsk), err;
	socklen_t optlen, expected_len;

	optlen = sizeof(*stats);
	err = getsockopt(fd, SOL_XDP, XDP_STATISTICS, stats, &optlen);
	if (err) {
		ksft_print_msg("[%s] getsockopt(XDP_STATISTICS) error %u %s\n",
			       __func__, -err, strerror(-err));
		return TEST_FAILURE;
	}

	expected_len = sizeof(struct xdp_statistics);
	if (optlen != expected_len) {
		ksft_print_msg("[%s] getsockopt optlen error. Expected: %u got: %u\n",
			       __func__, expected_len, optlen);
		return TEST_FAILURE;
	}

	return TEST_PASS;
}

static int validate_rx_dropped(struct ifobject *ifobject)
{
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	struct xdp_statistics stats;
	int err;

	kick_rx(ifobject->xsk);

	err = get_xsk_stats(xsk, &stats);
	if (err)
		return TEST_FAILURE;

	/* The receiver calls getsockopt after receiving the last (valid)
	 * packet which is not the final packet sent in this test (valid and
	 * invalid packets are sent in alternating fashion with the final
	 * packet being invalid). Since the last packet may or may not have
	 * been dropped already, both outcomes must be allowed.
	 */
	if (stats.rx_dropped == ifobject->pkt_stream->nb_pkts / 2 ||
	    stats.rx_dropped == ifobject->pkt_stream->nb_pkts / 2 - 1)
		return TEST_PASS;

	return TEST_FAILURE;
}

static int validate_rx_full(struct ifobject *ifobject)
{
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	struct xdp_statistics stats;
	int err;

	usleep(1000);
	kick_rx(ifobject->xsk);

	err = get_xsk_stats(xsk, &stats);
	if (err)
		return TEST_FAILURE;

	if (stats.rx_ring_full)
		return TEST_PASS;

	return TEST_FAILURE;
}

static int validate_fill_empty(struct ifobject *ifobject)
{
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	struct xdp_statistics stats;
	int err;

	usleep(1000);
	kick_rx(ifobject->xsk);

	err = get_xsk_stats(xsk, &stats);
	if (err)
		return TEST_FAILURE;

	if (stats.rx_fill_ring_empty_descs)
		return TEST_PASS;

	return TEST_FAILURE;
}

static int validate_tx_invalid_descs(struct ifobject *ifobject)
{
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	int fd = xsk_socket__fd(xsk);
	struct xdp_statistics stats;
	socklen_t optlen;
	int err;

	optlen = sizeof(stats);
	err = getsockopt(fd, SOL_XDP, XDP_STATISTICS, &stats, &optlen);
	if (err) {
		ksft_print_msg("[%s] getsockopt(XDP_STATISTICS) error %u %s\n",
			       __func__, -err, strerror(-err));
		return TEST_FAILURE;
	}

	if (stats.tx_invalid_descs != ifobject->pkt_stream->nb_pkts / 2) {
		ksft_print_msg("[%s] tx_invalid_descs incorrect. Got [%u] expected [%u]\n",
			       __func__, stats.tx_invalid_descs, ifobject->pkt_stream->nb_pkts);
		return TEST_FAILURE;
	}

	return TEST_PASS;
}

static void xsk_configure_socket(struct test_spec *test, struct ifobject *ifobject,
				 struct xsk_umem_info *umem, bool tx)
{
	int i, ret;

	for (i = 0; i < test->nb_sockets; i++) {
		bool shared = (ifobject->shared_umem && tx) ? true : !!i;
		u32 ctr = 0;

		while (ctr++ < SOCK_RECONF_CTR) {
			ret = __xsk_configure_socket(&ifobject->xsk_arr[i], umem,
						     ifobject, shared);
			if (!ret)
				break;

			/* Retry if it fails as xsk_socket__create() is asynchronous */
			if (ctr >= SOCK_RECONF_CTR)
				exit_with_error(-ret);
			usleep(USLEEP_MAX);
		}
		if (ifobject->busy_poll)
			enable_busy_poll(&ifobject->xsk_arr[i]);
	}
}

static void thread_common_ops_tx(struct test_spec *test, struct ifobject *ifobject)
{
	xsk_configure_socket(test, ifobject, test->ifobj_rx->umem, true);
	ifobject->xsk = &ifobject->xsk_arr[0];
	ifobject->xskmap = test->ifobj_rx->xskmap;
	memcpy(ifobject->umem, test->ifobj_rx->umem, sizeof(struct xsk_umem_info));
}

static void xsk_populate_fill_ring(struct xsk_umem_info *umem, struct pkt_stream *pkt_stream)
{
	u32 idx = 0, i, buffers_to_fill;
	int ret;

	if (umem->num_frames < XSK_RING_PROD__DEFAULT_NUM_DESCS)
		buffers_to_fill = umem->num_frames;
	else
		buffers_to_fill = XSK_RING_PROD__DEFAULT_NUM_DESCS;

	ret = xsk_ring_prod__reserve(&umem->fq, buffers_to_fill, &idx);
	if (ret != buffers_to_fill)
		exit_with_error(ENOSPC);
	for (i = 0; i < buffers_to_fill; i++) {
		u64 addr;

		if (pkt_stream->use_addr_for_fill) {
			struct pkt *pkt = pkt_stream_get_pkt(pkt_stream, i);

			if (!pkt)
				break;
			addr = pkt->addr;
		} else {
			addr = i * umem->frame_size;
		}

		*xsk_ring_prod__fill_addr(&umem->fq, idx++) = addr;
	}
	xsk_ring_prod__submit(&umem->fq, i);
}

static void thread_common_ops(struct test_spec *test, struct ifobject *ifobject)
{
	u64 umem_sz = ifobject->umem->num_frames * ifobject->umem->frame_size;
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
	LIBBPF_OPTS(bpf_xdp_query_opts, opts);
	void *bufs;
	int ret;

	if (ifobject->umem->unaligned_mode)
		mmap_flags |= MAP_HUGETLB;

	if (ifobject->shared_umem)
		umem_sz *= 2;

	bufs = mmap(NULL, umem_sz, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
	if (bufs == MAP_FAILED)
		exit_with_error(errno);

	ret = xsk_configure_umem(ifobject->umem, bufs, umem_sz);
	if (ret)
		exit_with_error(-ret);

	xsk_populate_fill_ring(ifobject->umem, ifobject->pkt_stream);

	xsk_configure_socket(test, ifobject, ifobject->umem, false);

	ifobject->xsk = &ifobject->xsk_arr[0];

	if (!ifobject->rx_on)
		return;

	ret = xsk_update_xskmap(ifobject->xskmap, ifobject->xsk->xsk);
	if (ret)
		exit_with_error(errno);
}

static void *worker_testapp_validate_tx(void *arg)
{
	struct test_spec *test = (struct test_spec *)arg;
	struct ifobject *ifobject = test->ifobj_tx;
	int err;

	if (test->current_step == 1) {
		if (!ifobject->shared_umem)
			thread_common_ops(test, ifobject);
		else
			thread_common_ops_tx(test, ifobject);
	}

	print_verbose("Sending %d packets on interface %s\n", ifobject->pkt_stream->nb_pkts,
		      ifobject->ifname);
	err = send_pkts(test, ifobject);

	if (!err && ifobject->validation_func)
		err = ifobject->validation_func(ifobject);
	if (err)
		report_failure(test);

	pthread_exit(NULL);
}

static void *worker_testapp_validate_rx(void *arg)
{
	struct test_spec *test = (struct test_spec *)arg;
	struct ifobject *ifobject = test->ifobj_rx;
	struct pollfd fds = { };
	int err;

	if (test->current_step == 1) {
		thread_common_ops(test, ifobject);
	} else {
		xsk_clear_xskmap(ifobject->xskmap);
		err = xsk_update_xskmap(ifobject->xskmap, ifobject->xsk->xsk);
		if (err) {
			printf("Error: Failed to update xskmap, error %s\n", strerror(-err));
			exit_with_error(-err);
		}
	}

	fds.fd = xsk_socket__fd(ifobject->xsk->xsk);
	fds.events = POLLIN;

	pthread_barrier_wait(&barr);

	err = receive_pkts(test, &fds);

	if (!err && ifobject->validation_func)
		err = ifobject->validation_func(ifobject);
	if (err) {
		report_failure(test);
		pthread_mutex_lock(&pacing_mutex);
		pthread_cond_signal(&pacing_cond);
		pthread_mutex_unlock(&pacing_mutex);
	}

	pthread_exit(NULL);
}

static void testapp_clean_xsk_umem(struct ifobject *ifobj)
{
	u64 umem_sz = ifobj->umem->num_frames * ifobj->umem->frame_size;

	if (ifobj->shared_umem)
		umem_sz *= 2;

	xsk_umem__delete(ifobj->umem->umem);
	munmap(ifobj->umem->buffer, umem_sz);
}

static void handler(int signum)
{
	pthread_exit(NULL);
}

static bool xdp_prog_changed(struct test_spec *test, struct ifobject *ifobj)
{
	return ifobj->xdp_prog != test->xdp_prog_rx || ifobj->mode != test->mode;
}

static void xsk_reattach_xdp(struct ifobject *ifobj, struct bpf_program *xdp_prog,
			     struct bpf_map *xskmap, enum test_mode mode)
{
	int err;

	xsk_detach_xdp_program(ifobj->ifindex, mode_to_xdp_flags(ifobj->mode));
	err = xsk_attach_xdp_program(xdp_prog, ifobj->ifindex, mode_to_xdp_flags(mode));
	if (err) {
		printf("Error attaching XDP program\n");
		exit_with_error(-err);
	}

	if (ifobj->mode != mode && (mode == TEST_MODE_DRV || mode == TEST_MODE_ZC))
		if (!xsk_is_in_mode(ifobj->ifindex, XDP_FLAGS_DRV_MODE)) {
			ksft_print_msg("ERROR: XDP prog not in DRV mode\n");
			exit_with_error(EINVAL);
		}

	ifobj->xdp_prog = xdp_prog;
	ifobj->xskmap = xskmap;
	ifobj->mode = mode;
}

static void xsk_attach_xdp_progs(struct test_spec *test, struct ifobject *ifobj_rx,
				 struct ifobject *ifobj_tx)
{
	if (xdp_prog_changed(test, ifobj_rx))
		xsk_reattach_xdp(ifobj_rx, test->xdp_prog_rx, test->xskmap_rx, test->mode);

	if (!ifobj_tx || ifobj_tx->shared_umem)
		return;

	if (xdp_prog_changed(test, ifobj_tx))
		xsk_reattach_xdp(ifobj_tx, test->xdp_prog_tx, test->xskmap_tx, test->mode);
}

static int __testapp_validate_traffic(struct test_spec *test, struct ifobject *ifobj1,
				      struct ifobject *ifobj2)
{
	pthread_t t0, t1;

	if (ifobj2)
		if (pthread_barrier_init(&barr, NULL, 2))
			exit_with_error(errno);

	test->current_step++;
	pkt_stream_reset(ifobj1->pkt_stream);
	pkts_in_flight = 0;

	signal(SIGUSR1, handler);
	/*Spawn RX thread */
	pthread_create(&t0, NULL, ifobj1->func_ptr, test);

	if (ifobj2) {
		pthread_barrier_wait(&barr);
		if (pthread_barrier_destroy(&barr))
			exit_with_error(errno);

		/*Spawn TX thread */
		pthread_create(&t1, NULL, ifobj2->func_ptr, test);

		pthread_join(t1, NULL);
	}

	if (!ifobj2)
		pthread_kill(t0, SIGUSR1);
	else
		pthread_join(t0, NULL);

	if (test->total_steps == test->current_step || test->fail) {
		if (ifobj2)
			xsk_socket__delete(ifobj2->xsk->xsk);
		xsk_socket__delete(ifobj1->xsk->xsk);
		testapp_clean_xsk_umem(ifobj1);
		if (ifobj2 && !ifobj2->shared_umem)
			testapp_clean_xsk_umem(ifobj2);
	}

	return !!test->fail;
}

static int testapp_validate_traffic(struct test_spec *test)
{
	struct ifobject *ifobj_rx = test->ifobj_rx;
	struct ifobject *ifobj_tx = test->ifobj_tx;

	xsk_attach_xdp_progs(test, ifobj_rx, ifobj_tx);
	return __testapp_validate_traffic(test, ifobj_rx, ifobj_tx);
}

static int testapp_validate_traffic_single_thread(struct test_spec *test, struct ifobject *ifobj)
{
	return __testapp_validate_traffic(test, ifobj, NULL);
}

static void testapp_teardown(struct test_spec *test)
{
	int i;

	test_spec_set_name(test, "TEARDOWN");
	for (i = 0; i < MAX_TEARDOWN_ITER; i++) {
		if (testapp_validate_traffic(test))
			return;
		test_spec_reset(test);
	}
}

static void swap_directions(struct ifobject **ifobj1, struct ifobject **ifobj2)
{
	thread_func_t tmp_func_ptr = (*ifobj1)->func_ptr;
	struct ifobject *tmp_ifobj = (*ifobj1);

	(*ifobj1)->func_ptr = (*ifobj2)->func_ptr;
	(*ifobj2)->func_ptr = tmp_func_ptr;

	*ifobj1 = *ifobj2;
	*ifobj2 = tmp_ifobj;
}

static void testapp_bidi(struct test_spec *test)
{
	test_spec_set_name(test, "BIDIRECTIONAL");
	test->ifobj_tx->rx_on = true;
	test->ifobj_rx->tx_on = true;
	test->total_steps = 2;
	if (testapp_validate_traffic(test))
		return;

	print_verbose("Switching Tx/Rx vectors\n");
	swap_directions(&test->ifobj_rx, &test->ifobj_tx);
	__testapp_validate_traffic(test, test->ifobj_rx, test->ifobj_tx);

	swap_directions(&test->ifobj_rx, &test->ifobj_tx);
}

static void swap_xsk_resources(struct ifobject *ifobj_tx, struct ifobject *ifobj_rx)
{
	int ret;

	xsk_socket__delete(ifobj_tx->xsk->xsk);
	xsk_socket__delete(ifobj_rx->xsk->xsk);
	ifobj_tx->xsk = &ifobj_tx->xsk_arr[1];
	ifobj_rx->xsk = &ifobj_rx->xsk_arr[1];

	ret = xsk_update_xskmap(ifobj_rx->xskmap, ifobj_rx->xsk->xsk);
	if (ret)
		exit_with_error(errno);
}

static void testapp_bpf_res(struct test_spec *test)
{
	test_spec_set_name(test, "BPF_RES");
	test->total_steps = 2;
	test->nb_sockets = 2;
	if (testapp_validate_traffic(test))
		return;

	swap_xsk_resources(test->ifobj_tx, test->ifobj_rx);
	testapp_validate_traffic(test);
}

static void testapp_headroom(struct test_spec *test)
{
	test_spec_set_name(test, "UMEM_HEADROOM");
	test->ifobj_rx->umem->frame_headroom = UMEM_HEADROOM_TEST_SIZE;
	testapp_validate_traffic(test);
}

static void testapp_stats_rx_dropped(struct test_spec *test)
{
	test_spec_set_name(test, "STAT_RX_DROPPED");
	pkt_stream_replace_half(test, MIN_PKT_SIZE * 4, 0);
	test->ifobj_rx->umem->frame_headroom = test->ifobj_rx->umem->frame_size -
		XDP_PACKET_HEADROOM - MIN_PKT_SIZE * 3;
	pkt_stream_receive_half(test);
	test->ifobj_rx->validation_func = validate_rx_dropped;
	testapp_validate_traffic(test);
}

static void testapp_stats_tx_invalid_descs(struct test_spec *test)
{
	test_spec_set_name(test, "STAT_TX_INVALID");
	pkt_stream_replace_half(test, XSK_UMEM__INVALID_FRAME_SIZE, 0);
	test->ifobj_tx->validation_func = validate_tx_invalid_descs;
	testapp_validate_traffic(test);
}

static void testapp_stats_rx_full(struct test_spec *test)
{
	test_spec_set_name(test, "STAT_RX_FULL");
	pkt_stream_replace(test, DEFAULT_UMEM_BUFFERS + DEFAULT_UMEM_BUFFERS / 2, PKT_SIZE);
	test->ifobj_rx->pkt_stream = pkt_stream_generate(test->ifobj_rx->umem,
							 DEFAULT_UMEM_BUFFERS, PKT_SIZE);
	if (!test->ifobj_rx->pkt_stream)
		exit_with_error(ENOMEM);

	test->ifobj_rx->xsk->rxqsize = DEFAULT_UMEM_BUFFERS;
	test->ifobj_rx->release_rx = false;
	test->ifobj_rx->validation_func = validate_rx_full;
	testapp_validate_traffic(test);
}

static void testapp_stats_fill_empty(struct test_spec *test)
{
	test_spec_set_name(test, "STAT_RX_FILL_EMPTY");
	pkt_stream_replace(test, DEFAULT_UMEM_BUFFERS + DEFAULT_UMEM_BUFFERS / 2, PKT_SIZE);
	test->ifobj_rx->pkt_stream = pkt_stream_generate(test->ifobj_rx->umem,
							 DEFAULT_UMEM_BUFFERS, PKT_SIZE);
	if (!test->ifobj_rx->pkt_stream)
		exit_with_error(ENOMEM);

	test->ifobj_rx->use_fill_ring = false;
	test->ifobj_rx->validation_func = validate_fill_empty;
	testapp_validate_traffic(test);
}

/* Simple test */
static bool hugepages_present(struct ifobject *ifobject)
{
	const size_t mmap_sz = 2 * ifobject->umem->num_frames * ifobject->umem->frame_size;
	void *bufs;

	bufs = mmap(NULL, mmap_sz, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (bufs == MAP_FAILED)
		return false;

	munmap(bufs, mmap_sz);
	return true;
}

static bool testapp_unaligned(struct test_spec *test)
{
	if (!hugepages_present(test->ifobj_tx)) {
		ksft_test_result_skip("No 2M huge pages present.\n");
		return false;
	}

	test_spec_set_name(test, "UNALIGNED_MODE");
	test->ifobj_tx->umem->unaligned_mode = true;
	test->ifobj_rx->umem->unaligned_mode = true;
	/* Let half of the packets straddle a buffer boundrary */
	pkt_stream_replace_half(test, PKT_SIZE, -PKT_SIZE / 2);
	test->ifobj_rx->pkt_stream->use_addr_for_fill = true;
	testapp_validate_traffic(test);

	return true;
}

static void testapp_single_pkt(struct test_spec *test)
{
	struct pkt pkts[] = {{0x1000, PKT_SIZE, 0, true}};

	pkt_stream_generate_custom(test, pkts, ARRAY_SIZE(pkts));
	testapp_validate_traffic(test);
}

static void testapp_invalid_desc(struct test_spec *test)
{
	u64 umem_size = test->ifobj_tx->umem->num_frames * test->ifobj_tx->umem->frame_size;
	struct pkt pkts[] = {
		/* Zero packet address allowed */
		{0, PKT_SIZE, 0, true},
		/* Allowed packet */
		{0x1000, PKT_SIZE, 0, true},
		/* Straddling the start of umem */
		{-2, PKT_SIZE, 0, false},
		/* Packet too large */
		{0x2000, XSK_UMEM__INVALID_FRAME_SIZE, 0, false},
		/* After umem ends */
		{umem_size, PKT_SIZE, 0, false},
		/* Straddle the end of umem */
		{umem_size - PKT_SIZE / 2, PKT_SIZE, 0, false},
		/* Straddle a page boundrary */
		{0x3000 - PKT_SIZE / 2, PKT_SIZE, 0, false},
		/* Straddle a 2K boundrary */
		{0x3800 - PKT_SIZE / 2, PKT_SIZE, 0, true},
		/* Valid packet for synch so that something is received */
		{0x4000, PKT_SIZE, 0, true}};

	if (test->ifobj_tx->umem->unaligned_mode) {
		/* Crossing a page boundrary allowed */
		pkts[6].valid = true;
	}
	if (test->ifobj_tx->umem->frame_size == XSK_UMEM__DEFAULT_FRAME_SIZE / 2) {
		/* Crossing a 2K frame size boundrary not allowed */
		pkts[7].valid = false;
	}

	if (test->ifobj_tx->shared_umem) {
		pkts[4].addr += umem_size;
		pkts[5].addr += umem_size;
	}

	pkt_stream_generate_custom(test, pkts, ARRAY_SIZE(pkts));
	testapp_validate_traffic(test);
}

static void testapp_xdp_drop(struct test_spec *test)
{
	struct xsk_xdp_progs *skel_rx = test->ifobj_rx->xdp_progs;
	struct xsk_xdp_progs *skel_tx = test->ifobj_tx->xdp_progs;

	test_spec_set_name(test, "XDP_DROP_HALF");
	test_spec_set_xdp_prog(test, skel_rx->progs.xsk_xdp_drop, skel_tx->progs.xsk_xdp_drop,
			       skel_rx->maps.xsk, skel_tx->maps.xsk);

	pkt_stream_receive_half(test);
	testapp_validate_traffic(test);
}

static void testapp_poll_txq_tmout(struct test_spec *test)
{
	test_spec_set_name(test, "POLL_TXQ_FULL");

	test->ifobj_tx->use_poll = true;
	/* create invalid frame by set umem frame_size and pkt length equal to 2048 */
	test->ifobj_tx->umem->frame_size = 2048;
	pkt_stream_replace(test, 2 * DEFAULT_PKT_CNT, 2048);
	testapp_validate_traffic_single_thread(test, test->ifobj_tx);
}

static void testapp_poll_rxq_tmout(struct test_spec *test)
{
	test_spec_set_name(test, "POLL_RXQ_EMPTY");
	test->ifobj_rx->use_poll = true;
	testapp_validate_traffic_single_thread(test, test->ifobj_rx);
}

static int xsk_load_xdp_programs(struct ifobject *ifobj)
{
	ifobj->xdp_progs = xsk_xdp_progs__open_and_load();
	if (libbpf_get_error(ifobj->xdp_progs))
		return libbpf_get_error(ifobj->xdp_progs);

	return 0;
}

static void xsk_unload_xdp_programs(struct ifobject *ifobj)
{
	xsk_xdp_progs__destroy(ifobj->xdp_progs);
}

static void init_iface(struct ifobject *ifobj, const char *dst_mac, const char *src_mac,
		       const char *dst_ip, const char *src_ip, const u16 dst_port,
		       const u16 src_port, thread_func_t func_ptr)
{
	struct in_addr ip;
	int err;

	memcpy(ifobj->dst_mac, dst_mac, ETH_ALEN);
	memcpy(ifobj->src_mac, src_mac, ETH_ALEN);

	inet_aton(dst_ip, &ip);
	ifobj->dst_ip = ip.s_addr;

	inet_aton(src_ip, &ip);
	ifobj->src_ip = ip.s_addr;

	ifobj->dst_port = dst_port;
	ifobj->src_port = src_port;

	ifobj->func_ptr = func_ptr;

	err = xsk_load_xdp_programs(ifobj);
	if (err) {
		printf("Error loading XDP program\n");
		exit_with_error(err);
	}
}

static void run_pkt_test(struct test_spec *test, enum test_mode mode, enum test_type type)
{
	switch (type) {
	case TEST_TYPE_STATS_RX_DROPPED:
		if (mode == TEST_MODE_ZC) {
			ksft_test_result_skip("Can not run RX_DROPPED test for ZC mode\n");
			return;
		}
		testapp_stats_rx_dropped(test);
		break;
	case TEST_TYPE_STATS_TX_INVALID_DESCS:
		testapp_stats_tx_invalid_descs(test);
		break;
	case TEST_TYPE_STATS_RX_FULL:
		testapp_stats_rx_full(test);
		break;
	case TEST_TYPE_STATS_FILL_EMPTY:
		testapp_stats_fill_empty(test);
		break;
	case TEST_TYPE_TEARDOWN:
		testapp_teardown(test);
		break;
	case TEST_TYPE_BIDI:
		testapp_bidi(test);
		break;
	case TEST_TYPE_BPF_RES:
		testapp_bpf_res(test);
		break;
	case TEST_TYPE_RUN_TO_COMPLETION:
		test_spec_set_name(test, "RUN_TO_COMPLETION");
		testapp_validate_traffic(test);
		break;
	case TEST_TYPE_RUN_TO_COMPLETION_SINGLE_PKT:
		test_spec_set_name(test, "RUN_TO_COMPLETION_SINGLE_PKT");
		testapp_single_pkt(test);
		break;
	case TEST_TYPE_RUN_TO_COMPLETION_2K_FRAME:
		test_spec_set_name(test, "RUN_TO_COMPLETION_2K_FRAME_SIZE");
		test->ifobj_tx->umem->frame_size = 2048;
		test->ifobj_rx->umem->frame_size = 2048;
		pkt_stream_replace(test, DEFAULT_PKT_CNT, PKT_SIZE);
		testapp_validate_traffic(test);
		break;
	case TEST_TYPE_RX_POLL:
		test->ifobj_rx->use_poll = true;
		test_spec_set_name(test, "POLL_RX");
		testapp_validate_traffic(test);
		break;
	case TEST_TYPE_TX_POLL:
		test->ifobj_tx->use_poll = true;
		test_spec_set_name(test, "POLL_TX");
		testapp_validate_traffic(test);
		break;
	case TEST_TYPE_POLL_TXQ_TMOUT:
		testapp_poll_txq_tmout(test);
		break;
	case TEST_TYPE_POLL_RXQ_TMOUT:
		testapp_poll_rxq_tmout(test);
		break;
	case TEST_TYPE_ALIGNED_INV_DESC:
		test_spec_set_name(test, "ALIGNED_INV_DESC");
		testapp_invalid_desc(test);
		break;
	case TEST_TYPE_ALIGNED_INV_DESC_2K_FRAME:
		test_spec_set_name(test, "ALIGNED_INV_DESC_2K_FRAME_SIZE");
		test->ifobj_tx->umem->frame_size = 2048;
		test->ifobj_rx->umem->frame_size = 2048;
		testapp_invalid_desc(test);
		break;
	case TEST_TYPE_UNALIGNED_INV_DESC:
		if (!hugepages_present(test->ifobj_tx)) {
			ksft_test_result_skip("No 2M huge pages present.\n");
			return;
		}
		test_spec_set_name(test, "UNALIGNED_INV_DESC");
		test->ifobj_tx->umem->unaligned_mode = true;
		test->ifobj_rx->umem->unaligned_mode = true;
		testapp_invalid_desc(test);
		break;
	case TEST_TYPE_UNALIGNED:
		if (!testapp_unaligned(test))
			return;
		break;
	case TEST_TYPE_HEADROOM:
		testapp_headroom(test);
		break;
	case TEST_TYPE_XDP_DROP_HALF:
		testapp_xdp_drop(test);
		break;
	default:
		break;
	}

	if (!test->fail)
		ksft_test_result_pass("PASS: %s %s%s\n", mode_string(test), busy_poll_string(test),
				      test->name);
	pkt_stream_restore_default(test);
}

static struct ifobject *ifobject_create(void)
{
	struct ifobject *ifobj;

	ifobj = calloc(1, sizeof(struct ifobject));
	if (!ifobj)
		return NULL;

	ifobj->xsk_arr = calloc(MAX_SOCKETS, sizeof(*ifobj->xsk_arr));
	if (!ifobj->xsk_arr)
		goto out_xsk_arr;

	ifobj->umem = calloc(1, sizeof(*ifobj->umem));
	if (!ifobj->umem)
		goto out_umem;

	return ifobj;

out_umem:
	free(ifobj->xsk_arr);
out_xsk_arr:
	free(ifobj);
	return NULL;
}

static void ifobject_delete(struct ifobject *ifobj)
{
	free(ifobj->umem);
	free(ifobj->xsk_arr);
	free(ifobj);
}

static bool is_xdp_supported(int ifindex)
{
	int flags = XDP_FLAGS_DRV_MODE;

	LIBBPF_OPTS(bpf_link_create_opts, opts, .flags = flags);
	struct bpf_insn insns[2] = {
		BPF_MOV64_IMM(BPF_REG_0, XDP_PASS),
		BPF_EXIT_INSN()
	};
	int prog_fd, insn_cnt = ARRAY_SIZE(insns);
	int err;

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_XDP, NULL, "GPL", insns, insn_cnt, NULL);
	if (prog_fd < 0)
		return false;

	err = bpf_xdp_attach(ifindex, prog_fd, flags, NULL);
	if (err) {
		close(prog_fd);
		return false;
	}

	bpf_xdp_detach(ifindex, flags, NULL);
	close(prog_fd);

	return true;
}

int main(int argc, char **argv)
{
	struct pkt_stream *rx_pkt_stream_default;
	struct pkt_stream *tx_pkt_stream_default;
	struct ifobject *ifobj_tx, *ifobj_rx;
	int modes = TEST_MODE_SKB + 1;
	u32 i, j, failed_tests = 0;
	struct test_spec test;
	bool shared_netdev;

	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	ifobj_tx = ifobject_create();
	if (!ifobj_tx)
		exit_with_error(ENOMEM);
	ifobj_rx = ifobject_create();
	if (!ifobj_rx)
		exit_with_error(ENOMEM);

	setlocale(LC_ALL, "");

	parse_command_line(ifobj_tx, ifobj_rx, argc, argv);

	shared_netdev = (ifobj_tx->ifindex == ifobj_rx->ifindex);
	ifobj_tx->shared_umem = shared_netdev;
	ifobj_rx->shared_umem = shared_netdev;

	if (!validate_interface(ifobj_tx) || !validate_interface(ifobj_rx)) {
		usage(basename(argv[0]));
		ksft_exit_xfail();
	}

	if (is_xdp_supported(ifobj_tx->ifindex)) {
		modes++;
		if (ifobj_zc_avail(ifobj_tx))
			modes++;
	}

	init_iface(ifobj_rx, MAC1, MAC2, IP1, IP2, UDP_PORT1, UDP_PORT2,
		   worker_testapp_validate_rx);
	init_iface(ifobj_tx, MAC2, MAC1, IP2, IP1, UDP_PORT2, UDP_PORT1,
		   worker_testapp_validate_tx);

	test_spec_init(&test, ifobj_tx, ifobj_rx, 0);
	tx_pkt_stream_default = pkt_stream_generate(ifobj_tx->umem, DEFAULT_PKT_CNT, PKT_SIZE);
	rx_pkt_stream_default = pkt_stream_generate(ifobj_rx->umem, DEFAULT_PKT_CNT, PKT_SIZE);
	if (!tx_pkt_stream_default || !rx_pkt_stream_default)
		exit_with_error(ENOMEM);
	test.tx_pkt_stream_default = tx_pkt_stream_default;
	test.rx_pkt_stream_default = rx_pkt_stream_default;

	ksft_set_plan(modes * TEST_TYPE_MAX);

	for (i = 0; i < modes; i++) {
		for (j = 0; j < TEST_TYPE_MAX; j++) {
			test_spec_init(&test, ifobj_tx, ifobj_rx, i);
			run_pkt_test(&test, i, j);
			usleep(USLEEP_MAX);

			if (test.fail)
				failed_tests++;
		}
	}

	pkt_stream_delete(tx_pkt_stream_default);
	pkt_stream_delete(rx_pkt_stream_default);
	xsk_unload_xdp_programs(ifobj_tx);
	xsk_unload_xdp_programs(ifobj_rx);
	ifobject_delete(ifobj_tx);
	ifobject_delete(ifobj_rx);

	if (failed_tests)
		ksft_exit_fail();
	else
		ksft_exit_pass();
}
