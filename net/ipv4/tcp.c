/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp.c,v 1.180 2000/11/28 17:04:09 davem Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Charles Hedrick, <hedrick@klinzhai.rutgers.edu>
 *		Linus Torvalds, <torvalds@cs.helsinki.fi>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Matthew Dillon, <dillon@apollo.west.oic.com>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 *
 * Fixes:
 *		Alan Cox	:	Numerous verify_area() calls
 *		Alan Cox	:	Set the ACK bit on a reset
 *		Alan Cox	:	Stopped it crashing if it closed while
 *					sk->inuse=1 and was trying to connect
 *					(tcp_err()).
 *		Alan Cox	:	All icmp error handling was broken
 *					pointers passed where wrong and the
 *					socket was looked up backwards. Nobody
 *					tested any icmp error code obviously.
 *		Alan Cox	:	tcp_err() now handled properly. It
 *					wakes people on errors. poll
 *					behaves and the icmp error race
 *					has gone by moving it into sock.c
 *		Alan Cox	:	tcp_send_reset() fixed to work for
 *					everything not just packets for
 *					unknown sockets.
 *		Alan Cox	:	tcp option processing.
 *		Alan Cox	:	Reset tweaked (still not 100%) [Had
 *					syn rule wrong]
 *		Herp Rosmanith  :	More reset fixes
 *		Alan Cox	:	No longer acks invalid rst frames.
 *					Acking any kind of RST is right out.
 *		Alan Cox	:	Sets an ignore me flag on an rst
 *					receive otherwise odd bits of prattle
 *					escape still
 *		Alan Cox	:	Fixed another acking RST frame bug.
 *					Should stop LAN workplace lockups.
 *		Alan Cox	: 	Some tidyups using the new skb list
 *					facilities
 *		Alan Cox	:	sk->keepopen now seems to work
 *		Alan Cox	:	Pulls options out correctly on accepts
 *		Alan Cox	:	Fixed assorted sk->rqueue->next errors
 *		Alan Cox	:	PSH doesn't end a TCP read. Switched a
 *					bit to skb ops.
 *		Alan Cox	:	Tidied tcp_data to avoid a potential
 *					nasty.
 *		Alan Cox	:	Added some better commenting, as the
 *					tcp is hard to follow
 *		Alan Cox	:	Removed incorrect check for 20 * psh
 *	Michael O'Reilly	:	ack < copied bug fix.
 *	Johannes Stille		:	Misc tcp fixes (not all in yet).
 *		Alan Cox	:	FIN with no memory -> CRASH
 *		Alan Cox	:	Added socket option proto entries.
 *					Also added awareness of them to accept.
 *		Alan Cox	:	Added TCP options (SOL_TCP)
 *		Alan Cox	:	Switched wakeup calls to callbacks,
 *					so the kernel can layer network
 *					sockets.
 *		Alan Cox	:	Use ip_tos/ip_ttl settings.
 *		Alan Cox	:	Handle FIN (more) properly (we hope).
 *		Alan Cox	:	RST frames sent on unsynchronised
 *					state ack error.
 *		Alan Cox	:	Put in missing check for SYN bit.
 *		Alan Cox	:	Added tcp_select_window() aka NET2E
 *					window non shrink trick.
 *		Alan Cox	:	Added a couple of small NET2E timer
 *					fixes
 *		Charles Hedrick :	TCP fixes
 *		Toomas Tamm	:	TCP window fixes
 *		Alan Cox	:	Small URG fix to rlogin ^C ack fight
 *		Charles Hedrick	:	Rewrote most of it to actually work
 *		Linus		:	Rewrote tcp_read() and URG handling
 *					completely
 *		Gerhard Koerting:	Fixed some missing timer handling
 *		Matthew Dillon  :	Reworked TCP machine states as per RFC
 *		Gerhard Koerting:	PC/TCP workarounds
 *		Adam Caldwell	:	Assorted timer/timing errors
 *		Matthew Dillon	:	Fixed another RST bug
 *		Alan Cox	:	Move to kernel side addressing changes.
 *		Alan Cox	:	Beginning work on TCP fastpathing
 *					(not yet usable)
 *		Arnt Gulbrandsen:	Turbocharged tcp_check() routine.
 *		Alan Cox	:	TCP fast path debugging
 *		Alan Cox	:	Window clamping
 *		Michael Riepe	:	Bug in tcp_check()
 *		Matt Dillon	:	More TCP improvements and RST bug fixes
 *		Matt Dillon	:	Yet more small nasties remove from the
 *					TCP code (Be very nice to this man if
 *					tcp finally works 100%) 8)
 *		Alan Cox	:	BSD accept semantics.
 *		Alan Cox	:	Reset on closedown bug.
 *	Peter De Schrijver	:	ENOTCONN check missing in tcp_sendto().
 *		Michael Pall	:	Handle poll() after URG properly in
 *					all cases.
 *		Michael Pall	:	Undo the last fix in tcp_read_urg()
 *					(multi URG PUSH broke rlogin).
 *		Michael Pall	:	Fix the multi URG PUSH problem in
 *					tcp_readable(), poll() after URG
 *					works now.
 *		Michael Pall	:	recv(...,MSG_OOB) never blocks in the
 *					BSD api.
 *		Alan Cox	:	Changed the semantics of sk->socket to
 *					fix a race and a signal problem with
 *					accept() and async I/O.
 *		Alan Cox	:	Relaxed the rules on tcp_sendto().
 *		Yury Shevchuk	:	Really fixed accept() blocking problem.
 *		Craig I. Hagan  :	Allow for BSD compatible TIME_WAIT for
 *					clients/servers which listen in on
 *					fixed ports.
 *		Alan Cox	:	Cleaned the above up and shrank it to
 *					a sensible code size.
 *		Alan Cox	:	Self connect lockup fix.
 *		Alan Cox	:	No connect to multicast.
 *		Ross Biro	:	Close unaccepted children on master
 *					socket close.
 *		Alan Cox	:	Reset tracing code.
 *		Alan Cox	:	Spurious resets on shutdown.
 *		Alan Cox	:	Giant 15 minute/60 second timer error
 *		Alan Cox	:	Small whoops in polling before an
 *					accept.
 *		Alan Cox	:	Kept the state trace facility since
 *					it's handy for debugging.
 *		Alan Cox	:	More reset handler fixes.
 *		Alan Cox	:	Started rewriting the code based on
 *					the RFC's for other useful protocol
 *					references see: Comer, KA9Q NOS, and
 *					for a reference on the difference
 *					between specifications and how BSD
 *					works see the 4.4lite source.
 *		A.N.Kuznetsov	:	Don't time wait on completion of tidy
 *					close.
 *		Linus Torvalds	:	Fin/Shutdown & copied_seq changes.
 *		Linus Torvalds	:	Fixed BSD port reuse to work first syn
 *		Alan Cox	:	Reimplemented timers as per the RFC
 *					and using multiple timers for sanity.
 *		Alan Cox	:	Small bug fixes, and a lot of new
 *					comments.
 *		Alan Cox	:	Fixed dual reader crash by locking
 *					the buffers (much like datagram.c)
 *		Alan Cox	:	Fixed stuck sockets in probe. A probe
 *					now gets fed up of retrying without
 *					(even a no space) answer.
 *		Alan Cox	:	Extracted closing code better
 *		Alan Cox	:	Fixed the closing state machine to
 *					resemble the RFC.
 *		Alan Cox	:	More 'per spec' fixes.
 *		Jorge Cwik	:	Even faster checksumming.
 *		Alan Cox	:	tcp_data() doesn't ack illegal PSH
 *					only frames. At least one pc tcp stack
 *					generates them.
 *		Alan Cox	:	Cache last socket.
 *		Alan Cox	:	Per route irtt.
 *		Matt Day	:	poll()->select() match BSD precisely on error
 *		Alan Cox	:	New buffers
 *		Marc Tamsky	:	Various sk->prot->retransmits and
 *					sk->retransmits misupdating fixed.
 *					Fixed tcp_write_timeout: stuck close,
 *					and TCP syn retries gets used now.
 *		Mark Yarvis	:	In tcp_read_wakeup(), don't send an
 *					ack if state is TCP_CLOSED.
 *		Alan Cox	:	Look up device on a retransmit - routes may
 *					change. Doesn't yet cope with MSS shrink right
 *					but its a start!
 *		Marc Tamsky	:	Closing in closing fixes.
 *		Mike Shaver	:	RFC1122 verifications.
 *		Alan Cox	:	rcv_saddr errors.
 *		Alan Cox	:	Block double connect().
 *		Alan Cox	:	Small hooks for enSKIP.
 *		Alexey Kuznetsov:	Path MTU discovery.
 *		Alan Cox	:	Support soft errors.
 *		Alan Cox	:	Fix MTU discovery pathological case
 *					when the remote claims no mtu!
 *		Marc Tamsky	:	TCP_CLOSE fix.
 *		Colin (G3TNE)	:	Send a reset on syn ack replies in
 *					window but wrong (fixes NT lpd problems)
 *		Pedro Roque	:	Better TCP window handling, delayed ack.
 *		Joerg Reuter	:	No modification of locked buffers in
 *					tcp_do_retransmit()
 *		Eric Schenk	:	Changed receiver side silly window
 *					avoidance algorithm to BSD style
 *					algorithm. This doubles throughput
 *					against machines running Solaris,
 *					and seems to result in general
 *					improvement.
 *	Stefan Magdalinski	:	adjusted tcp_readable() to fix FIONREAD
 *	Willy Konynenberg	:	Transparent proxying support.
 *	Mike McLagan		:	Routing by source
 *		Keith Owens	:	Do proper merging with partial SKB's in
 *					tcp_do_sendmsg to avoid burstiness.
 *		Eric Schenk	:	Fix fast close down bug with
 *					shutdown() followed by close().
 *		Andi Kleen 	:	Make poll agree with SIGIO
 *	Salvatore Sanfilippo	:	Support SO_LINGER with linger == 1 and
 *					lingertime == 0 (RFC 793 ABORT Call)
 *					
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or(at your option) any later version.
 *
 * Description of States:
 *
 *	TCP_SYN_SENT		sent a connection request, waiting for ack
 *
 *	TCP_SYN_RECV		received a connection request, sent ack,
 *				waiting for final ack in three-way handshake.
 *
 *	TCP_ESTABLISHED		connection established
 *
 *	TCP_FIN_WAIT1		our side has shutdown, waiting to complete
 *				transmission of remaining buffered data
 *
 *	TCP_FIN_WAIT2		all buffered data sent, waiting for remote
 *				to shutdown
 *
 *	TCP_CLOSING		both sides have shutdown but we still have
 *				data we have to finish sending
 *
 *	TCP_TIME_WAIT		timeout to catch resent junk before entering
 *				closed, can only be entered from FIN_WAIT2
 *				or CLOSING.  Required because the other end
 *				may not have gotten our last ACK causing it
 *				to retransmit the data packet (which we ignore)
 *
 *	TCP_CLOSE_WAIT		remote side has shutdown and is waiting for
 *				us to finish writing our data and to shutdown
 *				(we have to close() to move on to LAST_ACK)
 *
 *	TCP_LAST_ACK		out side has shutdown after remote has
 *				shutdown.  There may still be data in our
 *				buffer that we have to finish sending
 *
 *	TCP_CLOSE		socket is finished
 */

