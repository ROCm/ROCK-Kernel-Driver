/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2006 Silicon Graphics, Inc.  All Rights Reserved.
 */


/*
 * Cross Partition Communication (XPC) kdb support.
 *
 *	This provides kdb commands for debugging XPC.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/xpc.h>


MODULE_AUTHOR("SGI");
MODULE_DESCRIPTION("Debug XPC information");
MODULE_LICENSE("GPL");


static int
kdbm_xpc_down(int argc, const char **argv)
{
	if (xpc_rsvd_page == NULL) {
		kdb_printf("Reserved Page has not been initialized.\n");

	} else if (xpc_kdebug_force_disengage()) {
		kdb_printf("Unable to force XPC disengage.\n");
	}
	return 0;
}


static char *
kdbm_xpc_get_ascii_reason_code(enum xpc_retval reason)
{
	switch (reason) {
	case xpcSuccess:		return "";
	case xpcNotConnected:		return "xpcNotConnected";
	case xpcConnected:		return "xpcConnected";
	case xpcRETIRED1:		return "xpcRETIRED1";
	case xpcMsgReceived:		return "xpcMsgReceived";
	case xpcMsgDelivered:		return "xpcMsgDelivered";
	case xpcRETIRED2:		return "xpcRETIRED2";
	case xpcNoWait:			return "xpcNoWait";
	case xpcRetry:			return "xpcRetry";
	case xpcTimeout:		return "xpcTimeout";
	case xpcInterrupted:		return "xpcInterrupted";
	case xpcUnequalMsgSizes:	return "xpcUnequalMsgSizes";
	case xpcInvalidAddress:		return "xpcInvalidAddress";
	case xpcNoMemory:		return "xpcNoMemory";
	case xpcLackOfResources:	return "xpcLackOfResources";
	case xpcUnregistered:		return "xpcUnregistered";
	case xpcAlreadyRegistered:	return "xpcAlreadyRegistered";
	case xpcPartitionDown:		return "xpcPartitionDown";
	case xpcNotLoaded:		return "xpcNotLoaded";
	case xpcUnloading:		return "xpcUnloading";
	case xpcBadMagic:		return "xpcBadMagic";
	case xpcReactivating:		return "xpcReactivating";
	case xpcUnregistering:		return "xpcUnregistering";
	case xpcOtherUnregistering:	return "xpcOtherUnregistering";
	case xpcCloneKThread:		return "xpcCloneKThread";
	case xpcCloneKThreadFailed:	return "xpcCloneKThreadFailed";
	case xpcNoHeartbeat:		return "xpcNoHeartbeat";
	case xpcPioReadError:		return "xpcPioReadError";
	case xpcPhysAddrRegFailed:	return "xpcPhysAddrRegFailed";
	case xpcBteDirectoryError:	return "xpcBteDirectoryError";
	case xpcBtePoisonError:		return "xpcBtePoisonError";
	case xpcBteWriteError:		return "xpcBteWriteError";
	case xpcBteAccessError:		return "xpcBteAccessError";
	case xpcBtePWriteError:		return "xpcBtePWriteError";
	case xpcBtePReadError:		return "xpcBtePReadError";
	case xpcBteTimeOutError:	return "xpcBteTimeOutError";
	case xpcBteXtalkError:		return "xpcBteXtalkError";
	case xpcBteNotAvailable:	return "xpcBteNotAvailable";
	case xpcBteUnmappedError:	return "xpcBteUnmappedError";
	case xpcBadVersion:		return "xpcBadVersion";
	case xpcVarsNotSet:		return "xpcVarsNotSet";
	case xpcNoRsvdPageAddr:		return "xpcNoRsvdPageAddr";
	case xpcInvalidPartid:		return "xpcInvalidPartid";
	case xpcLocalPartid:		return "xpcLocalPartid";
	case xpcOtherGoingDown:		return "xpcOtherGoingDown";
	case xpcSystemGoingDown:	return "xpcSystemGoingDown";
	case xpcSystemHalt:		return "xpcSystemHalt";
	case xpcSystemReboot:		return "xpcSystemReboot";
	case xpcSystemPoweroff:		return "xpcSystemPoweroff";
	case xpcDisconnecting:		return "xpcDisconnecting";
	case xpcOpenCloseError:		return "xpcOpenCloseError";
	case xpcUnknownReason:		return "xpcUnknownReason";
	default:			return "undefined reason code";
	}
}


/*
 * Display the reserved page used by XPC.
 *
 *	xpcrp
 */
static int
kdbm_xpc_rsvd_page(int argc, const char **argv)
{
	struct xpc_rsvd_page *rp = (struct xpc_rsvd_page *) xpc_rsvd_page;


	if (argc > 0) {
		return KDB_ARGCOUNT;
	}

	if (rp == NULL) {
		kdb_printf("Reserved Page has not been initialized.\n");
		return 0;
	}

	kdb_printf("struct xpc_rsvd_page @ (0x%p):\n", (void *) rp);
	kdb_printf("\tSAL_signature=0x%lx\n", rp->SAL_signature);
	kdb_printf("\tSAL_version=0x%lx\n", rp->SAL_version);
	kdb_printf("\tpartid=%d\n", rp->partid);
	kdb_printf("\tversion=0x%x %d.%d\n", rp->version,
				XPC_VERSION_MAJOR(rp->version),
				XPC_VERSION_MINOR(rp->version));
	kdb_printf("\tvars_pa=0x%lx\n", rp->vars_pa);
	kdb_printf("\tstamp=0x%lx:0x%lx\n",
				rp->stamp.tv_sec, rp->stamp.tv_nsec);
	kdb_printf("\tnasids_size=%ld\n", rp->nasids_size);

	return 0;
}


