/*
 * (C) 2005 FUJITA Tomonori <tomof@acm.org>
 *
 * This code is licenced under the GPL.
 */

#include "iscsi.h"
#include "iscsi_dbg.h"
#include "digest.h"

struct target_type *target_type_array[] = {
	&disk_ops,
};

#define	CHECK_PARAM(info, iparam, word, min, max)			\
do {									\
	if (!info->partial || (info->partial & 1 << key_##word))	\
		if (iparam[key_##word] < min ||				\
			iparam[key_##word] > max) {			\
			eprintk("%s: %u is out of range (%u %u)\n",	\
				#word, iparam[key_##word], min, max);	\
			iparam[key_##word] = min;			\
		}							\
} while (0)

#define	SET_PARAM(param, info, iparam, word)				\
({									\
	int changed = 0;						\
	if (!info->partial || (info->partial & 1 << key_##word)) {	\
		if (param->word != iparam[key_##word])			\
			changed = 1;					\
		param->word = iparam[key_##word];			\
	}								\
	changed;							\
})

#define	GET_PARAM(param, info, iparam, word)				\
do {									\
	iparam[key_##word] = param->word;				\
} while (0)

static void sess_param_check(struct iscsi_param_info *info)
{
	u32 *iparam = info->session_param;

	CHECK_PARAM(info, iparam, max_connections, 1, 65535);
	CHECK_PARAM(info, iparam, max_recv_data_length, 512,
		    (u32) ((ISCSI_CONN_IOV_MAX - 1) * PAGE_CACHE_SIZE));
	CHECK_PARAM(info, iparam, max_xmit_data_length, 512,
		    (u32) ((ISCSI_CONN_IOV_MAX - 1) * PAGE_CACHE_SIZE));
	CHECK_PARAM(info, iparam, error_recovery_level, 0, 0);
	CHECK_PARAM(info, iparam, data_pdu_inorder, 1, 1);
	CHECK_PARAM(info, iparam, data_sequence_inorder, 1, 1);

	digest_alg_available(&iparam[key_header_digest]);
	digest_alg_available(&iparam[key_data_digest]);

	CHECK_PARAM(info, iparam, ofmarker, 0, 0);
	CHECK_PARAM(info, iparam, ifmarker, 0, 0);
}

static void sess_param_set(struct iscsi_sess_param *param, struct iscsi_param_info *info)
{
	u32 *iparam = info->session_param;

	SET_PARAM(param, info, iparam, initial_r2t);
	SET_PARAM(param, info, iparam, immediate_data);
	SET_PARAM(param, info, iparam, max_connections);
	SET_PARAM(param, info, iparam, max_recv_data_length);
	SET_PARAM(param, info, iparam, max_xmit_data_length);
	SET_PARAM(param, info, iparam, max_burst_length);
	SET_PARAM(param, info, iparam, first_burst_length);
	SET_PARAM(param, info, iparam, default_wait_time);
	SET_PARAM(param, info, iparam, default_retain_time);
	SET_PARAM(param, info, iparam, max_outstanding_r2t);
	SET_PARAM(param, info, iparam, data_pdu_inorder);
	SET_PARAM(param, info, iparam, data_sequence_inorder);
	SET_PARAM(param, info, iparam, error_recovery_level);
	SET_PARAM(param, info, iparam, header_digest);
	SET_PARAM(param, info, iparam, data_digest);
	SET_PARAM(param, info, iparam, ofmarker);
	SET_PARAM(param, info, iparam, ifmarker);
	SET_PARAM(param, info, iparam, ofmarkint);
	SET_PARAM(param, info, iparam, ifmarkint);
}

static void sess_param_get(struct iscsi_sess_param *param, struct iscsi_param_info *info)
{
	u32 *iparam = info->session_param;

	GET_PARAM(param, info, iparam, initial_r2t);
	GET_PARAM(param, info, iparam, immediate_data);
	GET_PARAM(param, info, iparam, max_connections);
	GET_PARAM(param, info, iparam, max_recv_data_length);
	GET_PARAM(param, info, iparam, max_xmit_data_length);
	GET_PARAM(param, info, iparam, max_burst_length);
	GET_PARAM(param, info, iparam, first_burst_length);
	GET_PARAM(param, info, iparam, default_wait_time);
	GET_PARAM(param, info, iparam, default_retain_time);
	GET_PARAM(param, info, iparam, max_outstanding_r2t);
	GET_PARAM(param, info, iparam, data_pdu_inorder);
	GET_PARAM(param, info, iparam, data_sequence_inorder);
	GET_PARAM(param, info, iparam, error_recovery_level);
	GET_PARAM(param, info, iparam, header_digest);
	GET_PARAM(param, info, iparam, data_digest);
	GET_PARAM(param, info, iparam, ofmarker);
	GET_PARAM(param, info, iparam, ifmarker);
	GET_PARAM(param, info, iparam, ofmarkint);
	GET_PARAM(param, info, iparam, ifmarkint);
}

static void trgt_param_check(struct iscsi_param_info *info)
{
	u32 *iparam = info->target_param;

	CHECK_PARAM(info, iparam, wthreads, MIN_NR_WTHREADS, MAX_NR_WTHREADS);
	CHECK_PARAM(info, iparam, target_type, 0,
		    (unsigned int) ARRAY_SIZE(target_type_array) - 1);
	CHECK_PARAM(info, iparam, queued_cmnds, MIN_NR_QUEUED_CMNDS, MAX_NR_QUEUED_CMNDS);
}

static void trgt_param_set(struct iscsi_target *target, struct iscsi_param_info *info)
{
	struct iscsi_trgt_param *param = &target->trgt_param;
	u32 *iparam = info->target_param;

	if (SET_PARAM(param, info, iparam, wthreads))
		wthread_start(target);
	SET_PARAM(param, info, iparam, target_type);
	SET_PARAM(param, info, iparam, queued_cmnds);
}

static void trgt_param_get(struct iscsi_trgt_param *param, struct iscsi_param_info *info)
{
	u32 *iparam = info->target_param;

	GET_PARAM(param, info, iparam, wthreads);
	GET_PARAM(param, info, iparam, target_type);
	GET_PARAM(param, info, iparam, queued_cmnds);
}

static int trgt_param(struct iscsi_target *target, struct iscsi_param_info *info, int set)
{

	if (set) {
		trgt_param_check(info);
		trgt_param_set(target, info);
	} else
		trgt_param_get(&target->trgt_param, info);

	return 0;
}

static int sess_param(struct iscsi_target *target, struct iscsi_param_info *info, int set)
{
	struct iscsi_session *session = NULL;
	struct iscsi_sess_param *param;
	int err = -ENOENT;

	if (set)
		sess_param_check(info);

	if (info->sid) {
		if (!(session = session_lookup(target, info->sid)))
			goto out;
		param = &session->param;
	} else {
		param = &target->sess_param;
	}

	if (set) {
		sess_param_set(param, info);
		show_param(param);
	} else
		sess_param_get(param, info);

	err = 0;
out:
	return err;
}

int iscsi_param_set(struct iscsi_target *target, struct iscsi_param_info *info, int set)
{
	int err;

	if (info->param_type == key_session)
		err = sess_param(target, info, set);
	else if (info->param_type == key_target)
		err = trgt_param(target, info, set);
	else
		err = -EINVAL;

	return err;
}