/*
 * RFC1122 status:
 * NOTE: I'm not going to be doing comments in the code for this one except
 * for violations and the like.  tcp.c is just too big... If I say something
 * "does?" or "doesn't?", it means I'm not sure, and will have to hash it out
 * with Alan. -- MS 950903
 * [Note: Most of the TCP code has been rewriten/redesigned since this 
 *  RFC1122 check. It is probably not correct anymore. It should be redone 
 *  before 2.2. -AK]
 *
 * Use of PSH (4.2.2.2)
 *   MAY aggregate data sent without the PSH flag. (does)
 *   MAY queue data received without the PSH flag. (does)
 *   SHOULD collapse successive PSH flags when it packetizes data. (doesn't)
 *   MAY implement PSH on send calls. (doesn't, thus:)
 *     MUST NOT buffer data indefinitely (doesn't [1 second])
 *     MUST set PSH on last segment (does)
 *   MAY pass received PSH to application layer (doesn't)
 *   SHOULD send maximum-sized segment whenever possible. (almost always does)
 *
 * Window Size (4.2.2.3, 4.2.2.16)
 *   MUST treat window size as an unsigned number (does)
 *   SHOULD treat window size as a 32-bit number (does not)
 *   MUST NOT shrink window once it is offered (does not normally)
 *
 * Urgent Pointer (4.2.2.4)
 * **MUST point urgent pointer to last byte of urgent data (not right
 *     after). (doesn't, to be like BSD. That's configurable, but defaults
 *	to off)
 *   MUST inform application layer asynchronously of incoming urgent
 *     data. (does)
 *   MUST provide application with means of determining the amount of
 *     urgent data pending. (does)
 * **MUST support urgent data sequence of arbitrary length. (doesn't, but
 *   it's sort of tricky to fix, as urg_ptr is a 16-bit quantity)
 *	[Follows BSD 1 byte of urgent data]
 *
 * TCP Options (4.2.2.5)
 *   MUST be able to receive TCP options in any segment. (does)
 *   MUST ignore unsupported options (does)
 *
 * Maximum Segment Size Option (4.2.2.6)
 *   MUST implement both sending and receiving MSS. (does, but currently
 *	only uses the smaller of both of them)
 *   SHOULD send an MSS with every SYN where receive MSS != 536 (MAY send
 *     it always). (does, even when MSS == 536, which is legal)
 *   MUST assume MSS == 536 if no MSS received at connection setup (does)
 *   MUST calculate "effective send MSS" correctly:
 *     min(physical_MTU, remote_MSS+20) - sizeof(tcphdr) - sizeof(ipopts)
 *     (does - but allows operator override)
 *
 * TCP Checksum (4.2.2.7)
 *   MUST generate and check TCP checksum. (does)
 *
 * Initial Sequence Number Selection (4.2.2.8)
 *   MUST use the RFC 793 clock selection mechanism.  (doesn't, but it's
 *     OK: RFC 793 specifies a 250KHz clock, while we use 1MHz, which is
 *     necessary for 10Mbps networks - and harder than BSD to spoof!
 *     With syncookies we don't)
 *
 * Simultaneous Open Attempts (4.2.2.10)
 *   MUST support simultaneous open attempts (does)
 *
 * Recovery from Old Duplicate SYN (4.2.2.11)
 *   MUST keep track of active vs. passive open (does)
 *
 * RST segment (4.2.2.12)
 *   SHOULD allow an RST segment to contain data (does, but doesn't do
 *     anything with it, which is standard)
 *
 * Closing a Connection (4.2.2.13)
 *   MUST inform application of whether connection was closed by RST or
 *     normal close. (does)
 *   MAY allow "half-duplex" close (treat connection as closed for the
 *     local app, even before handshake is done). (does)
 *   MUST linger in TIME_WAIT for 2 * MSL (does)
 *
 * Retransmission Timeout (4.2.2.15)
 *   MUST implement Jacobson's slow start and congestion avoidance
 *     stuff. (does)
 *
 * Probing Zero Windows (4.2.2.17)
 *   MUST support probing of zero windows. (does)
 *   MAY keep offered window closed indefinitely. (does)
 *   MUST allow remote window to stay closed indefinitely. (does)
 *
 * Passive Open Calls (4.2.2.18)
 *   MUST NOT let new passive open affect other connections. (doesn't)
 *   MUST support passive opens (LISTENs) concurrently. (does)
 *
 * Time to Live (4.2.2.19)
 *   MUST make TCP TTL configurable. (does - IP_TTL option)
 *
 * Event Processing (4.2.2.20)
 *   SHOULD queue out-of-order segments. (does)
 *   MUST aggregate ACK segments whenever possible. (does but badly)
 *
 * Retransmission Timeout Calculation (4.2.3.1)
 *   MUST implement Karn's algorithm and Jacobson's algorithm for RTO
 *     calculation. (does, or at least explains them in the comments 8*b)
 *  SHOULD initialize RTO to 0 and RTT to 3. (does)
 *
 * When to Send an ACK Segment (4.2.3.2)
 *   SHOULD implement delayed ACK. (does)
 *   MUST keep ACK delay < 0.5 sec. (does)
 *
 * When to Send a Window Update (4.2.3.3)
 *   MUST implement receiver-side SWS. (does)
 *
 * When to Send Data (4.2.3.4)
 *   MUST implement sender-side SWS. (does)
 *   SHOULD implement Nagle algorithm. (does)
 *
 * TCP Connection Failures (4.2.3.5)
 *  MUST handle excessive retransmissions "properly" (see the RFC). (does)
 *   SHOULD inform application layer of soft errors. (does)
 *
 * TCP Keep-Alives (4.2.3.6)
 *   MAY provide keep-alives. (does)
 *   MUST make keep-alives configurable on a per-connection basis. (does)
 *   MUST default to no keep-alives. (does)
 *   MUST make keep-alive interval configurable. (does)
 *   MUST make default keep-alive interval > 2 hours. (does)
 *   MUST NOT interpret failure to ACK keep-alive packet as dead
 *     connection. (doesn't)
 *   SHOULD send keep-alive with no data. (does)
 *
 * TCP Multihoming (4.2.3.7)
 *   MUST get source address from IP layer before sending first
 *     SYN. (does)
 *   MUST use same local address for all segments of a connection. (does)
 *
 * IP Options (4.2.3.8)
 *   MUST ignore unsupported IP options. (does)
 *   MAY support Time Stamp and Record Route. (does)
 *   MUST allow application to specify a source route. (does)
 *   MUST allow received Source Route option to set route for all future
 *     segments on this connection. (does not (security issues))
 *
 * ICMP messages (4.2.3.9)
 *   MUST act on ICMP errors. (does)
 *   MUST slow transmission upon receipt of a Source Quench. (doesn't anymore 
 *   because that is deprecated now by the IETF, can be turned on)
 *   MUST NOT abort connection upon receipt of soft Destination
 *     Unreachables (0, 1, 5), Time Exceededs and Parameter
 *     Problems. (doesn't)
 *   SHOULD report soft Destination Unreachables etc. to the
 *     application. (does, except during SYN_RECV and may drop messages
 *     in some rare cases before accept() - ICMP is unreliable)	
 *   SHOULD abort connection upon receipt of hard Destination Unreachable
 *     messages (2, 3, 4). (does, but see above)
 *
 * Remote Address Validation (4.2.3.10)
 *   MUST reject as an error OPEN for invalid remote IP address. (does)
 *   MUST ignore SYN with invalid source address. (does)
 *   MUST silently discard incoming SYN for broadcast/multicast
 *     address. (does)
 *
 * Asynchronous Reports (4.2.4.1)
 * MUST provide mechanism for reporting soft errors to application
 *     layer. (does)
 *
 * Type of Service (4.2.4.2)
 *   MUST allow application layer to set Type of Service. (does IP_TOS)
 *
 * (Whew. -- MS 950903)
 * (Updated by AK, but not complete yet.)
 **/

#include <linux/config.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/smp_lock.h>

#include <net/icmp.h>
#include <net/tcp.h>

#include <asm/uaccess.h>

int sysctl_tcp_fin_timeout = TCP_FIN_TIMEOUT;

struct tcp_mib	tcp_statistics[NR_CPUS*2];

kmem_cache_t *tcp_openreq_cachep;
kmem_cache_t *tcp_bucket_cachep;
kmem_cache_t *tcp_timewait_cachep;

atomic_t tcp_orphan_count = ATOMIC_INIT(0);

int sysctl_tcp_mem[3];
int sysctl_tcp_wmem[3] = { 4*1024, 16*1024, 128*1024 };
int sysctl_tcp_rmem[3] = { 4*1024, 87380, 87380*2 };

atomic_t tcp_memory_allocated;	/* Current allocated memory. */
atomic_t tcp_sockets_allocated;	/* Current number of TCP sockets. */

/* Pressure flag: try to collapse.
 * Technical note: it is used by multiple contexts non atomically.
 * All the tcp_mem_schedule() is of this nature: accounting
 * is strict, actions are advisory and have some latency. */
int tcp_memory_pressure;

#define TCP_PAGES(amt) (((amt)+TCP_MEM_QUANTUM-1)/TCP_MEM_QUANTUM)

