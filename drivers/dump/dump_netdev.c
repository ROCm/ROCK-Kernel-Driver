/*
 * Implements the dump driver interface for saving a dump via network
 * interface. 
 *
 * Some of this code has been taken/adapted from Ingo Molnar's netconsole
 * code. LKCD team expresses its thanks to Ingo.
 *
 * Started: June 2002 - Mohamed Abbas <mohamed.abbas@intel.com>
 * 	Adapted netconsole code to implement LKCD dump over the network.
 *
 * Nov 2002 - Bharata B. Rao <bharata@in.ibm.com>
 * 	Innumerable code cleanups, simplification and some fixes.
 *	Netdump configuration done by ioctl instead of using module parameters.
 * Oct 2003 - Prasanna S Panchamukhi <prasanna@in.ibm.com>
 *	Netdump code modified to use Netpoll API's.
 *
 * Copyright (C) 2001  Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2002 International Business Machines Corp. 
 *
 *  This code is released under version 2 of the GNU GPL.
 */

#include <net/tcp.h>
#include <net/udp.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/module.h>
#include <linux/dump.h>
#include <linux/dump_netdev.h>

#include <asm/unaligned.h>

static int startup_handshake;
static int page_counter;
static unsigned long flags_global;
static int netdump_in_progress;

/*
 * security depends on the trusted path between the netconsole
 * server and netconsole client, since none of the packets are
 * encrypted. The random magic number protects the protocol
 * against spoofing.
 */
static u64 dump_magic;

/*
 * We maintain a small pool of fully-sized skbs,
 * to make sure the message gets out even in
 * extreme OOM situations.
 */

static void rx_hook(struct netpoll *np, int port, char *msg, int size);
int new_req = 0;
static req_t req;

static void rx_hook(struct netpoll *np, int port, char *msg, int size)
{
	req_t * __req = (req_t *) msg;
	/* 
	 * First check if were are dumping or doing startup handshake, if
	 * not quickly return.
	 */

	if (!netdump_in_progress)
		return ;

	if ((ntohl(__req->command) != COMM_GET_MAGIC) &&
	    (ntohl(__req->command) != COMM_HELLO) &&
	    (ntohl(__req->command) != COMM_START_WRITE_NETDUMP_ACK) &&
	    (ntohl(__req->command) != COMM_START_NETDUMP_ACK) &&
	    (memcmp(&__req->magic, &dump_magic, sizeof(dump_magic)) != 0))
		goto out;

	req.magic = ntohl(__req->magic);
	req.command = ntohl(__req->command);
	req.from = ntohl(__req->from);
	req.to = ntohl(__req->to);
	req.nr = ntohl(__req->nr);
	new_req = 1;
out:
	return ;
}
static char netdump_membuf[1024 + HEADER_LEN + 1];
/*
 * Fill the netdump_membuf with the header information from reply_t structure 
 * and send it down to netpoll_send_udp() routine.
 */
static void 
netdump_send_packet(struct netpoll *np, reply_t *reply, size_t data_len) {
	char *b;

	b = &netdump_membuf[1];
	netdump_membuf[0] = NETCONSOLE_VERSION;
	put_unaligned(htonl(reply->nr), (u32 *) b);
	put_unaligned(htonl(reply->code), (u32 *) (b + sizeof(reply->code)));
	put_unaligned(htonl(reply->info), (u32 *) (b + sizeof(reply->code) + 
		sizeof(reply->info)));
	netpoll_send_udp(np, netdump_membuf, data_len + HEADER_LEN);
}

static void
dump_send_mem(struct netpoll *np, req_t *req, const char* buff, size_t len)
{
	int i;

	int nr_chunks = len/1024;
	reply_t reply;

	reply.nr = req->nr;
	reply.code = REPLY_MEM;
        if ( nr_chunks <= 0)
		 nr_chunks = 1;
	for (i = 0; i < nr_chunks; i++) {
		uint64_t offset = ((uint64_t) i) * 1024;

		/* We put the high 24 bits of the offset into the top
		   of reply.code. */

		reply.info = offset;
		reply.code |= (offset >> 24) & 0xffffff00;
		memcpy((netdump_membuf + HEADER_LEN), (buff + offset), 1024);
		netdump_send_packet(np, &reply, 1024);
	}
}

/*
 * This function waits for the client to acknowledge the receipt
 * of the netdump startup reply, with the possibility of packets
 * getting lost. We resend the startup packet if no ACK is received,
 * after a 1 second delay.
 *
 * (The client can test the success of the handshake via the HELLO
 * command, and send ACKs until we enter netdump mode.)
 */
