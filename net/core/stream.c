/*
 *     SUCS NET3:
 *
 *     Generic stream handling routines. These are generic for most
 *     protocols. Even IP. Tonight 8-).
 *     This is used because TCP, LLC (others too) layer all have mostly
 *     identical sendmsg() and recvmsg() code.
 *     So we (will) share it here.
 *
 *     Authors:        Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *                     (from old tcp.c code)
 *                     Alan Cox <alan@redhat.com> (Borrowed comments 8-))
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/signal.h>
#include <linux/wait.h>
#include <net/sock.h>

/**
 * sk_stream_write_space - stream socket write_space callback.
 * sk - socket
 *
 * FIXME: write proper description
 */
void sk_stream_write_space(struct sock *sk)
{
	struct socket *sock = sk->sk_socket;

	if (sk_stream_wspace(sk) >= sk_stream_min_wspace(sk) && sock) {
		clear_bit(SOCK_NOSPACE, &sock->flags);

		if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
			wake_up_interruptible(sk->sk_sleep);
		if (sock->fasync_list && !(sk->sk_shutdown & SEND_SHUTDOWN))
			sock_wake_async(sock, 2, POLL_OUT);
	}
}

EXPORT_SYMBOL(sk_stream_write_space);
