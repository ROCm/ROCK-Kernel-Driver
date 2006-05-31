#ifndef _IET_U_H
#define _IET_U_H

#define IET_VERSION_STRING	"0.4.13"

/* The maximum length of 223 bytes in the RFC. */
#define ISCSI_NAME_LEN	256
#define ISCSI_ARGS_LEN	2048

#define VENDOR_ID_LEN	8
#define SCSI_ID_LEN	24

struct target_info {
	u32 tid;
	char name[ISCSI_NAME_LEN];
};

struct volume_info {
	u32 tid;

	u32 lun;
	char args[ISCSI_ARGS_LEN]; /* FIXME */
};

struct session_info {
	u32 tid;

	u64 sid;
	char initiator_name[ISCSI_NAME_LEN];
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
};

#define DIGEST_ALL	(DIGEST_NONE | DIGEST_CRC32C)
#define DIGEST_NONE		(1 << 0)
#define DIGEST_CRC32C           (1 << 1)

struct conn_info {
	u32 tid;
	u64 sid;

	u32 cid;
	u32 stat_sn;
	u32 exp_stat_sn;
	int header_digest;
	int data_digest;
	int fd;
};

enum {
	key_initial_r2t,
	key_immediate_data,
	key_max_connections,
	key_max_recv_data_length,
	key_max_xmit_data_length,
	key_max_burst_length,
	key_first_burst_length,
	key_default_wait_time,
	key_default_retain_time,
	key_max_outstanding_r2t,
	key_data_pdu_inorder,
	key_data_sequence_inorder,
	key_error_recovery_level,
	key_header_digest,
	key_data_digest,
	key_ofmarker,
	key_ifmarker,
	key_ofmarkint,
	key_ifmarkint,
	session_key_last,
};

enum {
	key_wthreads,
	key_target_type,
	key_queued_cmnds,
	target_key_last,
};

enum {
	key_session,
	key_target,
};

struct iscsi_param_info {
	u32 tid;
	u64 sid;

	u32 param_type;
	u32 partial;

	u32 session_param[session_key_last];
	u32 target_param[target_key_last];
};

enum iet_event_state {
	E_CONN_CLOSE,
};

struct iet_event {
	u32 tid;
	u64 sid;
	u32 cid;
	u32 state;
};

#define	DEFAULT_NR_WTHREADS	8
#define	MIN_NR_WTHREADS		1
#define	MAX_NR_WTHREADS		128

#define	DEFAULT_NR_QUEUED_CMNDS	32
#define	MIN_NR_QUEUED_CMNDS	1
#define	MAX_NR_QUEUED_CMNDS	256

#define NETLINK_IET	21

#define ADD_TARGET _IOW('i', 0, struct target_info)
#define DEL_TARGET _IOW('i', 1, struct target_info)
#define START_TARGET _IO('i', 2)
#define STOP_TARGET _IO('i', 3)
#define ADD_VOLUME _IOW('i', 4, struct volume_info)
#define DEL_VOLUME _IOW('i', 5, struct volume_info)
#define ADD_SESSION _IOW('i', 6, struct session_info)
#define DEL_SESSION _IOW('i', 7, struct session_info)
#define GET_SESSION_INFO _IOWR('i', 8, struct session_info)
#define ADD_CONN _IOW('i', 9, struct conn_info)
#define DEL_CONN _IOW('i', 10, struct conn_info)
#define GET_CONN_INFO _IOWR('i', 11, struct conn_info)
#define ISCSI_PARAM_SET _IOW('i', 12, struct iscsi_param_info)
#define ISCSI_PARAM_GET _IOWR('i', 13, struct iscsi_param_info)

#endif