int tcp_mem_schedule(struct sock *sk, int size, int kind)
{
	int amt = TCP_PAGES(size);

	sk->forward_alloc += amt*TCP_MEM_QUANTUM;
	atomic_add(amt, &tcp_memory_allocated);

	/* Under limit. */
	if (atomic_read(&tcp_memory_allocated) < sysctl_tcp_mem[0]) {
		if (tcp_memory_pressure)
			tcp_memory_pressure = 0;
		return 1;
	}

	/* Over hard limit. */
	if (atomic_read(&tcp_memory_allocated) > sysctl_tcp_mem[2]) {
		tcp_enter_memory_pressure();
		goto suppress_allocation;
	}

	/* Under pressure. */
	if (atomic_read(&tcp_memory_allocated) > sysctl_tcp_mem[1])
		tcp_enter_memory_pressure();

	if (kind) {
		if (atomic_read(&sk->rmem_alloc) < sysctl_tcp_rmem[0])
			return 1;
	} else {
		if (sk->wmem_queued < sysctl_tcp_wmem[0])
			return 1;
	}

	if (!tcp_memory_pressure ||
	    sysctl_tcp_mem[2] > atomic_read(&tcp_sockets_allocated)
	    * TCP_PAGES(sk->wmem_queued+atomic_read(&sk->rmem_alloc)+
			sk->forward_alloc))
		return 1;

suppress_allocation:

	if (kind == 0) {
		tcp_moderate_sndbuf(sk);

		/* Fail only if socket is _under_ its sndbuf.
		 * In this case we cannot block, so that we have to fail.
		 */
		if (sk->wmem_queued+size >= sk->sndbuf)
			return 1;
	}

	/* Alas. Undo changes. */
	sk->forward_alloc -= amt*TCP_MEM_QUANTUM;
	atomic_sub(amt, &tcp_memory_allocated);
	return 0;
}

void __tcp_mem_reclaim(struct sock *sk)
{
	if (sk->forward_alloc >= TCP_MEM_QUANTUM) {
		atomic_sub(sk->forward_alloc/TCP_MEM_QUANTUM, &tcp_memory_allocated);
		sk->forward_alloc &= (TCP_MEM_QUANTUM-1);
		if (tcp_memory_pressure &&
		    atomic_read(&tcp_memory_allocated) < sysctl_tcp_mem[0])
			tcp_memory_pressure = 0;
	}
}

void tcp_rfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	atomic_sub(skb->truesize, &sk->rmem_alloc);
	sk->forward_alloc += skb->truesize;
}

/*
 * LISTEN is a special case for poll..
 */
static __inline__ unsigned int tcp_listen_poll(struct sock *sk, poll_table *wait)
{
	return sk->tp_pinfo.af_tcp.accept_queue ? (POLLIN | POLLRDNORM) : 0;
}

/*
 *	Wait for a TCP event.
 *
 *	Note that we don't need to lock the socket, as the upper poll layers
 *	take care of normal races (between the test and the event) and we don't
 *	go look at any of the socket buffers directly.
 */
unsigned int tcp_poll(struct file * file, struct socket *sock, poll_table *wait)
{
	unsigned int mask;
	struct sock *sk = sock->sk;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	poll_wait(file, sk->sleep, wait);
	if (sk->state == TCP_LISTEN)
		return tcp_listen_poll(sk, wait);

	/* Socket is not locked. We are protected from async events
	   by poll logic and correct handling of state changes
	   made by another threads is impossible in any case.
	 */

	mask = 0;
	if (sk->err)
		mask = POLLERR;

	/*
	 * POLLHUP is certainly not done right. But poll() doesn't
	 * have a notion of HUP in just one direction, and for a
	 * socket the read side is more interesting.
	 *
	 * Some poll() documentation says that POLLHUP is incompatible
	 * with the POLLOUT/POLLWR flags, so somebody should check this
	 * all. But careful, it tends to be safer to return too many
	 * bits than too few, and you can easily break real applications
	 * if you don't tell them that something has hung up!
	 *
	 * Check-me.
	 *
	 * Check number 1. POLLHUP is _UNMASKABLE_ event (see UNIX98 and
	 * our fs/select.c). It means that after we received EOF,
	 * poll always returns immediately, making impossible poll() on write()
	 * in state CLOSE_WAIT. One solution is evident --- to set POLLHUP
	 * if and only if shutdown has been made in both directions.
	 * Actually, it is interesting to look how Solaris and DUX
	 * solve this dilemma. I would prefer, if PULLHUP were maskable,
	 * then we could set it on SND_SHUTDOWN. BTW examples given
	 * in Stevens' books assume exactly this behaviour, it explains
	 * why PULLHUP is incompatible with POLLOUT.	--ANK
	 *
	 * NOTE. Check for TCP_CLOSE is added. The goal is to prevent
	 * blocking on fresh not-connected or disconnected socket. --ANK
	 */
	if (sk->shutdown == SHUTDOWN_MASK || sk->state == TCP_CLOSE)
		mask |= POLLHUP;
	if (sk->shutdown & RCV_SHUTDOWN)
		mask |= POLLIN | POLLRDNORM;

	/* Connected? */
	if ((1 << sk->state) & ~(TCPF_SYN_SENT|TCPF_SYN_RECV)) {
		/* Potential race condition. If read of tp below will
		 * escape above sk->state, we can be illegally awaken
		 * in SYN_* states. */
		if ((tp->rcv_nxt != tp->copied_seq) &&
		    (tp->urg_seq != tp->copied_seq ||
		     tp->rcv_nxt != tp->copied_seq+1 ||
		     sk->urginline || !tp->urg_data))
			mask |= POLLIN | POLLRDNORM;

		if (!(sk->shutdown & SEND_SHUTDOWN)) {
			if (tcp_wspace(sk) >= tcp_min_write_space(sk)) {
				mask |= POLLOUT | POLLWRNORM;
			} else {  /* send SIGIO later */
				set_bit(SOCK_ASYNC_NOSPACE, &sk->socket->flags);
				set_bit(SOCK_NOSPACE, &sk->socket->flags);

				/* Race breaker. If space is freed after
				 * wspace test but before the flags are set,
				 * IO signal will be lost.
				 */
				if (tcp_wspace(sk) >= tcp_min_write_space(sk))
					mask |= POLLOUT | POLLWRNORM;
			}
		}

		if (tp->urg_data & TCP_URG_VALID)
			mask |= POLLPRI;
	}
	return mask;
}

/*
 *	TCP socket write_space callback. Not used.
 */
void tcp_write_space(struct sock *sk)
{
}

int tcp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int answ;

	switch(cmd) {
	case SIOCINQ:
		if (sk->state == TCP_LISTEN)
			return(-EINVAL);

		lock_sock(sk);
		if ((1<<sk->state) & (TCPF_SYN_SENT|TCPF_SYN_RECV))
			answ = 0;
		else if (sk->urginline || !tp->urg_data ||
			 before(tp->urg_seq,tp->copied_seq) ||
			 !before(tp->urg_seq,tp->rcv_nxt)) {
			answ = tp->rcv_nxt - tp->copied_seq;

			/* Subtract 1, if FIN is in queue. */
			if (answ && !skb_queue_empty(&sk->receive_queue))
				answ -= ((struct sk_buff*)sk->receive_queue.prev)->h.th->fin;
		} else
			answ = tp->urg_seq - tp->copied_seq;
		release_sock(sk);
		break;
	case SIOCATMARK:
		{
			answ = tp->urg_data && tp->urg_seq == tp->copied_seq;
			break;
		}
	case SIOCOUTQ:
		if (sk->state == TCP_LISTEN)
			return(-EINVAL);

		if ((1<<sk->state) & (TCPF_SYN_SENT|TCPF_SYN_RECV))
			answ = 0;
		else
			answ = tp->write_seq - tp->snd_una;
		break;
	default:
		return(-ENOIOCTLCMD);
	};

	return put_user(answ, (int *)arg);
}


int tcp_listen_start(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct tcp_listen_opt *lopt;

	sk->max_ack_backlog = 0;
	sk->ack_backlog = 0;
	tp->accept_queue = tp->accept_queue_tail = NULL;
	tp->syn_wait_lock = RW_LOCK_UNLOCKED;

	lopt = kmalloc(sizeof(struct tcp_listen_opt), GFP_KERNEL);
	if (!lopt)
		return -ENOMEM;

	memset(lopt, 0, sizeof(struct tcp_listen_opt));
	for (lopt->max_qlen_log = 6; ; lopt->max_qlen_log++)
		if ((1<<lopt->max_qlen_log) >= sysctl_max_syn_backlog)
			break;

	write_lock_bh(&tp->syn_wait_lock);
	tp->listen_opt = lopt;
	write_unlock_bh(&tp->syn_wait_lock);

	/* There is race window here: we announce ourselves listening,
	 * but this transition is still not validated by get_port().
	 * It is OK, because this socket enters to hash table only
	 * after validation is complete.
	 */
	sk->state = TCP_LISTEN;
	if (sk->prot->get_port(sk, sk->num) == 0) {
		sk->sport = htons(sk->num);

		sk_dst_reset(sk);
		sk->prot->hash(sk);

		return 0;
	}

	sk->state = TCP_CLOSE;
	write_lock_bh(&tp->syn_wait_lock);
	tp->listen_opt = NULL;
	write_unlock_bh(&tp->syn_wait_lock);
	kfree(lopt);
	return -EADDRINUSE;
}

/*
 *	This routine closes sockets which have been at least partially
 *	opened, but not yet accepted.
 */

static void tcp_listen_stop (struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct tcp_listen_opt *lopt = tp->listen_opt;
	struct open_request *acc_req = tp->accept_queue;
	struct open_request *req;
	int i;

	tcp_delete_keepalive_timer(sk);

	/* make all the listen_opt local to us */
	write_lock_bh(&tp->syn_wait_lock);
	tp->listen_opt =NULL;
	write_unlock_bh(&tp->syn_wait_lock);
	tp->accept_queue = tp->accept_queue_tail = NULL;

	if (lopt->qlen) {
		for (i=0; i<TCP_SYNQ_HSIZE; i++) {
			while ((req = lopt->syn_table[i]) != NULL) {
				lopt->syn_table[i] = req->dl_next;
				lopt->qlen--;
				tcp_openreq_free(req);

		/* Following specs, it would be better either to send FIN
		 * (and enter FIN-WAIT-1, it is normal close)
		 * or to send active reset (abort). 
		 * Certainly, it is pretty dangerous while synflood, but it is
		 * bad justification for our negligence 8)
		 * To be honest, we are not able to make either
		 * of the variants now.			--ANK
		 */
			}
		}
	}
	BUG_TRAP(lopt->qlen == 0);

	kfree(lopt);

	while ((req=acc_req) != NULL) {
		struct sock *child = req->sk;

		acc_req = req->dl_next;

		local_bh_disable();
		bh_lock_sock(child);
		BUG_TRAP(child->lock.users==0);
		sock_hold(child);

		tcp_disconnect(child, O_NONBLOCK);

		sock_orphan(child);

		atomic_inc(&tcp_orphan_count);

		tcp_destroy_sock(child);

		bh_unlock_sock(child);
		local_bh_enable();
		sock_put(child);

		tcp_acceptq_removed(sk);
		tcp_openreq_fastfree(req);
	}
	BUG_TRAP(sk->ack_backlog == 0);
}

