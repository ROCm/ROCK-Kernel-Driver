/*
 *	An async IO implementation for Linux
 *	Written by Benjamin LaHaise <bcrl@redhat.com>
 *
 *	Implements an efficient asynchronous io interface.
 *
 *	Copyright 2000, 2001, 2002 Red Hat, Inc.  All Rights Reserved.
 *
 *	See ../COPYING for licensing terms.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/aio_abi.h>

/* io_setup:
 *	Create an aio_context capable of receiving at least nr_events.
 *	ctxp must not point to an aio_context that already exists, and
 *	must be initialized to 0 prior to the call.  On successful
 *	creation of the aio_context, *ctxp is filled in with the resulting 
 *	handle.  May fail with -EINVAL if *ctxp is not initialized,
 *	if the specified nr_events exceeds internal limits.  May fail 
 *	with -EAGAIN if the specified nr_events exceeds the user's limit 
 *	of available events.  May fail with -ENOMEM if insufficient kernel
 *	resources are available.  May fail with -EFAULT if an invalid
 *	pointer is passed for ctxp.  Will fail with -ENOSYS if not
 *	implemented.
 */
asmlinkage long sys_io_setup(unsigned nr_events, aio_context_t *ctxp)
{
	return -ENOSYS;
}

/* io_destroy:
 *	Destroy the aio_context specified.  May cancel any outstanding 
 *	AIOs and block on completion.  Will fail with -ENOSYS if not
 *	implemented.  May fail with -EFAULT if the context pointed to
 *	is invalid.
 */
asmlinkage long sys_io_destroy(aio_context_t ctx)
{
	return -ENOSYS;
}

/* io_submit:
 *	Queue the nr iocbs pointed to by iocbpp for processing.  Returns
 *	the number of iocbs queued.  May return -EINVAL if the aio_context
 *	specified by ctx_id is invalid, if nr is < 0, if the iocb at
 *	*iocbpp[0] is not properly initialized, if the operation specified
 *	is invalid for the file descriptor in the iocb.  May fail with
 *	-EFAULT if any of the data structures point to invalid data.  May
 *	fail with -EBADF if the file descriptor specified in the first
 *	iocb is invalid.  May fail with -EAGAIN if insufficient resources
 *	are available to queue any iocbs.  Will return 0 if nr is 0.  Will
 *	fail with -ENOSYS if not implemented.
 */
asmlinkage long sys_io_submit(aio_context_t ctx_id, long nr,
			      struct iocb **iocbpp)
{
	return -ENOSYS;
}

/* io_cancel:
 *	Attempts to cancel an iocb previously passed to io_submit.  If
 *	the operation is successfully cancelled, the resulting event is
 *	copied into the memory pointed to by result without being placed
 *	into the completion queue and 0 is returned.  May fail with
 *	-EFAULT if any of the data structures pointed to are invalid.
 *	May fail with -EINVAL if aio_context specified by ctx_id is
 *	invalid.  May fail with -EAGAIN if the iocb specified was not
 *	cancelled.  Will fail with -ENOSYS if not implemented.
 */
asmlinkage long sys_io_cancel(aio_context_t ctx_id, struct iocb *iocb,
			      struct io_event *result)
{
	return -ENOSYS;
}

/* io_getevents:
 *	Attempts to read at least min_nr events and up to nr events from
 *	the completion queue for the aio_context specified by ctx_id.  May
 *	fail with -EINVAL if ctx_id is invalid, if min_nr is out of range,
 *	if nr is out of range, if when is out of range.  May fail with
 *	-EFAULT if any of the memory specified to is invalid.  May return
 *	0 or < min_nr if no events are available and the timeout specified
 *	by when	has elapsed, where when == NULL specifies an infinite
 *	timeout.  Note that the timeout pointed to by when is relative and
 *	will be updated if not NULL and the operation blocks.  Will fail
 *	with -ENOSYS if not implemented.
 */
asmlinkage long sys_io_getevents(aio_context_t ctx_id,
				 long min_nr,
				 long nr,
				 struct io_event *events,
				 struct timespec *when)
{
	return -ENOSYS;
}