static void
kdbm_xpc_print_vars_part(struct xpc_vars_part *vars_part, partid_t partid)
{
	kdb_printf("struct xpc_vars_part @ (0x%p) [partid=%d]:\n",
						(void *) vars_part, partid);
	kdb_printf("\tmagic=0x%lx ", vars_part->magic);
	if (vars_part->magic != 0) {
		kdb_printf("%s", (char *) &vars_part->magic);
	}
	kdb_printf("\n");
	kdb_printf("\tGPs_pa=0x%lx\n", vars_part->GPs_pa);
	kdb_printf("\topenclose_args_pa=0x%lx\n",
				vars_part->openclose_args_pa);
	kdb_printf("\tIPI_amo_pa=0x%lx\n", vars_part->IPI_amo_pa);
	kdb_printf("\tIPI_nasid=0x%x\n", vars_part->IPI_nasid);
	kdb_printf("\tIPI_phys_cpuid=0x%x\n", vars_part->IPI_phys_cpuid);
	kdb_printf("\tnchannels=%d\n", vars_part->nchannels);
}


/*
 * Display XPC variables.
 *
 *      xpcvars [ <partid> ]
 *
 *	    no partid - displays xpc_vars structure
 *	    partid=0  - displays all initialized xpc_vars_part structures
 *	    partid=i  - displays xpc_vars_part structure for specified
 *			partition, if initialized
 */
static int
kdbm_xpc_variables(int argc, const char **argv)
{
	int ret;
	unsigned long ulong_partid;
	partid_t partid;
	struct xpc_vars_part *vars_part;


	if (xpc_rsvd_page == NULL) {
		kdb_printf("Reserved Page has not been initialized.\n");
		return 0;
	}
	DBUG_ON(xpc_vars == NULL);

	if (argc == 0) {

		/* just display the xpc_vars structure */

		kdb_printf("struct xpc_vars @ (0x%p):\n", (void *) xpc_vars);
		kdb_printf("\tversion=0x%x %d.%d\n", xpc_vars->version,
				XPC_VERSION_MAJOR(xpc_vars->version),
				XPC_VERSION_MINOR(xpc_vars->version));
		kdb_printf("\theartbeat=%ld\n", xpc_vars->heartbeat);
		kdb_printf("\theartbeating_to_mask=0x%lx",
					xpc_vars->heartbeating_to_mask);
		for (partid = 1; partid < XP_MAX_PARTITIONS; partid++) {
			if (xpc_hb_allowed(partid, xpc_vars)) {
				kdb_printf(" %d", partid);
			}
		}
		kdb_printf("\n");
		kdb_printf("\theartbeat_offline=0x%lx\n",
					xpc_vars->heartbeat_offline);
		kdb_printf("\tact_nasid=0x%x\n", xpc_vars->act_nasid);
		kdb_printf("\tact_phys_cpuid=0x%x\n",
					xpc_vars->act_phys_cpuid);
		kdb_printf("\tvars_part_pa=0x%lx\n", xpc_vars->vars_part_pa);
		kdb_printf("\tamos_page_pa=0x%lx\n", xpc_vars->amos_page_pa);
		kdb_printf("\tamos_page=0x%p\n", (void *) xpc_vars->amos_page);
		return 0;

	} else if (argc != 1) {
		return KDB_ARGCOUNT;
	}

	ret = kdbgetularg(argv[1], (unsigned long *) &ulong_partid);
	if (ret) {
		return ret;
	}
	partid = (partid_t) ulong_partid;
	if (partid < 0 || partid >= XP_MAX_PARTITIONS) {
		kdb_printf("invalid partid\n");
		return KDB_BADINT;
	}

	vars_part = (struct xpc_vars_part *) __va(xpc_vars->vars_part_pa);
	DBUG_ON(vars_part == NULL);

	if (partid == 0) {

		/* display all initialized xpc_vars_part structure */

		for (partid = 1; partid < XP_MAX_PARTITIONS; partid++) {
			if (vars_part[partid].magic == 0) {
				continue;
			}
			kdbm_xpc_print_vars_part(&vars_part[partid], partid);
		}

	} else {

		/* display specified xpc_vars_part structure */

		if (vars_part[partid].magic != 0) {
			kdbm_xpc_print_vars_part(&vars_part[partid], partid);
		} else {
			kdb_printf("struct xpc_vars_part for partid %d not "
				"initialized\n", partid);
		}
	}

	return 0;
}


static void
kdbm_xpc_print_engaged(char *string, u64 mask, int verbose)
{
	partid_t partid;


	kdb_printf("%s=0x%lx", string, mask);

	if (verbose) {
		partid = 0;
		while (mask != 0) {
			if (mask & 1UL) {
				kdb_printf(" %d", partid);
			}
			partid++;
			mask >>= 1;
		}
	}
	kdb_printf("\n");
}