/*
 *	Wait for a socket to get into the connected state
 *
 *	Note: Must be called with the socket locked.
 */
static int wait_for_tcp_connect(struct sock * sk, int flags, long *timeo_p)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	while((1 << sk->state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)) {
		if(sk->err)
			return sock_error(sk);
		if((1 << sk->state) &
		   ~(TCPF_SYN_SENT | TCPF_SYN_RECV)) {
			if(sk->keepopen && !(flags&MSG_NOSIGNAL))
				send_sig(SIGPIPE, tsk, 0);
			return -EPIPE;
		}
		if(!*timeo_p)
			return -EAGAIN;
		if(signal_pending(tsk))
			return sock_intr_errno(*timeo_p);

		__set_task_state(tsk, TASK_INTERRUPTIBLE);
		add_wait_queue(sk->sleep, &wait);
		sk->tp_pinfo.af_tcp.write_pending++;

		release_sock(sk);
		*timeo_p = schedule_timeout(*timeo_p);
		lock_sock(sk);

		__set_task_state(tsk, TASK_RUNNING);
		remove_wait_queue(sk->sleep, &wait);
		sk->tp_pinfo.af_tcp.write_pending--;
	}
	return 0;
}

static inline int tcp_memory_free(struct sock *sk)
{
	return sk->wmem_queued < sk->sndbuf;
}

/*
 *	Wait for more memory for a socket
 */
static long wait_for_tcp_memory(struct sock * sk, long timeo)
{
	long vm_wait = 0;
	long current_timeo = timeo;
	DECLARE_WAITQUEUE(wait, current);

	if (tcp_memory_free(sk))
		current_timeo = vm_wait = (net_random()%(HZ/5))+2;

	clear_bit(SOCK_ASYNC_NOSPACE, &sk->socket->flags);

	add_wait_queue(sk->sleep, &wait);
	for (;;) {
		set_bit(SOCK_NOSPACE, &sk->socket->flags);

		set_current_state(TASK_INTERRUPTIBLE);

		if (signal_pending(current))
			break;
		if (tcp_memory_free(sk) && !vm_wait)
			break;
		if (sk->shutdown & SEND_SHUTDOWN)
			break;
		if (sk->err)
			break;
		release_sock(sk);
		if (!tcp_memory_free(sk) || vm_wait)
			current_timeo = schedule_timeout(current_timeo);
		lock_sock(sk);
		if (vm_wait) {
			if (timeo != MAX_SCHEDULE_TIMEOUT &&
			    (timeo -= vm_wait-current_timeo) < 0)
				timeo = 0;
			break;
		} else {
			timeo = current_timeo;
		}
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(sk->sleep, &wait);
	return timeo;
}

/* When all user supplied data has been queued set the PSH bit */
#define PSH_NEEDED (seglen == 0 && iovlen == 0)

/*
 *	This routine copies from a user buffer into a socket,
 *	and starts the transmit system.
 */

int tcp_sendmsg(struct sock *sk, struct msghdr *msg, int size)
{
	struct iovec *iov;
	struct tcp_opt *tp;
	struct sk_buff *skb;
	int iovlen, flags;
	int mss_now;
	int err, copied;
	long timeo;

	err = 0;
	tp = &(sk->tp_pinfo.af_tcp);

	lock_sock(sk);
	TCP_CHECK_TIMER(sk);

	flags = msg->msg_flags;

	timeo = sock_sndtimeo(sk, flags&MSG_DONTWAIT);

	/* Wait for a connection to finish. */
	if ((1 << sk->state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT))
		if((err = wait_for_tcp_connect(sk, flags, &timeo)) != 0)
			goto out_unlock;

	/* This should be in poll */
	clear_bit(SOCK_ASYNC_NOSPACE, &sk->socket->flags);

	mss_now = tcp_current_mss(sk);

	/* Ok commence sending. */
	iovlen = msg->msg_iovlen;
	iov = msg->msg_iov;
	copied = 0;

	while (--iovlen >= 0) {
		int seglen=iov->iov_len;
		unsigned char * from=iov->iov_base;

		iov++;

		while (seglen > 0) {
			int copy, tmp, queue_it;

			if (err)
				goto do_fault2;

			/* Stop on errors. */
			if (sk->err)
				goto do_sock_err;

			/* Make sure that we are established. */
			if (sk->shutdown & SEND_SHUTDOWN)
				goto do_shutdown;
	
			/* Now we need to check if we have a half
			 * built packet we can tack some data onto.
			 */
			skb = sk->write_queue.prev;
			if (tp->send_head &&
			    (mss_now - skb->len) > 0) {
				copy = skb->len;
				if (skb_tailroom(skb) > 0) {
					int last_byte_was_odd = (copy % 4);

					copy = mss_now - copy;
					if(copy > skb_tailroom(skb))
						copy = skb_tailroom(skb);
					if(copy > seglen)
						copy = seglen;
					if(last_byte_was_odd) {
						if(copy_from_user(skb_put(skb, copy),
								  from, copy))
							err = -EFAULT;
						skb->csum = csum_partial(skb->data,
									 skb->len, 0);
					} else {
						skb->csum =
							csum_and_copy_from_user(
							from, skb_put(skb, copy),
							copy, skb->csum, &err);
					}
					/*
					 * FIXME: the *_user functions should
					 *	  return how much data was
					 *	  copied before the fault
					 *	  occurred and then a partial
					 *	  packet with this data should
					 *	  be sent.  Unfortunately
					 *	  csum_and_copy_from_user doesn't
					 *	  return this information.
					 *	  ATM it might send partly zeroed
					 *	  data in this case.
					 */
					tp->write_seq += copy;
					TCP_SKB_CB(skb)->end_seq += copy;
					from += copy;
					copied += copy;
					seglen -= copy;
					if (PSH_NEEDED ||
					    after(tp->write_seq, tp->pushed_seq+(tp->max_window>>1))) {
						TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_PSH;
						tp->pushed_seq = tp->write_seq;
					}
					if (flags&MSG_OOB) {
						tp->urg_mode = 1;
						tp->snd_up = tp->write_seq;
						TCP_SKB_CB(skb)->sacked |= TCPCB_URG;
					}
					continue;
				} else {
					TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_PSH;
					tp->pushed_seq = tp->write_seq;
				}
			}

			copy = min(seglen, mss_now);

			/* Determine how large of a buffer to allocate.  */
			tmp = MAX_TCP_HEADER + 15 + tp->mss_cache;
			if (copy < mss_now && !(flags & MSG_OOB)) {
				/* What is happening here is that we want to
				 * tack on later members of the users iovec
				 * if possible into a single frame.  When we
				 * leave this loop our we check to see if
				 * we can send queued frames onto the wire.
				 */
				queue_it = 1;
			} else {
				queue_it = 0;
			}

			skb = NULL;
			if (tcp_memory_free(sk))
				skb = tcp_alloc_skb(sk, tmp, sk->allocation);
			if (skb == NULL) {
				/* If we didn't get any memory, we need to sleep. */
				set_bit(SOCK_ASYNC_NOSPACE, &sk->socket->flags);
				set_bit(SOCK_NOSPACE, &sk->socket->flags);

				__tcp_push_pending_frames(sk, tp, mss_now, 1);

				if (!timeo) {
					err = -EAGAIN;
					goto do_interrupted;
				}
				if (signal_pending(current)) {
					err = sock_intr_errno(timeo);
					goto do_interrupted;
				}
				timeo = wait_for_tcp_memory(sk, timeo);

				/* If SACK's were formed or PMTU events happened,
				 * we must find out about it.
				 */
				mss_now = tcp_current_mss(sk);
				continue;
			}

			seglen -= copy;

			/* Prepare control bits for TCP header creation engine. */
			if (PSH_NEEDED ||
			    after(tp->write_seq+copy, tp->pushed_seq+(tp->max_window>>1))) {
				TCP_SKB_CB(skb)->flags = TCPCB_FLAG_ACK|TCPCB_FLAG_PSH;
				tp->pushed_seq = tp->write_seq + copy;
			} else {
				TCP_SKB_CB(skb)->flags = TCPCB_FLAG_ACK;
			}
			TCP_SKB_CB(skb)->sacked = 0;
			if (flags & MSG_OOB) {
				TCP_SKB_CB(skb)->sacked |= TCPCB_URG;
				tp->urg_mode = 1;
				tp->snd_up = tp->write_seq + copy;
			}

			/* TCP data bytes are SKB_PUT() on top, later
			 * TCP+IP+DEV headers are SKB_PUSH()'d beneath.
			 * Reserve header space and checksum the data.
			 */
			skb_reserve(skb, MAX_TCP_HEADER);
			skb->csum = csum_and_copy_from_user(from,
					skb_put(skb, copy), copy, 0, &err);

			if (err)
				goto do_fault;

			from += copy;
			copied += copy;

			TCP_SKB_CB(skb)->seq = tp->write_seq;
			TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + copy;

			/* This advances tp->write_seq for us. */
			tcp_send_skb(sk, skb, queue_it, mss_now);
		}
	}
	err = copied;
out:
	__tcp_push_pending_frames(sk, tp, mss_now, tp->nonagle);
out_unlock:
	TCP_CHECK_TIMER(sk);
	release_sock(sk);
	return err;

do_sock_err:
	if (copied)
		err = copied;
	else
		err = sock_error(sk);
	goto out;
do_shutdown:
	if (copied)
		err = copied;
	else {
		if (!(flags&MSG_NOSIGNAL))
			send_sig(SIGPIPE, current, 0);
		err = -EPIPE;
	}
	goto out;
do_interrupted:
	if (copied)
		err = copied;
	goto out_unlock;
do_fault:
	__kfree_skb(skb);
do_fault2:
	if (copied)
		err = copied;
	else
		err = -EFAULT;
	goto out;
}

#undef PSH_NEEDED

/*
 *	Handle reading urgent data. BSD has very simple semantics for
 *	this, no blocking and very strange errors 8)
 */

static int tcp_recv_urg(struct sock * sk, long timeo,
			struct msghdr *msg, int len, int flags, 
			int *addr_len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* No URG data to read. */
	if (sk->urginline || !tp->urg_data || tp->urg_data == TCP_URG_READ)
		return -EINVAL;	/* Yes this is right ! */

	if (sk->state==TCP_CLOSE && !sk->done)
		return -ENOTCONN;

	if (tp->urg_data & TCP_URG_VALID) {
		int err = 0; 
		char c = tp->urg_data;

		if (!(flags & MSG_PEEK))
			tp->urg_data = TCP_URG_READ;

		/* Read urgent data. */
		msg->msg_flags|=MSG_OOB;

		if(len>0) {
			if (!(flags & MSG_PEEK))
				err = memcpy_toiovec(msg->msg_iov, &c, 1);
			len = 1;
		} else
			msg->msg_flags|=MSG_TRUNC;

		return err ? -EFAULT : len;
	}

	if (sk->state == TCP_CLOSE || (sk->shutdown & RCV_SHUTDOWN))
		return 0;

	/* Fixed the recv(..., MSG_OOB) behaviour.  BSD docs and
	 * the available implementations agree in this case:
	 * this call should never block, independent of the
	 * blocking state of the socket.
	 * Mike <pall@rz.uni-karlsruhe.de>
	 */
	return -EAGAIN;
}

/*
 *	Release a skb if it is no longer needed. This routine
 *	must be called with interrupts disabled or with the
 *	socket locked so that the sk_buff queue operation is ok.
 */

static inline void tcp_eat_skb(struct sock *sk, struct sk_buff * skb)
{
	__skb_unlink(skb, &sk->receive_queue);
	__kfree_skb(skb);
}

/* Clean up the receive buffer for full frames taken by the user,
 * then send an ACK if necessary.  COPIED is the number of bytes
 * tcp_recvmsg has given to the user so far, it speeds up the
 * calculation of whether or not we must ACK for the sake of
 * a window update.
 */
static void cleanup_rbuf(struct sock *sk, int copied)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;
	int time_to_ack = 0;

	/* NOTE! The socket must be locked, so that we don't get
	 * a messed-up receive queue.
	 */
	while ((skb=skb_peek(&sk->receive_queue)) != NULL) {
		if (!skb->used)
			break;
		tcp_eat_skb(sk, skb);
	}

	if (tcp_ack_scheduled(tp)) {
		   /* Delayed ACKs frequently hit locked sockets during bulk receive. */
		if (tp->ack.blocked
		    /* Once-per-two-segments ACK was not sent by tcp_input.c */
		    || tp->rcv_nxt - tp->rcv_wup > tp->ack.rcv_mss
		    /*
		     * If this read emptied read buffer, we send ACK, if
		     * connection is not bidirectional, user drained
		     * receive buffer and there was a small segment
		     * in queue.
		     */
		    || (copied > 0 &&
			(tp->ack.pending&TCP_ACK_PUSHED) &&
			!tp->ack.pingpong &&
			atomic_read(&sk->rmem_alloc) == 0)) {
			time_to_ack = 1;
		}
	}

  	/* We send an ACK if we can now advertise a non-zero window
	 * which has been raised "significantly".
	 *
	 * Even if window raised up to infinity, do not send window open ACK
	 * in states, where we will not receive more. It is useless.
  	 */
	if(copied > 0 && !time_to_ack && !(sk->shutdown&RCV_SHUTDOWN)) {
		__u32 rcv_window_now = tcp_receive_window(tp);

		/* Optimize, __tcp_select_window() is not cheap. */
		if (2*rcv_window_now <= tp->window_clamp) {
			__u32 new_window = __tcp_select_window(sk);

			/* Send ACK now, if this read freed lots of space
			 * in our buffer. Certainly, new_window is new window.
			 * We can advertise it now, if it is not less than current one.
			 * "Lots" means "at least twice" here.
			 */
			if(new_window && new_window >= 2*rcv_window_now)
				time_to_ack = 1;
		}
	}
	if (time_to_ack)
		tcp_send_ack(sk);
}