static int
dump_handshake(struct dump_dev *net_dev)
{
	reply_t reply;
	int i, j;
	size_t str_len;

	if (startup_handshake) {
		sprintf((netdump_membuf + HEADER_LEN), 
			"NETDUMP start, waiting for start-ACK.\n");
		reply.code = REPLY_START_NETDUMP;
		reply.nr = 0;
		reply.info = 0;
	} else {
		sprintf((netdump_membuf + HEADER_LEN), 
			"NETDUMP start, waiting for start-ACK.\n");
		reply.code = REPLY_START_WRITE_NETDUMP;
		reply.nr = net_dev->curr_offset;
		reply.info = net_dev->curr_offset;
	}
	str_len = strlen(netdump_membuf + HEADER_LEN);
	
	/* send 300 handshake packets before declaring failure */
	for (i = 0; i < 300; i++) {
		netdump_send_packet(&net_dev->np, &reply, str_len);

		/* wait 1 sec */
		for (j = 0; j < 10000; j++) {
			udelay(100);
			netpoll_poll(&net_dev->np);
			if (new_req)
				break;
		}

		/* 
		 * if there is no new request, try sending the handshaking
		 * packet again
		 */
		if (!new_req)
			continue;

		/* 
		 * check if the new request is of the expected type,
		 * if so, return, else try sending the handshaking
		 * packet again
		 */
		if (startup_handshake) {
			if (req.command == COMM_HELLO || req.command ==
				COMM_START_NETDUMP_ACK) {
				return 0;
			} else {
				new_req = 0;
				continue;
			}
		} else {
			if (req.command == COMM_SEND_MEM) {
				return 0;
			} else {
				new_req = 0;
				continue;
			}
		}
	}
	return -1;
}

static ssize_t
do_netdump(struct dump_dev *net_dev, const char* buff, size_t len)
{
	reply_t reply;
	ssize_t  ret = 0;
	int repeatCounter, counter, total_loop;
	size_t str_len;
	
	netdump_in_progress = 1;

	if (dump_handshake(net_dev) < 0) {
		printk("LKCD: network dump failed due to handshake failure\n");
		goto out;
	}

	/*
	 * Ideally startup handshake should be done during dump configuration,
	 * i.e., in dump_net_open(). This will be done when I figure out
	 * the dependency between startup handshake, subsequent write and
	 * various commands wrt to net-server.
	 */
	if (startup_handshake)
		startup_handshake = 0;

        counter = 0;
	repeatCounter = 0;
	total_loop = 0;
	while (1) {
                if (!new_req) {
			netpoll_poll(&net_dev->np);
		}
		if (!new_req) {
			repeatCounter++;

			if (repeatCounter > 5) {
				counter++;
				if (counter > 10000) {
					if (total_loop >= 100000) {
						printk("LKCD: Time OUT LEAVE NOW\n");
						goto out;
					} else {
						total_loop++;
						printk("LKCD: Try number %d out of "
							"10 before Time Out\n",
							total_loop);
					}
				}
				mdelay(1);
				repeatCounter = 0;
			}	
			continue;
		}
		repeatCounter = 0;
		counter = 0;
		total_loop = 0;
		new_req = 0;
		switch (req.command) {
		case COMM_NONE:
			break;

		case COMM_SEND_MEM:
			dump_send_mem(&net_dev->np, &req, buff, len);
			break;

		case COMM_EXIT:
                case COMM_START_WRITE_NETDUMP_ACK:
			ret = len;
			goto out;

		case COMM_HELLO:
			sprintf((netdump_membuf + HEADER_LEN), 
				"Hello, this is netdump version " "0.%02d\n",
				 NETCONSOLE_VERSION);
			str_len = strlen(netdump_membuf + HEADER_LEN);
			reply.code = REPLY_HELLO;
			reply.nr = req.nr;
                        reply.info = net_dev->curr_offset;
			netdump_send_packet(&net_dev->np, &reply, str_len);
			break;

		case COMM_GET_PAGE_SIZE:
			sprintf((netdump_membuf + HEADER_LEN), 
				"PAGE_SIZE: %ld\n", PAGE_SIZE);
			str_len = strlen(netdump_membuf + HEADER_LEN);
			reply.code = REPLY_PAGE_SIZE;
			reply.nr = req.nr;
			reply.info = PAGE_SIZE;
			netdump_send_packet(&net_dev->np, &reply, str_len);
			break;

		case COMM_GET_NR_PAGES:
			reply.code = REPLY_NR_PAGES;
			reply.nr = req.nr;
			reply.info = num_physpages;
			reply.info = page_counter;
			sprintf((netdump_membuf + HEADER_LEN), 
				"Number of pages: %ld\n", num_physpages);
			str_len = strlen(netdump_membuf + HEADER_LEN);
			netdump_send_packet(&net_dev->np, &reply, str_len);
			break;

		case COMM_GET_MAGIC:
			reply.code = REPLY_MAGIC;
			reply.nr = req.nr;
			reply.info = NETCONSOLE_VERSION;
			sprintf((netdump_membuf + HEADER_LEN), 
				(char *)&dump_magic, sizeof(dump_magic));
			str_len = strlen(netdump_membuf + HEADER_LEN);
			netdump_send_packet(&net_dev->np, &reply, str_len);
			break;

		default:
			reply.code = REPLY_ERROR;
			reply.nr = req.nr;
			reply.info = req.command;
			sprintf((netdump_membuf + HEADER_LEN), 
				"Got unknown command code %d!\n", req.command);
			str_len = strlen(netdump_membuf + HEADER_LEN);
			netdump_send_packet(&net_dev->np, &reply, str_len);
			break;
		}
	}
out:
	netdump_in_progress = 0;
	return ret;
}