/*
 * Display XPC's 'engaged partitions' and 'disengage request' AMOs.
 *
 *      xpcengaged [ -v ]
 *
 *	    -v  - verbose mode, displays partition numbers.
 */
static int
kdbm_xpc_engaged(int argc, const char **argv)
{
	int nextarg = 1;
	int verbose = 0;
	u64 mask;


	if (argc > 1) {
		return KDB_ARGCOUNT;
	}
	if (argc == 1) {
		if (strcmp(argv[nextarg], "-v") != 0) {
			return KDB_ARGCOUNT;
		}
		verbose = 1;
	}

	mask = xpc_partition_engaged(-1UL);
	kdbm_xpc_print_engaged("engaged partitions", mask, verbose);

	mask = xpc_partition_disengage_requested(-1UL);
	kdbm_xpc_print_engaged("disengage request", mask, verbose);

	return 0;
}


static void
kdbm_xpc_print_IPI_flags_for_channel(u8 IPI_flags)
{
	if (IPI_flags & XPC_IPI_MSGREQUEST)   kdb_printf(" MSGREQUEST");
	if (IPI_flags & XPC_IPI_OPENREPLY)    kdb_printf(" OPENREPLY");
	if (IPI_flags & XPC_IPI_OPENREQUEST)  kdb_printf(" OPENREQUEST");
	if (IPI_flags & XPC_IPI_CLOSEREPLY)   kdb_printf(" CLOSEREPLY");
	if (IPI_flags & XPC_IPI_CLOSEREQUEST) kdb_printf(" CLOSEREQUEST");
}


static void
kdbm_xpc_print_IPI_flags(u64 IPI_amo)
{
	int ch_number;
	u8 IPI_flags;


	for (ch_number = 0; ch_number < XPC_NCHANNELS; ch_number++) {

		/* get the IPI flags for the specific channel */
		IPI_flags = XPC_GET_IPI_FLAGS(IPI_amo, ch_number);
		if (IPI_flags == 0) {
			continue;
		}

		kdb_printf("\t    channel_%d=0x%x", ch_number, IPI_flags);
		kdbm_xpc_print_IPI_flags_for_channel(IPI_flags);
		kdb_printf("\n");
	}
}


static void
kdbm_xpc_print_part(struct xpc_partition *part, partid_t partid)
{
	kdb_printf("xpc_partitions[partid=%d] (0x%p):\n", partid,
							(void *) part);
	kdb_printf("\tremote_rp_version=0x%x %d.%d\n", part->remote_rp_version,
				XPC_VERSION_MAJOR(part->remote_rp_version),
				XPC_VERSION_MINOR(part->remote_rp_version));
	kdb_printf("\tremote_rp_stamp=0x%lx:0x%lx\n",
		part->remote_rp_stamp.tv_sec, part->remote_rp_stamp.tv_nsec);
	kdb_printf("\tremote_rp_pa=0x%lx\n", part->remote_rp_pa);
	kdb_printf("\tremote_vars_pa=0x%lx\n", part->remote_vars_pa);
	kdb_printf("\tremote_vars_part_pa=0x%lx\n", part->remote_vars_part_pa);
	kdb_printf("\tlast_heartbeat=%ld\n", part->last_heartbeat);
	kdb_printf("\tremote_amos_page_pa=0x%lx\n", part->remote_amos_page_pa);
	kdb_printf("\tremote_act_nasid=0x%x\n", part->remote_act_nasid);
	kdb_printf("\tremote_act_phys_cpuid=0x%x\n",
						part->remote_act_phys_cpuid);
	kdb_printf("\tact_IRQ_rcvd=%d\n", part->act_IRQ_rcvd);
	kdb_printf("\tact_state=%d", part->act_state);
	switch (part->act_state) {
	case XPC_P_INACTIVE:	      kdb_printf(" INACTIVE\n"); break;
	case XPC_P_ACTIVATION_REQ:    kdb_printf(" ACTIVATION_REQ\n"); break;
	case XPC_P_ACTIVATING:	      kdb_printf(" ACTIVATING\n"); break;
	case XPC_P_ACTIVE:	      kdb_printf(" ACTIVE\n"); break;
	case XPC_P_DEACTIVATING:      kdb_printf(" DEACTIVATING\n"); break;
	default:		      kdb_printf(" unknown\n");
	}
	kdb_printf("\tremote_vars_version=0x%x %d.%d\n",
				part->remote_vars_version,
				XPC_VERSION_MAJOR(part->remote_vars_version),
				XPC_VERSION_MINOR(part->remote_vars_version));
	kdb_printf("\treactivate_nasid=%d\n", part->reactivate_nasid);
	kdb_printf("\treason=%d %s\n", part->reason,
				kdbm_xpc_get_ascii_reason_code(part->reason));
	kdb_printf("\treason_line=%d\n", part->reason_line);

	kdb_printf("\tdisengage_request_timeout=0x%lx\n",
				part->disengage_request_timeout);
	kdb_printf("\t&disengage_request_timer=0x%p\n",
				(void *) &part->disengage_request_timer);

	kdb_printf("\tsetup_state=%d", part->setup_state);
	switch (part->setup_state) {
	case XPC_P_UNSET:	kdb_printf(" UNSET\n"); break;
	case XPC_P_SETUP:	kdb_printf(" SETUP\n"); break;
	case XPC_P_WTEARDOWN:	kdb_printf(" WTEARDOWN\n"); break;
	case XPC_P_TORNDOWN:	kdb_printf(" TORNDOWN\n"); break;
	default:		kdb_printf(" unknown\n");
	}
	kdb_printf("\treferences=%d\n", atomic_read(&part->references));
	kdb_printf("\tnchannels=%d\n", part->nchannels);
	kdb_printf("\tnchannels_active=%d\n",
					atomic_read(&part->nchannels_active));
	kdb_printf("\tnchannels_engaged=%d\n",
					atomic_read(&part->nchannels_engaged));
	kdb_printf("\tchannels=0x%p\n", (void *) part->channels);
	kdb_printf("\tlocal_GPs=0x%p\n", (void *) part->local_GPs);
	kdb_printf("\tremote_GPs=0x%p\n", (void *) part->remote_GPs);
	kdb_printf("\tremote_GPs_pa=0x%lx\n", part->remote_GPs_pa);
	kdb_printf("\tlocal_openclose_args=0x%p\n",
					(void *) part->local_openclose_args);
	kdb_printf("\tremote_openclose_args=0x%p\n",
					(void *) part->remote_openclose_args);
	kdb_printf("\tremote_openclose_args_pa=0x%lx\n",
					part->remote_openclose_args_pa);
	kdb_printf("\tremote_IPI_nasid=0x%x\n", part->remote_IPI_nasid);
	kdb_printf("\tremote_IPI_phys_cpuid=0x%x\n",
					part->remote_IPI_phys_cpuid);
	kdb_printf("\tremote_IPI_amo_va=0x%p\n",
					(void *) part->remote_IPI_amo_va);
	kdb_printf("\tlocal_IPI_amo_va=0x%p\n",
					(void *) part->local_IPI_amo_va);
	kdb_printf("\tlocal_IPI_amo=0x%lx\n", part->local_IPI_amo);
	kdbm_xpc_print_IPI_flags(part->local_IPI_amo);
	kdb_printf("\tIPI_owner=%s\n", part->IPI_owner);
	kdb_printf("\t&dropped_IPI_timer=0x%p\n",
					(void *) &part->dropped_IPI_timer);

	kdb_printf("\tchannel_mgr_requests=%d\n", atomic_read(&part->
							channel_mgr_requests));
}