/* Now socket state including sk->err is changed only under lock,
 * hence we may omit checks after joining wait queue.
 * We check receive queue before schedule() only as optimization;
 * it is very likely that release_sock() added new data.
 */

static long tcp_data_wait(struct sock *sk, long timeo)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(sk->sleep, &wait);

	__set_current_state(TASK_INTERRUPTIBLE);

	set_bit(SOCK_ASYNC_WAITDATA, &sk->socket->flags);
	release_sock(sk);

	if (skb_queue_empty(&sk->receive_queue))
		timeo = schedule_timeout(timeo);

	lock_sock(sk);
	clear_bit(SOCK_ASYNC_WAITDATA, &sk->socket->flags);

	remove_wait_queue(sk->sleep, &wait);
	__set_current_state(TASK_RUNNING);
	return timeo;
}

static void tcp_prequeue_process(struct sock *sk)
{
	struct sk_buff *skb;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	net_statistics[smp_processor_id()*2+1].TCPPrequeued += skb_queue_len(&tp->ucopy.prequeue);

	/* RX process wants to run with disabled BHs, though it is not necessary */
	local_bh_disable();
	while ((skb = __skb_dequeue(&tp->ucopy.prequeue)) != NULL)
		sk->backlog_rcv(sk, skb);
	local_bh_enable();

	/* Clear memory counter. */
	tp->ucopy.memory = 0;
}

/*
 *	This routine copies from a sock struct into the user buffer. 
 *
 *	Technical note: in 2.3 we work on _locked_ socket, so that
 *	tricks with *seq access order and skb->users are not required.
 *	Probably, code can be easily improved even more.
 */
 
int tcp_recvmsg(struct sock *sk, struct msghdr *msg,
		int len, int nonblock, int flags, int *addr_len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int copied = 0;
	u32 peek_seq;
	u32 *seq;
	unsigned long used;
	int err;
	int target;		/* Read at least this many bytes */
	long timeo;
	struct task_struct *user_recv = NULL;

	lock_sock(sk);

	TCP_CHECK_TIMER(sk);

	err = -ENOTCONN;
	if (sk->state == TCP_LISTEN)
		goto out;

	timeo = sock_rcvtimeo(sk, nonblock);

	/* Urgent data needs to be handled specially. */
	if (flags & MSG_OOB)
		goto recv_urg;

	seq = &tp->copied_seq;
	if (flags & MSG_PEEK) {
		peek_seq = tp->copied_seq;
		seq = &peek_seq;
	}

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	do {
		struct sk_buff * skb;
		u32 offset;

		/* Are we at urgent data? Stop if we have read anything. */
		if (copied && tp->urg_data && tp->urg_seq == *seq)
			break;

		/* We need to check signals first, to get correct SIGURG
		 * handling. FIXME: Need to check this doesnt impact 1003.1g
		 * and move it down to the bottom of the loop
		 */
		if (signal_pending(current)) {
			if (copied)
				break;
			copied = timeo ? sock_intr_errno(timeo) : -EAGAIN;
			break;
		}

		/* Next get a buffer. */

		skb = skb_peek(&sk->receive_queue);
		do {
			if (!skb)
				break;

			/* Now that we have two receive queues this 
			 * shouldn't happen.
			 */
			if (before(*seq, TCP_SKB_CB(skb)->seq)) {
				printk(KERN_INFO "recvmsg bug: copied %X seq %X\n",
				       *seq, TCP_SKB_CB(skb)->seq);
				break;
			}
			offset = *seq - TCP_SKB_CB(skb)->seq;
			if (skb->h.th->syn)
				offset--;
			if (offset < skb->len)
				goto found_ok_skb;
			if (skb->h.th->fin)
				goto found_fin_ok;
			if (!(flags & MSG_PEEK))
				skb->used = 1;
			skb = skb->next;
		} while (skb != (struct sk_buff *)&sk->receive_queue);

		/* Well, if we have backlog, try to process it now yet. */

		if (copied >= target && sk->backlog.tail == NULL)
			break;

		if (copied) {
			if (sk->err ||
			    sk->state == TCP_CLOSE ||
			    (sk->shutdown & RCV_SHUTDOWN) ||
			    !timeo)
				break;
		} else {
			if (sk->done)
				break;

			if (sk->err) {
				copied = sock_error(sk);
				break;
			}

			if (sk->shutdown & RCV_SHUTDOWN)
				break;

			if (sk->state == TCP_CLOSE) {
				if (!sk->done) {
					/* This occurs when user tries to read
					 * from never connected socket.
					 */
					copied = -ENOTCONN;
					break;
				}
				break;
			}

			if (!timeo) {
				copied = -EAGAIN;
				break;
			}
		}

		cleanup_rbuf(sk, copied);

		if (tp->ucopy.task == user_recv) {
			/* Install new reader */
			if (user_recv == NULL && !(flags&(MSG_TRUNC|MSG_PEEK))) {
				user_recv = current;
				tp->ucopy.task = user_recv;
				tp->ucopy.iov = msg->msg_iov;
			}

			tp->ucopy.len = len;

			BUG_TRAP(tp->copied_seq == tp->rcv_nxt || (flags&(MSG_PEEK|MSG_TRUNC)));

			/* Ugly... If prequeue is not empty, we have to
			 * process it before releasing socket, otherwise
			 * order will be broken at second iteration.
			 * More elegant solution is required!!!
			 *
			 * Look: we have the following (pseudo)queues:
			 *
			 * 1. packets in flight
			 * 2. backlog
			 * 3. prequeue
			 * 4. receive_queue
			 *
			 * Each queue can be processed only if the next ones
			 * are empty. At this point we have empty receive_queue.
			 * But prequeue _can_ be not empty after second iteration,
			 * when we jumped to start of loop because backlog
			 * processing added something to receive_queue.
			 * We cannot release_sock(), because backlog contains
			 * packets arrived _after_ prequeued ones.
			 *
			 * Shortly, algorithm is clear --- to process all
			 * the queues in order. We could make it more directly,
			 * requeueing packets from backlog to prequeue, if
			 * is not empty. It is more elegant, but eats cycles,
			 * unfortunately.
			 */
			if (skb_queue_len(&tp->ucopy.prequeue))
				goto do_prequeue;

			/* __ Set realtime policy in scheduler __ */
		}

		if (copied >= target) {
			/* Do not sleep, just process backlog. */
			release_sock(sk);
			lock_sock(sk);
		} else {
			timeo = tcp_data_wait(sk, timeo);
		}

		if (user_recv) {
			int chunk;

			/* __ Restore normal policy in scheduler __ */

			if ((chunk = len - tp->ucopy.len) != 0) {
				net_statistics[smp_processor_id()*2+1].TCPDirectCopyFromBacklog += chunk;
				len -= chunk;
				copied += chunk;
			}

			if (tp->rcv_nxt == tp->copied_seq &&
			    skb_queue_len(&tp->ucopy.prequeue)) {
do_prequeue:
				tcp_prequeue_process(sk);

				if ((chunk = len - tp->ucopy.len) != 0) {
					net_statistics[smp_processor_id()*2+1].TCPDirectCopyFromPrequeue += chunk;
					len -= chunk;
					copied += chunk;
				}
			}
		}
		continue;

	found_ok_skb:
		/* Ok so how much can we use? */
		used = skb->len - offset;
		if (len < used)
			used = len;

		/* Do we have urgent data here? */
		if (tp->urg_data) {
			u32 urg_offset = tp->urg_seq - *seq;
			if (urg_offset < used) {
				if (!urg_offset) {
					if (!sk->urginline) {
						++*seq;
						offset++;
						used--;
					}
				} else
					used = urg_offset;
			}
		}

		err = 0;
		if (!(flags&MSG_TRUNC)) {
			err = memcpy_toiovec(msg->msg_iov, ((unsigned char *)skb->h.th) + skb->h.th->doff*4 + offset, used);
			if (err) {
				/* Exception. Bailout! */
				if (!copied)
					copied = -EFAULT;
				break;
			}
		}

		*seq += used;
		copied += used;
		len -= used;

		if (after(tp->copied_seq,tp->urg_seq)) {
			tp->urg_data = 0;
			if (skb_queue_len(&tp->out_of_order_queue) == 0
#ifdef TCP_FORMAL_WINDOW
			    && tcp_receive_window(tp)
#endif
			    ) {
				tcp_fast_path_on(tp);
			}
		}
		if (used + offset < skb->len)
			continue;

		/*	Process the FIN. We may also need to handle PSH
		 *	here and make it break out of MSG_WAITALL.
		 */
		if (skb->h.th->fin)
			goto found_fin_ok;
		if (flags & MSG_PEEK)
			continue;
		skb->used = 1;
		tcp_eat_skb(sk, skb);
		continue;

	found_fin_ok:
		++*seq;
		if (flags & MSG_PEEK)
			break;

		/* All is done. */
		skb->used = 1;
		break;
	} while (len > 0);

	if (user_recv) {
		if (skb_queue_len(&tp->ucopy.prequeue)) {
			int chunk;

			tp->ucopy.len = copied > 0 ? len : 0;

			tcp_prequeue_process(sk);

			if (copied > 0 && (chunk = len - tp->ucopy.len) != 0) {
				net_statistics[smp_processor_id()*2+1].TCPDirectCopyFromPrequeue += chunk;
				len -= chunk;
				copied += chunk;
			}
		}

		tp->ucopy.task = NULL;
		tp->ucopy.len = 0;
	}

	/* According to UNIX98, msg_name/msg_namelen are ignored
	 * on connected socket. I was just happy when found this 8) --ANK
	 */

	/* Clean up data we have read: This will do ACK frames. */
	cleanup_rbuf(sk, copied);

	TCP_CHECK_TIMER(sk);
	release_sock(sk);
	return copied;

out:
	TCP_CHECK_TIMER(sk);
	release_sock(sk);
	return err;

recv_urg:
	err = tcp_recv_urg(sk, timeo, msg, len, flags, addr_len);
	goto out;
}