static int
dump_validate_config(struct netpoll *np)
{
	if (!np->local_ip) {
		printk("LKCD: network device %s has no local address, "
				"aborting.\n", np->name);
		return -1;
	}

#define IP(x) ((unsigned char *)&np->local_ip)[x]
	printk("LKCD: Source %d.%d.%d.%d ", IP(0), IP(1), IP(2), IP(3));
#undef IP

	if (!np->local_port) {
		printk("LKCD: source_port parameter not specified, aborting.\n");
		return -1;
	}

	if (!np->remote_ip) {
		printk("LKCD: target_ip parameter not specified, aborting.\n");
		return -1;
	}

	np->remote_ip = ntohl(np->remote_ip);
#define IP(x) ((unsigned char *)&np->remote_ip)[x]
	printk("LKCD: Target %d.%d.%d.%d ", IP(0), IP(1), IP(2), IP(3));
#undef IP

	if (!np->remote_port) {
		printk("LKCD: target_port parameter not specified, aborting.\n");
		return -1;
	}
	printk("LKCD: Target Ethernet Address %02x:%02x:%02x:%02x:%02x:%02x",
		np->remote_mac[0], np->remote_mac[1], np->remote_mac[2], 
		np->remote_mac[3], np->remote_mac[4], np->remote_mac[5]);

	if ((np->remote_mac[0] & np->remote_mac[1] & np->remote_mac[2] & 
		np->remote_mac[3] & np->remote_mac[4] & np->remote_mac[5]) == 255)
		printk("LKCD: (Broadcast)");
	printk("\n");
	return 0;
}

/*
 * Prepares the dump device so we can take a dump later. 
 * Validates the netdump configuration parameters.
 *
 * TODO: Network connectivity check should be done here.
 */
static int
dump_net_open(struct dump_dev *net_dev, const char *arg)
{
	int retval = 0;
	char *p, *larg;
	char *larg_orig;
	u64 tmp;

	if (!(larg = kmalloc(strlen(arg), GFP_KERNEL)))
		return -ENOMEM;

	strcpy(larg, arg);
	larg_orig = larg;

	if ((p = strchr(larg, ',')) != NULL)
		*p = '\0';
	strcpy(net_dev->np.dev_name, larg);
	larg = p + 1;

	if ((p = strchr(larg, ',')) != NULL)
		*p = '\0';
	net_dev->np.remote_ip = simple_strtol(larg, &p, 16);

	larg = p + 1;
	if ((p = strchr(larg, ',')) != NULL)
		*p = '\0';
	net_dev->np.remote_port = simple_strtol(larg, &p, 16);

	larg = p + 1;
	if ((p = strchr(larg, ',')) != NULL)
		*p = '\0';
	net_dev->np.local_port = simple_strtol(larg, &p, 16);
	larg = p + 1;

	tmp = simple_strtoull(larg, NULL, 16);
	net_dev->np.remote_mac[0] = (char) ((tmp & 0x0000ff0000000000LL) >> 40);
	net_dev->np.remote_mac[1] = (char) ((tmp & 0x000000ff00000000LL) >> 32);
	net_dev->np.remote_mac[2] = (char) ((tmp & 0x00000000ff000000LL) >> 24);
	net_dev->np.remote_mac[3] = (char) ((tmp & 0x0000000000ff0000LL) >> 16);
	net_dev->np.remote_mac[4] = (char) ((tmp & 0x000000000000ff00LL) >> 8);
	net_dev->np.remote_mac[5] = (char) ((tmp & 0x00000000000000ffLL));

	net_dev->np.rx_hook = rx_hook;	
	retval = netpoll_setup(&net_dev->np);

	dump_validate_config(&net_dev->np);
	net_dev->curr_offset = 0;
	printk("LKCD: Network device %s successfully configured for dumping\n",
			net_dev->np.dev_name);

	kfree(larg_orig);
	return retval;
}