/*
 * Display XPC partitions.
 *
 *	xpcpart [ <vaddr> | <partid> ]
 */
static int
kdbm_xpc_partitions(int argc, const char **argv)
{
	int ret;
	int nextarg = 1;
	long offset = 0;
	unsigned long addr;
	struct xpc_partition *part;
	partid_t partid;


	if (argc > 1) {
		return KDB_ARGCOUNT;

	} else if (argc == 1) {
		ret = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset,
								NULL);
		if (ret) {
			return ret;
		}
		if (addr > 0 && addr < XP_MAX_PARTITIONS) {
			partid = (partid_t) addr;
			part = &xpc_partitions[partid];
		} else {
			part = (struct xpc_partition *) addr;
			partid = part - &xpc_partitions[0];
			if (partid <= 0 || partid >= XP_MAX_PARTITIONS ||
					part != &xpc_partitions[partid]) {
				kdb_printf("invalid partition entry address\n");
				return KDB_BADADDR;
			}
		}
		kdbm_xpc_print_part(part, partid);

	} else {
		for (partid = 1; partid < XP_MAX_PARTITIONS; partid++) {
			part = &xpc_partitions[partid];
			if (part->setup_state == XPC_P_UNSET &&
							part->reason == 0) {
				continue;
			}
			kdbm_xpc_print_part(part, partid);
		}
	}
	return 0;
}


static void
kdbm_xpc_print_channel_flags(u32 flags)
{
	kdb_printf("\tflags=0x%x", flags);

	if (flags & XPC_C_WDISCONNECT)		kdb_printf(" WDISCONNECT");
	if (flags & XPC_C_DISCONNECTINGCALLOUT_MADE) kdb_printf(" DISCONNECTINGCALLOUT_MADE");
	if (flags & XPC_C_DISCONNECTINGCALLOUT)	kdb_printf(" DISCONNECTINGCALLOUT");
	if (flags & XPC_C_DISCONNECTING)	kdb_printf(" DISCONNECTING");
	if (flags & XPC_C_DISCONNECTED)		kdb_printf(" DISCONNECTED");

	if (flags & XPC_C_CLOSEREQUEST)		kdb_printf(" CLOSEREQUEST");
	if (flags & XPC_C_RCLOSEREQUEST)	kdb_printf(" RCLOSEREQUEST");
	if (flags & XPC_C_CLOSEREPLY)		kdb_printf(" CLOSEREPLY");
	if (flags & XPC_C_RCLOSEREPLY)		kdb_printf(" RCLOSEREPLY");

	if (flags & XPC_C_CONNECTING)		kdb_printf(" CONNECTING");
	if (flags & XPC_C_CONNECTED)		kdb_printf(" CONNECTED");
	if (flags & XPC_C_CONNECTEDCALLOUT_MADE) kdb_printf(" CONNECTEDCALLOUT_MADE");
	if (flags & XPC_C_CONNECTEDCALLOUT)	kdb_printf(" CONNECTEDCALLOUT");
	if (flags & XPC_C_SETUP)		kdb_printf(" SETUP");

	if (flags & XPC_C_OPENREQUEST)		kdb_printf(" OPENREQUEST");
	if (flags & XPC_C_ROPENREQUEST)		kdb_printf(" ROPENREQUEST");
	if (flags & XPC_C_OPENREPLY)		kdb_printf(" OPENREPLY");
	if (flags & XPC_C_ROPENREPLY)		kdb_printf(" ROPENREPLY");

	if (flags & XPC_C_WASCONNECTED)		kdb_printf(" WASCONNECTED");

	kdb_printf("\n");
}