/*
 *	State processing on a close. This implements the state shift for
 *	sending our FIN frame. Note that we only send a FIN for some
 *	states. A shutdown() may have already sent the FIN, or we may be
 *	closed.
 */

static unsigned char new_state[16] = {
  /* current state:        new state:      action:	*/
  /* (Invalid)		*/ TCP_CLOSE,
  /* TCP_ESTABLISHED	*/ TCP_FIN_WAIT1 | TCP_ACTION_FIN,
  /* TCP_SYN_SENT	*/ TCP_CLOSE,
  /* TCP_SYN_RECV	*/ TCP_FIN_WAIT1 | TCP_ACTION_FIN,
  /* TCP_FIN_WAIT1	*/ TCP_FIN_WAIT1,
  /* TCP_FIN_WAIT2	*/ TCP_FIN_WAIT2,
  /* TCP_TIME_WAIT	*/ TCP_CLOSE,
  /* TCP_CLOSE		*/ TCP_CLOSE,
  /* TCP_CLOSE_WAIT	*/ TCP_LAST_ACK  | TCP_ACTION_FIN,
  /* TCP_LAST_ACK	*/ TCP_LAST_ACK,
  /* TCP_LISTEN		*/ TCP_CLOSE,
  /* TCP_CLOSING	*/ TCP_CLOSING,
};

static int tcp_close_state(struct sock *sk)
{
	int next = (int) new_state[sk->state];
	int ns = (next & TCP_STATE_MASK);

	tcp_set_state(sk, ns);

	return (next & TCP_ACTION_FIN);
}

/*
 *	Shutdown the sending side of a connection. Much like close except
 *	that we don't receive shut down or set sk->dead.
 */

void tcp_shutdown(struct sock *sk, int how)
{
	/*	We need to grab some memory, and put together a FIN,
	 *	and then put it into the queue to be sent.
	 *		Tim MacKenzie(tym@dibbler.cs.monash.edu.au) 4 Dec '92.
	 */
	if (!(how & SEND_SHUTDOWN))
		return;

	/* If we've already sent a FIN, or it's a closed state, skip this. */
	if ((1 << sk->state) &
	    (TCPF_ESTABLISHED|TCPF_SYN_SENT|TCPF_SYN_RECV|TCPF_CLOSE_WAIT)) {
		/* Clear out any half completed packets.  FIN if needed. */
		if (tcp_close_state(sk))
			tcp_send_fin(sk);
	}
}


/*
 *	Return 1 if we still have things to send in our buffers.
 */

static inline int closing(struct sock * sk)
{
	return ((1 << sk->state) & (TCPF_FIN_WAIT1|TCPF_CLOSING|TCPF_LAST_ACK));
}

static __inline__ void tcp_kill_sk_queues(struct sock *sk)
{
	/* First the read buffer. */
	__skb_queue_purge(&sk->receive_queue);

	/* Next, the error queue. */
	__skb_queue_purge(&sk->error_queue);

	/* Next, the write queue. */
	BUG_TRAP(skb_queue_empty(&sk->write_queue));

	/* Account for returned memory. */
	tcp_mem_reclaim(sk);

	BUG_TRAP(sk->wmem_queued == 0);
	BUG_TRAP(sk->forward_alloc == 0);

	/* It is _impossible_ for the backlog to contain anything
	 * when we get here.  All user references to this socket
	 * have gone away, only the net layer knows can touch it.
	 */
}

/*
 * At this point, there should be no process reference to this
 * socket, and thus no user references at all.  Therefore we
 * can assume the socket waitqueue is inactive and nobody will
 * try to jump onto it.
 */
void tcp_destroy_sock(struct sock *sk)
{
	BUG_TRAP(sk->state==TCP_CLOSE);
	BUG_TRAP(sk->dead);

	/* It cannot be in hash table! */
	BUG_TRAP(sk->pprev==NULL);

	/* It it has not 0 sk->num, it must be bound */
	BUG_TRAP(!sk->num || sk->prev!=NULL);

#ifdef TCP_DEBUG
	if (sk->zapped) {
		printk("TCP: double destroy sk=%p\n", sk);
		sock_hold(sk);
	}
	sk->zapped = 1;
#endif

	sk->prot->destroy(sk);

	tcp_kill_sk_queues(sk);

#ifdef INET_REFCNT_DEBUG
	if (atomic_read(&sk->refcnt) != 1) {
		printk(KERN_DEBUG "Destruction TCP %p delayed, c=%d\n", sk, atomic_read(&sk->refcnt));
	}
#endif

	atomic_dec(&tcp_orphan_count);
	sock_put(sk);
}

void tcp_close(struct sock *sk, long timeout)
{
	struct sk_buff *skb;
	int data_was_unread = 0;

	lock_sock(sk);
	sk->shutdown = SHUTDOWN_MASK;

	if(sk->state == TCP_LISTEN) {
		tcp_set_state(sk, TCP_CLOSE);

		/* Special case. */
		tcp_listen_stop(sk);

		goto adjudge_to_death;
	}

	/*  We need to flush the recv. buffs.  We do this only on the
	 *  descriptor close, not protocol-sourced closes, because the
	 *  reader process may not have drained the data yet!
	 */
	while((skb=__skb_dequeue(&sk->receive_queue))!=NULL) {
		u32 len = TCP_SKB_CB(skb)->end_seq - TCP_SKB_CB(skb)->seq - skb->h.th->fin;
		data_was_unread += len;
		__kfree_skb(skb);
	}

	tcp_mem_reclaim(sk);

	/* As outlined in draft-ietf-tcpimpl-prob-03.txt, section
	 * 3.10, we send a RST here because data was lost.  To
	 * witness the awful effects of the old behavior of always
	 * doing a FIN, run an older 2.1.x kernel or 2.0.x, start
	 * a bulk GET in an FTP client, suspend the process, wait
	 * for the client to advertise a zero window, then kill -9
	 * the FTP client, wheee...  Note: timeout is always zero
	 * in such a case.
	 */
	if(data_was_unread != 0) {
		/* Unread data was tossed, zap the connection. */
		NET_INC_STATS_USER(TCPAbortOnClose);
		tcp_set_state(sk, TCP_CLOSE);
		tcp_send_active_reset(sk, GFP_KERNEL);
	} else if (sk->linger && sk->lingertime==0) {
		/* Check zero linger _after_ checking for unread data. */
		sk->prot->disconnect(sk, 0);
		NET_INC_STATS_USER(TCPAbortOnData);
	} else if (tcp_close_state(sk)) {
		/* We FIN if the application ate all the data before
		 * zapping the connection.
		 */

		/* RED-PEN. Formally speaking, we have broken TCP state
		 * machine. State transitions:
		 *
		 * TCP_ESTABLISHED -> TCP_FIN_WAIT1
		 * TCP_SYN_RECV	-> TCP_FIN_WAIT1 (forget it, it's impossible)
		 * TCP_CLOSE_WAIT -> TCP_LAST_ACK
		 *
		 * are legal only when FIN has been sent (i.e. in window),
		 * rather than queued out of window. Purists blame.
		 *
		 * F.e. "RFC state" is ESTABLISHED,
		 * if Linux state is FIN-WAIT-1, but FIN is still not sent.
		 *
		 * The visible declinations are that sometimes
		 * we enter time-wait state, when it is not required really
		 * (harmless), do not send active resets, when they are
		 * required by specs (TCP_ESTABLISHED, TCP_CLOSE_WAIT, when
		 * they look as CLOSING or LAST_ACK for Linux)
		 * Probably, I missed some more holelets.
		 * 						--ANK
		 */
		tcp_send_fin(sk);
	}

	if (timeout) {
		struct task_struct *tsk = current;
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue(sk->sleep, &wait);

		do {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!closing(sk))
				break;
			release_sock(sk);
			timeout = schedule_timeout(timeout);
			lock_sock(sk);
		} while (!signal_pending(tsk) && timeout);

		tsk->state = TASK_RUNNING;
		remove_wait_queue(sk->sleep, &wait);
	}

