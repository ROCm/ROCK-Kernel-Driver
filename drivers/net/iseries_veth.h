/* File veth.h created by Kyle A. Lucke on Mon Aug  7 2000. */

#ifndef _ISERIES_VETH_H
#define _ISERIES_VETH_H

#define VethEventTypeCap	(0)
#define VethEventTypeFrames	(1)
#define VethEventTypeMonitor	(2)
#define VethEventTypeFramesAck	(3)

#define VETH_MAX_ACKS_PER_MSG	(20)
#define VETH_MAX_FRAMES_PER_MSG	(6)

struct VethFramesData {
	u32 addr[VETH_MAX_FRAMES_PER_MSG];
	u16 len[VETH_MAX_FRAMES_PER_MSG];
	u32 eof:VETH_MAX_FRAMES_PER_MSG;
	u32 mReserved:(32-VETH_MAX_FRAMES_PER_MSG);
};

struct VethFramesAckData {
	u16 token[VETH_MAX_ACKS_PER_MSG];
};

struct VethCapData {
	u8 caps_version;
	u8 rsvd1;
	u16 num_buffers;
	u16 ack_threshold;
	u16 rsvd2;
	u32 ack_timeout;
	u32 rsvd3;
	u64 rsvd4[3];
};

struct VethLpEvent {
	struct HvLpEvent base_event;
	union {
		struct VethCapData caps_data;
		struct VethFramesData frames_data;
		struct VethFramesAckData frames_ack_data;
	} u;

};

#define VETH_ACKTIMEOUT 	(1000000) /* microseconds */
#define VETH_MAX_MCAST		(12)

#define HVMAXARCHITECTEDVIRTUALLANS (16)

#define VETHSTACK(T) \
	struct VethStack##T { \
		struct T *head; \
		spinlock_t lock; \
	}
#define VETHSTACKPUSH(s, p) \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&(s)->lock,flags); \
		(p)->next = (s)->head; \
		(s)->head = (p); \
		spin_unlock_irqrestore(&(s)->lock, flags); \
	} while (0)

#define VETHSTACKPOP(s,p) \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&(s)->lock,flags); \
		(p) = (s)->head; \
		if ((s)->head) \
			(s)->head = (s)->head->next; \
		spin_unlock_irqrestore(&(s)->lock, flags); \
	} while (0)

struct veth_msg {
	struct veth_msg *next;
	struct VethFramesData data;
	int token;
	unsigned long in_use;
	struct sk_buff *skb;
};

struct veth_lpar_connection {
	HvLpIndex remote_lp;
	struct work_struct finish_open_wq;
	struct work_struct monitor_ack_wq;
	struct timer_list ack_timer;
	struct veth_msg *msgs;

	HvLpInstanceId src_inst;
	HvLpInstanceId dst_inst;

	spinlock_t status_gate;
	struct {
		u64 open:1;
		u64 ready:1;
		u64 sent_caps:1;
		u64 got_cap:1;
		u64 got_cap_ack:1;
		u64 monitor_ack_pending:1;
	} status;
	struct VethLpEvent cap_event, cap_ack_event;

	spinlock_t ack_gate;
	u16 pending_acks[VETH_MAX_ACKS_PER_MSG];
	u32 num_pending_acks;

	int mNumberAllocated;
	int mNumberRcvMsgs;
	int mNumberLpAcksAlloced;
	struct VethCapData local_caps;
	struct VethCapData remote_caps;
	u32 ack_timeout;

	VETHSTACK(veth_msg) msg_stack;
};

struct veth_port {
	struct net_device_stats stats;
	u64 mac_addr;
	HvLpIndexMap lpar_map;

	spinlock_t pending_gate;
	struct sk_buff *pending_skb;
	HvLpIndexMap pending_lpmask;

	rwlock_t mcast_gate;
	int promiscuous;
	int all_mcast;
	int num_mcast;
	u64 mcast_addr[VETH_MAX_MCAST];
};

struct VethFabricMgr {
	HvLpIndex this_lp;
	struct veth_lpar_connection connection[HVMAXARCHITECTEDLPS];
	struct net_device *netdev[HVMAXARCHITECTEDVIRTUALLANS];
};

#endif	/* _ISERIES_VETH_H */