static void
kdbm_xpc_print_channel(struct xpc_channel *ch)
{
	kdb_printf("channel %d (0x%p):\n", ch->number, (void *) ch);
	kdb_printf("\tpartid=%d\n", ch->partid);

	kdbm_xpc_print_channel_flags(ch->flags);

	kdb_printf("\treason=%d %s\n", ch->reason,
				kdbm_xpc_get_ascii_reason_code(ch->reason));
	kdb_printf("\treason_line=%d\n", ch->reason_line);
	kdb_printf("\tnumber=%d\n", ch->number);
	kdb_printf("\tmsg_size=%d\n", ch->msg_size);
	kdb_printf("\tlocal_nentries=%d\n", ch->local_nentries);
	kdb_printf("\tremote_nentries=%d\n", ch->remote_nentries);
	kdb_printf("\tlocal_msgqueue=0x%p\n", (void *) ch->local_msgqueue);
	kdb_printf("\tremote_msgqueue_pa=0x%lx\n", ch->remote_msgqueue_pa);
	kdb_printf("\tremote_msgqueue=0x%p\n",
					(void *) ch->remote_msgqueue);
	kdb_printf("\treferences=%d\n", atomic_read(&ch->references));
	kdb_printf("\tn_on_msg_allocate_wq=%d\n",
				atomic_read(&ch->n_on_msg_allocate_wq));
	kdb_printf("\t&msg_allocate_wq=0x%p\n",
				(void *) &ch->msg_allocate_wq);

	kdb_printf("\tdelayed_IPI_flags=0x%x", ch->delayed_IPI_flags);
	kdbm_xpc_print_IPI_flags_for_channel(ch->delayed_IPI_flags);
	kdb_printf("\n");

	kdb_printf("\tn_to_notify=%d\n", atomic_read(&ch->n_to_notify));
	kdb_printf("\tnotify_queue=0x%p\n", (void *) ch->notify_queue);
	kdb_printf("\tfunc=0x%p\n", (void *) ch->func);
	kdb_printf("\tkey=0x%p\n", ch->key);
	kdb_printf("\t&msg_to_pull_mutex=0x%p\n",
					(void *) &ch->msg_to_pull_mutex);
	kdb_printf("\t&wdisconnect_wait=0x%p\n",
					(void *) &ch->wdisconnect_wait);
	kdb_printf("\tlocal_GP=0x%p (%ld:%ld)\n", (void *) ch->local_GP,
						ch->local_GP->get,
						ch->local_GP->put);
	kdb_printf("\tremote_GP=%ld:%ld\n", ch->remote_GP.get,
						ch->remote_GP.put);
	kdb_printf("\tw_local_GP=%ld:%ld\n", ch->w_local_GP.get,
						ch->w_local_GP.put);
	kdb_printf("\tw_remote_GP=%ld:%ld\n", ch->w_remote_GP.get,
						ch->w_remote_GP.put);
	kdb_printf("\tnext_msg_to_pull=%ld\n", ch->next_msg_to_pull);
	kdb_printf("\tkthreads_assigned=%d\n",
				atomic_read(&ch->kthreads_assigned));
	kdb_printf("\tkthreads_assigned_limit=%d\n",
				ch->kthreads_assigned_limit);
	kdb_printf("\tkthreads_idle=%d\n",
				atomic_read(&ch->kthreads_idle));
	kdb_printf("\tkthreads_idle_limit=%d\n", ch->kthreads_idle_limit);
	kdb_printf("\tkthreads_active=%d\n",
				atomic_read(&ch->kthreads_active));
	kdb_printf("\tkthreads_created=%d\n", ch->kthreads_created);
	kdb_printf("\t&idle_wq=0x%p\n", (void *) &ch->idle_wq);

	if (ch->flags & XPC_C_CONNECTED) {
		kdb_printf("\n\t#of local msg queue entries available =%ld\n",
				ch->local_nentries - (ch->w_local_GP.put -
							ch->w_remote_GP.get));

		kdb_printf("\t#of local msgs allocated !sent =%ld\n",
				ch->w_local_GP.put - ch->local_GP->put);
		kdb_printf("\t#of local msgs allocated sent !ACK'd =%ld\n",
				ch->local_GP->put - ch->remote_GP.get);
		kdb_printf("\t#of local msgs allocated sent ACK'd !notified ="
			"%ld\n", ch->remote_GP.get - ch->w_remote_GP.get);

		kdb_printf("\t#of remote msgs sent !pulled =%ld\n",
				ch->w_remote_GP.put - ch->next_msg_to_pull);
		kdb_printf("\t#of remote msgs sent !delivered =%ld\n",
				ch->next_msg_to_pull - ch->w_local_GP.get);
		kdb_printf("\t#of remote msgs sent delivered !received =%ld\n",
				ch->w_local_GP.get - ch->local_GP->get);
	}
}