adjudge_to_death:
	/* It is the last release_sock in its life. It will remove backlog. */
	release_sock(sk);


	/* Now socket is owned by kernel and we acquire BH lock
	   to finish close. No need to check for user refs.
	 */
	local_bh_disable();
	bh_lock_sock(sk);
	BUG_TRAP(sk->lock.users==0);

	sock_hold(sk);
	sock_orphan(sk);

	/*	This is a (useful) BSD violating of the RFC. There is a
	 *	problem with TCP as specified in that the other end could
	 *	keep a socket open forever with no application left this end.
	 *	We use a 3 minute timeout (about the same as BSD) then kill
	 *	our end. If they send after that then tough - BUT: long enough
	 *	that we won't make the old 4*rto = almost no time - whoops
	 *	reset mistake.
	 *
	 *	Nope, it was not mistake. It is really desired behaviour
	 *	f.e. on http servers, when such sockets are useless, but
	 *	consume significant resources. Let's do it with special
	 *	linger2	option.					--ANK
	 */

	if (sk->state == TCP_FIN_WAIT2) {
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
		if (tp->linger2 < 0) {
			tcp_set_state(sk, TCP_CLOSE);
			tcp_send_active_reset(sk, GFP_ATOMIC);
			NET_INC_STATS_BH(TCPAbortOnLinger);
		} else {
			int tmo = tcp_fin_time(tp);

			if (tmo > TCP_TIMEWAIT_LEN) {
				tcp_reset_keepalive_timer(sk, tcp_fin_time(tp));
			} else {
				atomic_inc(&tcp_orphan_count);
				tcp_time_wait(sk, TCP_FIN_WAIT2, tmo);
				goto out;
			}
		}
	}
	if (sk->state != TCP_CLOSE) {
		tcp_mem_reclaim(sk);
		if (atomic_read(&tcp_orphan_count) > sysctl_tcp_max_orphans ||
		    (sk->wmem_queued > SOCK_MIN_SNDBUF &&
		     atomic_read(&tcp_memory_allocated) > sysctl_tcp_mem[2])) {
			if (net_ratelimit())
				printk(KERN_INFO "TCP: too many of orphaned sockets\n");
			tcp_set_state(sk, TCP_CLOSE);
			tcp_send_active_reset(sk, GFP_ATOMIC);
			NET_INC_STATS_BH(TCPAbortOnMemory);
		}
	}
	atomic_inc(&tcp_orphan_count);

	if (sk->state == TCP_CLOSE)
		tcp_destroy_sock(sk);
	/* Otherwise, socket is reprieved until protocol close. */

out:
	bh_unlock_sock(sk);
	local_bh_enable();
	sock_put(sk);
}

/* These states need RST on ABORT according to RFC793 */

extern __inline__ int tcp_need_reset(int state)
{
	return ((1 << state) &
	       	(TCPF_ESTABLISHED|TCPF_CLOSE_WAIT|TCPF_FIN_WAIT1|
		 TCPF_FIN_WAIT2|TCPF_SYN_RECV));
}

int tcp_disconnect(struct sock *sk, int flags)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	int old_state;
	int err = 0;

	old_state = sk->state;
	if (old_state != TCP_CLOSE)
		tcp_set_state(sk, TCP_CLOSE);

	/* ABORT function of RFC793 */
	if (old_state == TCP_LISTEN) {
		tcp_listen_stop(sk);
	} else if (tcp_need_reset(old_state) ||
		   (tp->snd_nxt != tp->write_seq &&
		    (1<<old_state)&(TCPF_CLOSING|TCPF_LAST_ACK))) {
		/* The last check adjusts for discrepance of Linux wrt. RFC
		 * states
		 */
		tcp_send_active_reset(sk, gfp_any());
		sk->err = ECONNRESET;
	} else if (old_state == TCP_SYN_SENT)
		sk->err = ECONNRESET;

	tcp_clear_xmit_timers(sk);
	__skb_queue_purge(&sk->receive_queue);
  	tcp_writequeue_purge(sk);
  	__skb_queue_purge(&tp->out_of_order_queue);

	sk->dport = 0;

	if (!(sk->userlocks&SOCK_BINDADDR_LOCK)) {
		sk->rcv_saddr = 0;
		sk->saddr = 0;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		memset(&sk->net_pinfo.af_inet6.saddr, 0, 16);
		memset(&sk->net_pinfo.af_inet6.rcv_saddr, 0, 16);
#endif
	}

	sk->shutdown = 0;
	sk->done = 0;
	tp->srtt = 0;
	if ((tp->write_seq += tp->max_window+2) == 0)
		tp->write_seq = 1;
	tp->backoff = 0;
	tp->snd_cwnd = 2;
	tp->probes_out = 0;
	tp->packets_out = 0;
	tp->snd_ssthresh = 0x7fffffff;
	tp->snd_cwnd_cnt = 0;
	tp->ca_state = TCP_CA_Open;
	tcp_clear_retrans(tp);
	tcp_delack_init(tp);
	tp->send_head = NULL;
	tp->saw_tstamp = 0;
	tcp_sack_reset(tp);
	__sk_dst_reset(sk);

	BUG_TRAP(!sk->num || sk->prev);

	sk->error_report(sk);
	return err;
}

/*
 *	Wait for an incoming connection, avoid race
 *	conditions. This must be called with the socket locked.
 */
static int wait_for_connect(struct sock * sk, long timeo)
{
	DECLARE_WAITQUEUE(wait, current);
	int err;

	/*
	 * True wake-one mechanism for incoming connections: only
	 * one process gets woken up, not the 'whole herd'.
	 * Since we do not 'race & poll' for established sockets
	 * anymore, the common case will execute the loop only once.
	 *
	 * Subtle issue: "add_wait_queue_exclusive()" will be added
	 * after any current non-exclusive waiters, and we know that
	 * it will always _stay_ after any new non-exclusive waiters
	 * because all non-exclusive waiters are added at the
	 * beginning of the wait-queue. As such, it's ok to "drop"
	 * our exclusiveness temporarily when we get woken up without
	 * having to remove and re-insert us on the wait queue.
	 */
	add_wait_queue_exclusive(sk->sleep, &wait);
	for (;;) {
		current->state = TASK_INTERRUPTIBLE;
		release_sock(sk);
		if (sk->tp_pinfo.af_tcp.accept_queue == NULL)
			timeo = schedule_timeout(timeo);
		lock_sock(sk);
		err = 0;
		if (sk->tp_pinfo.af_tcp.accept_queue)
			break;
		err = -EINVAL;
		if (sk->state != TCP_LISTEN)
			break;
		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			break;
		err = -EAGAIN;
		if (!timeo)
			break;
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(sk->sleep, &wait);
	return err;
}

/*
 *	This will accept the next outstanding connection.
 */

struct sock *tcp_accept(struct sock *sk, int flags, int *err)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct open_request *req;
	struct sock *newsk;
	int error;

	lock_sock(sk); 

	/* We need to make sure that this socket is listening,
	 * and that it has something pending.
	 */
	error = -EINVAL;
	if (sk->state != TCP_LISTEN)
		goto out;

	/* Find already established connection */
	if (!tp->accept_queue) {
		long timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

		/* If this is a non blocking socket don't sleep */
		error = -EAGAIN;
		if (!timeo)
			goto out;

		error = wait_for_connect(sk, timeo);
		if (error)
			goto out;
	}

	req = tp->accept_queue;
	if ((tp->accept_queue = req->dl_next) == NULL)
		tp->accept_queue_tail = NULL;

 	newsk = req->sk;
	tcp_acceptq_removed(sk);
	tcp_openreq_fastfree(req);
	BUG_TRAP(newsk->state != TCP_SYN_RECV);
	release_sock(sk);
	return newsk;

out:
	release_sock(sk);
	*err = error; 
	return NULL;
}

/*
 *	Socket option code for TCP. 
 */
  
int tcp_setsockopt(struct sock *sk, int level, int optname, char *optval, 
		   int optlen)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int val;
	int err = 0;

	if (level != SOL_TCP)
		return tp->af_specific->setsockopt(sk, level, optname, 
						   optval, optlen);

	if(optlen<sizeof(int))
		return -EINVAL;

	if (get_user(val, (int *)optval))
		return -EFAULT;

	lock_sock(sk);

	switch(optname) {
	case TCP_MAXSEG:
		/* values greater than interface MTU won't take effect.  however at
		 * the point when this call is done we typically don't yet know
		 * which interface is going to be used
		 */
		if(val < 8 || val > MAX_TCP_WINDOW) {
			err = -EINVAL;
			break;
		}
		tp->user_mss = val;
		break;

	case TCP_NODELAY:
		/* You cannot try to use this and TCP_CORK in
		 * tandem, so let the user know.
		 */
		if (tp->nonagle == 2) {
			err = -EINVAL;
			break;
		}
		tp->nonagle = (val == 0) ? 0 : 1;
		if (val)
			tcp_push_pending_frames(sk, tp);
		break;

	case TCP_CORK:
		/* When set indicates to always queue non-full frames.
		 * Later the user clears this option and we transmit
		 * any pending partial frames in the queue.  This is
		 * meant to be used alongside sendfile() to get properly
		 * filled frames when the user (for example) must write
		 * out headers with a write() call first and then use
		 * sendfile to send out the data parts.
		 *
		 * You cannot try to use TCP_NODELAY and this mechanism
		 * at the same time, so let the user know.
		 */
		if (tp->nonagle == 1) {
			err = -EINVAL;
			break;
		}
		if (val != 0) {
			tp->nonagle = 2;
		} else {
			tp->nonagle = 0;

			tcp_push_pending_frames(sk, tp);
		}
		break;
		
	case TCP_KEEPIDLE:
		if (val < 1 || val > MAX_TCP_KEEPIDLE)
			err = -EINVAL;
		else {
			tp->keepalive_time = val * HZ;
			if (sk->keepopen && !((1<<sk->state)&(TCPF_CLOSE|TCPF_LISTEN))) {
				__u32 elapsed = tcp_time_stamp - tp->rcv_tstamp;
				if (tp->keepalive_time > elapsed)
					elapsed = tp->keepalive_time - elapsed;
				else
					elapsed = 0;
				tcp_reset_keepalive_timer(sk, elapsed);
			}
		}
		break;
	case TCP_KEEPINTVL:
		if (val < 1 || val > MAX_TCP_KEEPINTVL)
			err = -EINVAL;
		else
			tp->keepalive_intvl = val * HZ;
		break;
	case TCP_KEEPCNT:
		if (val < 1 || val > MAX_TCP_KEEPCNT)
			err = -EINVAL;
		else
			tp->keepalive_probes = val;
		break;
	case TCP_SYNCNT:
		if (val < 1 || val > MAX_TCP_SYNCNT)
			err = -EINVAL;
		else
			tp->syn_retries = val;
		break;

	case TCP_LINGER2:
		if (val < 0)
			tp->linger2 = -1;
		else if (val > sysctl_tcp_fin_timeout/HZ)
			tp->linger2 = 0;
		else
			tp->linger2 = val*HZ;
		break;

	case TCP_DEFER_ACCEPT:
		tp->defer_accept = 0;
		if (val > 0) {
			/* Translate value in seconds to number of retransmits */
			while (val > ((TCP_TIMEOUT_INIT/HZ)<<tp->defer_accept))
				tp->defer_accept++;
			tp->defer_accept++;
		}
		break;

	case TCP_WINDOW_CLAMP:
		if (val==0) {
			if (sk->state != TCP_CLOSE) {
				err = -EINVAL;
				break;
			}
			tp->window_clamp = 0;
		} else {
			tp->window_clamp = val<SOCK_MIN_RCVBUF/2 ?
				SOCK_MIN_RCVBUF/2 : val;
		}
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	};
	release_sock(sk);
	return err;
}