/*
 * Close the dump device and release associated resources
 * Invoked when unconfiguring the dump device.
 */
static int
dump_net_release(struct dump_dev *net_dev)
{
	netpoll_cleanup(&net_dev->np);
	return 0;
}

/*
 * Prepare the dump device for use (silence any ongoing activity
 * and quiesce state) when the system crashes.
 */
static int
dump_net_silence(struct dump_dev *net_dev)
{
	netpoll_set_trap(1);
	local_irq_save(flags_global);
        startup_handshake = 1;
	net_dev->curr_offset = 0;
	printk("LKCD: Dumping to network device %s on CPU %d ...\n", net_dev->np.name,
			smp_processor_id());
	return 0;
}

/*
 * Invoked when dumping is done. This is the time to put things back 
 * (i.e. undo the effects of dump_block_silence) so the device is 
 * available for normal use.
 */
static int
dump_net_resume(struct dump_dev *net_dev)
{
	int indx;
	size_t str_len;
	reply_t reply;

	sprintf((netdump_membuf + HEADER_LEN), "NETDUMP end.\n");
	str_len = strlen(netdump_membuf + HEADER_LEN);
	for( indx = 0; indx < 6; indx++) {
		reply.code = REPLY_END_NETDUMP;
		reply.nr = 0;
		reply.info = 0;
		netdump_send_packet(&net_dev->np, &reply, str_len);
	}
	printk("LKCD: NETDUMP END!\n");
	local_irq_restore(flags_global);
	netpoll_set_trap(0);
	startup_handshake = 0;
	return 0;
}

/*
 * Seek to the specified offset in the dump device.
 * Makes sure this is a valid offset, otherwise returns an error.
 */
static  int
dump_net_seek(struct dump_dev *net_dev, loff_t off)
{
	net_dev->curr_offset = off;
	return 0;
}

/*
 *
 */
static int
dump_net_write(struct dump_dev *net_dev, void *buf, unsigned long len)
{
	int cnt, i, off;
	ssize_t ret;

	cnt = len/ PAGE_SIZE;

	for (i = 0; i < cnt; i++) {
		off = i* PAGE_SIZE;
		ret = do_netdump(net_dev, buf+off, PAGE_SIZE);
		if (ret <= 0)
			return -1;
		net_dev->curr_offset = net_dev->curr_offset + PAGE_SIZE;
	}
	return len;
}

/*
 * check if the last dump i/o is over and ready for next request
 */
static int
dump_net_ready(struct dump_dev *net_dev, void *buf)
{
	return 0;
}

struct dump_dev_ops dump_netdev_ops = {
	.open 		= dump_net_open,
	.release	= dump_net_release,
	.silence	= dump_net_silence,
	.resume 	= dump_net_resume,
	.seek		= dump_net_seek,
	.write		= dump_net_write,
	/* .read not implemented */
	.ready		= dump_net_ready,
};

static struct dump_dev default_dump_netdev = {
	.type = 2,
	.ops = &dump_netdev_ops, 
	.curr_offset = 0,
	.np.name = "netdump",
	.np.dev_name = "eth0",
	.np.rx_hook = rx_hook,
	.np.local_port = 6688,
	.np.remote_port = 6688,
	.np.remote_mac = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
};

static int __init
dump_netdev_init(void)
{
	default_dump_netdev.curr_offset = 0;

	if (dump_register_device(&default_dump_netdev) < 0) {
		printk("LKCD: network dump device driver registration failed\n");
		return -1;
	}
	printk("LKCD: network device driver for LKCD registered\n");
 
	get_random_bytes(&dump_magic, sizeof(dump_magic));
	return 0;
}

static void __exit
dump_netdev_cleanup(void)
{
	dump_unregister_device(&default_dump_netdev);
}

MODULE_AUTHOR("LKCD Development Team <lkcd-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Network Dump Driver for Linux Kernel Crash Dump (LKCD)");
MODULE_LICENSE("GPL");

module_init(dump_netdev_init);
module_exit(dump_netdev_cleanup);