/*
 * Display a XPC partition's channels.
 *
 * 	xpcchan <vaddr> | <partid> [ <channel> ]
 */
static int
kdbm_xpc_channels(int argc, const char **argv)
{
	int ret;
	int nextarg = 1;
	long offset = 0;
	unsigned long addr;
	partid_t partid;
	struct xpc_partition *part;
	int ch_number;
	struct xpc_channel *ch;


	if (argc < 1 || argc > 2) {
		return KDB_ARGCOUNT;
	}

	ret = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (ret) {
		return ret;
	}
	if (addr > 0 && addr < XP_MAX_PARTITIONS) {
		partid = (partid_t) addr;
		part = &xpc_partitions[partid];
		if (part->setup_state == XPC_P_UNSET) {
			kdb_printf("partition is UNSET\n");
			return 0;
		}
		if (part->setup_state == XPC_P_TORNDOWN) {
			kdb_printf("partition is TORNDOWN\n");
			return 0;
		}

		if (argc == 2) {
			ret = kdbgetularg(argv[2],
					(unsigned long *) &ch_number);
			if (ret) {
				return ret;
			}
			if (ch_number < 0 || ch_number >= part->nchannels) {
				kdb_printf("invalid channel #\n");
				return KDB_BADINT;
			}
			kdbm_xpc_print_channel(&part->channels[ch_number]);
		} else {
			for (ch_number = 0; ch_number < part->nchannels;
								ch_number++) {
				kdbm_xpc_print_channel(&part->
							channels[ch_number]);
			}
		}

	} else {
		ch = (struct xpc_channel *) addr;

		for (partid = 1; partid < XP_MAX_PARTITIONS; partid++) {
			part = &xpc_partitions[partid];
			if (part->setup_state != XPC_P_UNSET &&
					part->setup_state != XPC_P_TORNDOWN &&
						ch >= part->channels) {
				ch_number = ch - part->channels;
				if (ch_number < part->nchannels &&
					     ch == &part->channels[ch_number]) {
					break;
				}
			}
		}
		if (partid == XP_MAX_PARTITIONS) {
			kdb_printf("invalid channel address\n");
			return KDB_BADADDR;
		}
		kdbm_xpc_print_channel(ch);
	}

	return 0;
}


static void
kdbm_xpc_print_local_msgqueue(struct xpc_channel *ch)
{
	int i;
	char *prefix;
	struct xpc_msg *msg = ch->local_msgqueue;
	s64 w_remote_GP_get = ch->w_remote_GP.get % ch->local_nentries;
	s64 remote_GP_get = ch->remote_GP.get % ch->local_nentries;
	s64 local_GP_put = ch->local_GP->put % ch->local_nentries;
	s64 w_local_GP_put = ch->w_local_GP.put % ch->local_nentries;


	kdb_printf("local message queue (0x%p):\n\n", (void *) msg);

	for (i = 0; i < ch->local_nentries; i++) {
		kdb_printf("0x%p: flags=0x%x number=%ld", (void *) msg,
				msg->flags, msg->number);

		prefix = "  <--";

		if (i == w_remote_GP_get) {
			kdb_printf("%s w_remote_GP.get", prefix);
			prefix = ",";
		}
		if (i == remote_GP_get) {
			kdb_printf("%s remote_GP.get", prefix);
			prefix = ",";
		}
		if (i == local_GP_put) {
			kdb_printf("%s local_GP->put", prefix);
			prefix = ",";
		}
		if (i == w_local_GP_put) {
			kdb_printf("%s w_local_GP.put", prefix);
		}
		kdb_printf("\n");

		msg = (struct xpc_msg *) ((u64) msg + ch->msg_size);
	}
}


static void
kdbm_xpc_print_remote_msgqueue(struct xpc_channel *ch)
{
	int i;
	char *prefix;
	struct xpc_msg *msg = ch->remote_msgqueue;
	s64 local_GP_get = ch->local_GP->get % ch->remote_nentries;
	s64 w_local_GP_get = ch->w_local_GP.get % ch->remote_nentries;
	s64 next_msg_to_pull = ch->next_msg_to_pull % ch->remote_nentries;
	s64 w_remote_GP_put = ch->w_remote_GP.put % ch->remote_nentries;
	s64 remote_GP_put = ch->remote_GP.put % ch->remote_nentries;


	kdb_printf("cached remote message queue (0x%p):\n\n", (void *) msg);

	for (i = 0; i < ch->remote_nentries; i++) {
		kdb_printf("0x%p: flags=0x%x number=%ld", (void *) msg,
				msg->flags, msg->number);

		prefix = "  <--";

		if (i == local_GP_get) {
			kdb_printf("%s local_GP->get", prefix);
			prefix = ",";
		}
		if (i == w_local_GP_get) {
			kdb_printf("%s w_local_GP.get", prefix);
			prefix = ",";
		}
		if (i == next_msg_to_pull) {
			kdb_printf("%s next_msg_to_pull", prefix);
			prefix = ",";
		}
		if (i == w_remote_GP_put) {
			kdb_printf("%s w_remote_GP.put", prefix);
			prefix = ",";
		}
		if (i == remote_GP_put) {
			kdb_printf("%s remote_GP.put", prefix);
		}
		kdb_printf("\n");

		msg = (struct xpc_msg *) ((u64) msg + ch->msg_size);
	}
}