int tcp_getsockopt(struct sock *sk, int level, int optname, char *optval,
		   int *optlen)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int val, len;

	if(level != SOL_TCP)
		return tp->af_specific->getsockopt(sk, level, optname,
						   optval, optlen);

	if(get_user(len,optlen))
		return -EFAULT;

	len = min(len, sizeof(int));

	switch(optname) {
	case TCP_MAXSEG:
		val = tp->mss_cache;
		if (val == 0 && ((1<<sk->state)&(TCPF_CLOSE|TCPF_LISTEN)))
			val = tp->user_mss;
		break;
	case TCP_NODELAY:
		val = (tp->nonagle == 1);
		break;
	case TCP_CORK:
		val = (tp->nonagle == 2);
		break;
	case TCP_KEEPIDLE:
		val = (tp->keepalive_time ? : sysctl_tcp_keepalive_time)/HZ;
		break;
	case TCP_KEEPINTVL:
		val = (tp->keepalive_intvl ? : sysctl_tcp_keepalive_intvl)/HZ;
		break;
	case TCP_KEEPCNT:
		val = tp->keepalive_probes ? : sysctl_tcp_keepalive_probes;
		break;
	case TCP_SYNCNT:
		val = tp->syn_retries ? : sysctl_tcp_syn_retries;
		break;
	case TCP_LINGER2:
		val = tp->linger2;
		if (val > 0)
			val = (val ? : sysctl_tcp_fin_timeout)/HZ;
		break;
	case TCP_DEFER_ACCEPT:
		val = tp->defer_accept == 0 ? 0 : (TCP_TIMEOUT_INIT<<(tp->defer_accept-1));
		break;
	case TCP_WINDOW_CLAMP:
		val = tp->window_clamp;
		break;
	case TCP_INFO:
	{
		struct tcp_info info;
		u32 now = tcp_time_stamp;

		if(get_user(len,optlen))
			return -EFAULT;
		info.tcpi_state = sk->state;
		info.tcpi_ca_state = tp->ca_state;
		info.tcpi_retransmits = tp->retransmits;
		info.tcpi_probes = tp->probes_out;
		info.tcpi_backoff = tp->backoff;
		info.tcpi_options = 0;
		if (tp->tstamp_ok)
			info.tcpi_options |= TCPI_OPT_TIMESTAMPS;
		if (tp->sack_ok)
			info.tcpi_options |= TCPI_OPT_SACK;
		if (tp->wscale_ok) {
			info.tcpi_options |= TCPI_OPT_WSCALE;
			info.tcpi_snd_wscale = tp->snd_wscale;
			info.tcpi_rcv_wscale = tp->rcv_wscale;
		} else {
			info.tcpi_snd_wscale = 0;
			info.tcpi_rcv_wscale = 0;
		}
#ifdef CONFIG_INET_ECN
		if (tp->ecn_flags&TCP_ECN_OK)
			info.tcpi_options |= TCPI_OPT_ECN;
#endif

		info.tcpi_rto = (1000000*tp->rto)/HZ;
		info.tcpi_ato = (1000000*tp->ack.ato)/HZ;
		info.tcpi_snd_mss = tp->mss_cache;
		info.tcpi_rcv_mss = tp->ack.rcv_mss;

		info.tcpi_unacked = tp->packets_out;
		info.tcpi_sacked = tp->sacked_out;
		info.tcpi_lost = tp->lost_out;
		info.tcpi_retrans = tp->retrans_out;
		info.tcpi_fackets = tp->fackets_out;

		info.tcpi_last_data_sent = ((now - tp->lsndtime)*1000)/HZ;
		info.tcpi_last_ack_sent = 0;
		info.tcpi_last_data_recv = ((now - tp->ack.lrcvtime)*1000)/HZ;
		info.tcpi_last_ack_recv = ((now - tp->rcv_tstamp)*1000)/HZ;

		info.tcpi_pmtu = tp->pmtu_cookie;
		info.tcpi_rcv_ssthresh = tp->rcv_ssthresh;
		info.tcpi_rtt = ((1000000*tp->srtt)/HZ)>>3;
		info.tcpi_rttvar = ((1000000*tp->mdev)/HZ)>>2;
		info.tcpi_snd_ssthresh = tp->snd_ssthresh;
		info.tcpi_snd_cwnd = tp->snd_cwnd;
		info.tcpi_advmss = tp->advmss;
		info.tcpi_reordering = tp->reordering;

		len = min(len, sizeof(info));
		if(put_user(len, optlen))
			return -EFAULT;
		if(copy_to_user(optval, &info,len))
			return -EFAULT;
		return 0;
	}
	default:
		return -ENOPROTOOPT;
	};

  	if(put_user(len, optlen))
  		return -EFAULT;
	if(copy_to_user(optval, &val,len))
		return -EFAULT;
  	return 0;
}


extern void __skb_cb_too_small_for_tcp(int, int);

void __init tcp_init(void)
{
	struct sk_buff *skb = NULL;
	unsigned long goal;
	int order, i;

	if(sizeof(struct tcp_skb_cb) > sizeof(skb->cb))
		__skb_cb_too_small_for_tcp(sizeof(struct tcp_skb_cb),
					   sizeof(skb->cb));

	tcp_openreq_cachep = kmem_cache_create("tcp_open_request",
						   sizeof(struct open_request),
					       0, SLAB_HWCACHE_ALIGN,
					       NULL, NULL);
	if(!tcp_openreq_cachep)
		panic("tcp_init: Cannot alloc open_request cache.");

	tcp_bucket_cachep = kmem_cache_create("tcp_bind_bucket",
					      sizeof(struct tcp_bind_bucket),
					      0, SLAB_HWCACHE_ALIGN,
					      NULL, NULL);
	if(!tcp_bucket_cachep)
		panic("tcp_init: Cannot alloc tcp_bind_bucket cache.");

	tcp_timewait_cachep = kmem_cache_create("tcp_tw_bucket",
						sizeof(struct tcp_tw_bucket),
						0, SLAB_HWCACHE_ALIGN,
						NULL, NULL);
	if(!tcp_timewait_cachep)
		panic("tcp_init: Cannot alloc tcp_tw_bucket cache.");

	/* Size and allocate the main established and bind bucket
	 * hash tables.
	 *
	 * The methodology is similar to that of the buffer cache.
	 */
	goal = num_physpages >> (23 - PAGE_SHIFT);

	for(order = 0; (1UL << order) < goal; order++)
		;
	do {
		tcp_ehash_size = (1UL << order) * PAGE_SIZE /
			sizeof(struct tcp_ehash_bucket);
		tcp_ehash_size >>= 1;
		while (tcp_ehash_size & (tcp_ehash_size-1))
			tcp_ehash_size--;
		tcp_ehash = (struct tcp_ehash_bucket *)
			__get_free_pages(GFP_ATOMIC, order);
	} while (tcp_ehash == NULL && --order > 0);

	if (!tcp_ehash)
		panic("Failed to allocate TCP established hash table\n");
	for (i = 0; i < (tcp_ehash_size<<1); i++) {
		tcp_ehash[i].lock = RW_LOCK_UNLOCKED;
		tcp_ehash[i].chain = NULL;
	}

	do {
		tcp_bhash_size = (1UL << order) * PAGE_SIZE /
			sizeof(struct tcp_bind_hashbucket);
		if ((tcp_bhash_size > (64 * 1024)) && order > 0)
			continue;
		tcp_bhash = (struct tcp_bind_hashbucket *)
			__get_free_pages(GFP_ATOMIC, order);
	} while (tcp_bhash == NULL && --order >= 0);

	if (!tcp_bhash)
		panic("Failed to allocate TCP bind hash table\n");
	for (i = 0; i < tcp_bhash_size; i++) {
		tcp_bhash[i].lock = SPIN_LOCK_UNLOCKED;
		tcp_bhash[i].chain = NULL;
	}

	/* Try to be a bit smarter and adjust defaults depending
	 * on available memory.
	 */
	if (order > 4) {
		sysctl_local_port_range[0] = 32768;
		sysctl_local_port_range[1] = 61000;
		sysctl_tcp_max_tw_buckets = 180000;
		sysctl_tcp_max_orphans = 4096<<(order-4);
		sysctl_max_syn_backlog = 1024;
	} else if (order < 3) {
		sysctl_local_port_range[0] = 1024*(3-order);
		sysctl_tcp_max_tw_buckets >>= (3-order);
		sysctl_tcp_max_orphans >>= (3-order);
		sysctl_max_syn_backlog = 128;
	}
	tcp_port_rover = sysctl_local_port_range[0] - 1;

	sysctl_tcp_mem[0] = 64<<order;
	sysctl_tcp_mem[1] = 200<<order;
	sysctl_tcp_mem[2] = 256<<order;
	if (sysctl_tcp_mem[2] - sysctl_tcp_mem[1] > 512)
		sysctl_tcp_mem[1] = sysctl_tcp_mem[2] - 512;
	if (sysctl_tcp_mem[1] - sysctl_tcp_mem[0] > 512)
		sysctl_tcp_mem[0] = sysctl_tcp_mem[1] - 512;

	if (order < 3) {
		sysctl_tcp_wmem[2] = 64*1024;
		sysctl_tcp_rmem[0] = PAGE_SIZE;
		sysctl_tcp_rmem[1] = 43689;
		sysctl_tcp_rmem[2] = 2*43689;
	}

	printk("TCP: Hash tables configured (established %d bind %d)\n",
	       tcp_ehash_size<<1, tcp_bhash_size);
}