/*
 * Display XPC specified message queue.
 *
 *	xpcmque <partid> <channel> local|remote
 */
static int
kdbm_xpc_msgqueue(int argc, const char **argv)
{
	int ret, ch_number;
	unsigned long ulong_partid;
	partid_t partid;
	struct xpc_partition *part;
	struct xpc_channel *ch;


	if (argc != 3) {
		return KDB_ARGCOUNT;
	}

	ret = kdbgetularg(argv[1], (unsigned long *) &ulong_partid);
	if (ret) {
		return ret;
	}
	partid = (partid_t) ulong_partid;
	if (partid <= 0 || partid >= XP_MAX_PARTITIONS) {
		kdb_printf("invalid partid\n");
		return KDB_BADINT;
	}

	ret = kdbgetularg(argv[2], (unsigned long *) &ch_number);
	if (ret) {
		return ret;
	}
	if (ch_number < 0 || ch_number >= XPC_NCHANNELS) {
		kdb_printf("invalid channel #\n");
		return KDB_BADINT;
	}

	part = &xpc_partitions[partid];

	if (part->setup_state == XPC_P_UNSET) {
		kdb_printf("partition is UNSET\n");
		return 0;
	}
	if (part->setup_state == XPC_P_TORNDOWN) {
		kdb_printf("partition is TORNDOWN\n");
		return 0;
	}

	if (ch_number >= part->nchannels) {
		kdb_printf("unsupported channel #\n");
		return KDB_BADINT;
	}

	ch = &part->channels[ch_number];

	if (!(ch->flags & XPC_C_SETUP)) {
		kdb_printf("message queues are not SETUP\n");
		return 0;
	}

	if (strcmp(argv[3], "r") == 0 || strcmp(argv[3], "remote") == 0) {
		kdbm_xpc_print_remote_msgqueue(ch);
	} else if (strcmp(argv[3], "l") == 0 || strcmp(argv[3], "local") == 0) {
		kdbm_xpc_print_local_msgqueue(ch);
	} else {
		kdb_printf("unknown msg queue selected\n");
		return KDB_BADINT;
	}

	return 0;
}


static void
kdbm_xpc_print_msg_flags(u8 flags)
{
	kdb_printf("\tflags=0x%x", flags);

	if (flags & XPC_M_INTERRUPT)	kdb_printf(" INTERRUPT");
	if (flags & XPC_M_READY)	kdb_printf(" READY");
	if (flags & XPC_M_DONE)		kdb_printf(" DONE");

	kdb_printf("\n");
}


/*
 * Display XPC message.
 *
 *	xpcmsg <vaddr>
 */
static int
kdbm_xpc_msg(int argc, const char **argv)
{
	int ret, nextarg = argc;
	long offset = 0;
	unsigned long addr;
	struct xpc_msg *msg;


	if (argc != 1) {
		return KDB_ARGCOUNT;
	}

	ret = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (ret) {
		return ret;
	}

	msg = (struct xpc_msg *) addr;
	kdb_printf("msg (0x%p):\n", (void *) msg);
	kdbm_xpc_print_msg_flags(msg->flags);
	kdb_printf("\tnumber=%ld\n", msg->number);
	kdb_printf("\t&payload=0x%p\n", (void *) &msg->payload);

	return 0;
}


static void
kdbm_xpc_print_notify_queue(struct xpc_channel *ch)
{
	int i;
	char *prefix;
	struct xpc_notify *notify = ch->notify_queue;
	s64 w_remote_GP_get = ch->w_remote_GP.get % ch->local_nentries;
	s64 remote_GP_get = ch->remote_GP.get % ch->local_nentries;
	s64 local_GP_put = ch->local_GP->put % ch->local_nentries;
	s64 w_local_GP_put = ch->w_local_GP.put % ch->local_nentries;


	kdb_printf("notify queue (0x%p):\n\n", (void *) notify);

	for (i = 0; i < ch->local_nentries; i++) {
		kdb_printf("0x%p: type=0x%x", (void *) notify, notify->type);

		if (notify->type == XPC_N_CALL) {
			kdb_printf(" CALL  func=0x%p key=0x%p",
					(void *) notify->func, notify->key);
		}

		prefix = "  <--";

		if (i == w_remote_GP_get) {
			kdb_printf("%s w_remote_GP.get", prefix);
			prefix = ",";
		}
		if (i == remote_GP_get) {
			kdb_printf("%s remote_GP.get", prefix);
			prefix = ",";
		}
		if (i == local_GP_put) {
			kdb_printf("%s local_GP->put", prefix);
			prefix = ",";
		}
		if (i == w_local_GP_put) {
			kdb_printf("%s w_local_GP.put", prefix);
		}
		kdb_printf("\n");

		notify++;
	}
}


/*
 * Display XPC specified notify queue.
 *
 *	xpcnque <partid> <channel>
 */
static int
kdbm_xpc_notify_queue(int argc, const char **argv)
{
	int ret, ch_number;
	unsigned long ulong_partid;
	partid_t partid;
	struct xpc_partition *part;
	struct xpc_channel *ch;


	if (argc != 2) {
		return KDB_ARGCOUNT;
	}

	ret = kdbgetularg(argv[1], (unsigned long *) &ulong_partid);
	if (ret) {
		return ret;
	}
	partid = (partid_t) ulong_partid;
	if (partid <= 0 || partid >= XP_MAX_PARTITIONS) {
		kdb_printf("invalid partid\n");
		return KDB_BADINT;
	}

	ret = kdbgetularg(argv[2], (unsigned long *) &ch_number);
	if (ret) {
		return ret;
	}
	if (ch_number < 0 || ch_number >= XPC_NCHANNELS) {
		kdb_printf("invalid channel #\n");
		return KDB_BADINT;
	}

	part = &xpc_partitions[partid];

	if (part->setup_state == XPC_P_UNSET) {
		kdb_printf("partition is UNSET\n");
		return 0;
	}
	if (part->setup_state == XPC_P_TORNDOWN) {
		kdb_printf("partition is TORNDOWN\n");
		return 0;
	}

	if (ch_number >= part->nchannels) {
		kdb_printf("unsupported channel #\n");
		return KDB_BADINT;
	}

	ch = &part->channels[ch_number];

	if (!(ch->flags & XPC_C_SETUP)) {
		kdb_printf("notify queue is not SETUP\n");
		return 0;
	}

	kdbm_xpc_print_notify_queue(ch);

	return 0;
}


static void
kdbm_xpc_print_users(struct xpc_registration *registration, int ch_number)
{
	kdb_printf("xpc_registrations[channel=%d] (0x%p):\n", ch_number,
							(void *) registration);

	kdb_printf("\t&mutex=0x%p\n", (void *) &registration->mutex);
	kdb_printf("\tfunc=0x%p\n", (void *) registration->func);
	kdb_printf("\tkey=0x%p\n", registration->key);
	kdb_printf("\tnentries=%d\n", registration->nentries);
	kdb_printf("\tmsg_size=%d\n", registration->msg_size);
	kdb_printf("\tassigned_limit=%d\n", registration->assigned_limit);
	kdb_printf("\tidle_limit=%d\n", registration->idle_limit);
}


/*
 * Display current XPC users who have registered via xpc_connect().
 *
 *	xpcusers [ <channel> ]
 */
static int
kdbm_xpc_users(int argc, const char **argv)
{
	int ret;
	struct xpc_registration *registration;
	int ch_number;


	if (argc > 1) {
		return KDB_ARGCOUNT;

	} else if (argc == 1) {
		ret = kdbgetularg(argv[1], (unsigned long *) &ch_number);
		if (ret) {
			return ret;
		}
		if (ch_number < 0 || ch_number >= XPC_NCHANNELS) {
			kdb_printf("invalid channel #\n");
			return KDB_BADINT;
		}
		registration = &xpc_registrations[ch_number];
		kdbm_xpc_print_users(registration, ch_number);

	} else {
		for (ch_number = 0; ch_number < XPC_NCHANNELS; ch_number++) {
			registration = &xpc_registrations[ch_number];

			/* if !XPC_CHANNEL_REGISTERED(ch_number) */
			if (registration->func == NULL) {
				continue;
			}
			kdbm_xpc_print_users(registration, ch_number);
		}
	}
	return 0;
}


static int __init
kdbm_xpc_register(void)
{
	(void) kdb_register("xpcdown", kdbm_xpc_down, "",
			"Mark this partition as being down", 0);
	(void) kdb_register("xpcrp", kdbm_xpc_rsvd_page, "",
			"Display XPC reserved page", 0);
	(void) kdb_register("xpcvars", kdbm_xpc_variables, "[<partid>]",
			"Display XPC variables", 0);
	(void) kdb_register("xpcengaged", kdbm_xpc_engaged, "[-v]",
			"Display XPC engaged partitions AMOs", 0);
	(void) kdb_register("xpcpart", kdbm_xpc_partitions, "[<vaddr>|"
			"<partid>]", "Display struct xpc_partition entries", 0);
	(void) kdb_register("xpcchan", kdbm_xpc_channels, "<vaddr> | <partid> "
			"[<channel>]", "Display struct xpc_channel entries", 0);
	(void) kdb_register("xpcmque", kdbm_xpc_msgqueue, "<partid> <channel> "
			"local|remote", "Display local or remote msg queue", 0);
	(void) kdb_register("xpcmsg", kdbm_xpc_msg, "<vaddr>",
			"Display struct xpc_msg", 0);
	(void) kdb_register("xpcnque", kdbm_xpc_notify_queue, "<partid> "
			"<channel>", "Display notify queue", 0);
	(void) kdb_register("xpcusers", kdbm_xpc_users, "[ <channel> ]",
			"Display struct xpc_registration entries", 0);
	return 0;
}


static void __exit
kdbm_xpc_unregister(void)
{
	(void) kdb_unregister("xpcdown");
	(void) kdb_unregister("xpcrp");
	(void) kdb_unregister("xpcvars");
	(void) kdb_unregister("xpcengaged");
	(void) kdb_unregister("xpcpart");
	(void) kdb_unregister("xpcchan");
	(void) kdb_unregister("xpcmque");
	(void) kdb_unregister("xpcmsg");
	(void) kdb_unregister("xpcnque");
	(void) kdb_unregister("xpcusers");
}


module_init(kdbm_xpc_register);
module_exit(kdbm_xpc_unregister);

