/* File veth.h created by Kyle A. Lucke on Mon Aug  7 2000. */

/* Change Activity: */
/* End Change Activity */

#ifndef _ISERIES_VETH_H
#define _ISERIES_VETH_H

#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>
#include <linux/netdevice.h>

#define VethEventTypeCap (0)
#define VethEventTypeFrames (1)
#define VethEventTypeMonitor (2)
#define VethEventTypeFramesAck (3)

#define VETH_MAX_ACKS_PER_MSG	(20)
#define VETH_MAXFRAMESPERMSG	(6)
#define VETH_ACKTIMEOUT 	(1000000) /* microseconds */
#define HVMAXARCHITECTEDVIRTUALLANS 16
#define VETH_MAX_MCAST	12

#define VETHSTACK(T) \
	struct VethStack##T \
	{ \
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
	} while(0)

#define VETHSTACKPOP(s,p) \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&(s)->lock,flags); \
		(p) = (s)->head; \
		if ((s)->head != NULL) { \
			(s)->head = (s)->head->next; \
		} \
		spin_unlock_irqrestore(&(s)->lock, flags); \
	} while(0)

struct VethFramesData {
	u32 addr[6];
	u16 len[6];
	u32 eof:6;
	u32 mReserved:26;
};

struct VethFramesAckData {
	u16 mToken[VETH_MAX_ACKS_PER_MSG];
};

struct VethCapData {
	u8 mVersion;
	u8 mReserved1;
	u16 mNumberBuffers;
	u16 mThreshold;
	u16 mReserved2;
	u32 mTimer;
	u32 mReserved3;
	u64 mReserved4;
	u64 mReserved5;
	u64 mReserved6;
};

struct VethLpEvent {
	struct HvLpEvent mBaseEvent;
	union {
		struct VethFramesData mSendData;
		struct VethCapData mCapabilitiesData;
		struct VethFramesAckData mFramesAckData;
	} u;

};

struct VethMsg {
	struct VethMsg *next;
	struct VethFramesData mSendData;
	int mIndex;
	unsigned long mInUse;
	struct sk_buff *skb;
};

struct VethLpConnection {
	HvLpIndex remote_lp;
	struct work_struct finish_open_wq;
	struct work_struct monitor_ack_wq;
	struct timer_list ack_timer;
	u32 mNumMsgs;
	struct VethMsg *mMsgs;

	HvLpInstanceId src_inst;
	HvLpInstanceId dst_inst;

	spinlock_t status_gate;
	struct {
		u64 mOpen:1;
		u64 ready;
		u64 sent_caps:1;
		u64 got_cap:1;
		u64 got_cap_ack:1;
		u64 monitor_ack_pending:1;
	} status;
	struct VethLpEvent cap_event, cap_ack_event;

	spinlock_t ack_gate;
	u16 pending_acks[VETH_MAX_ACKS_PER_MSG];
	u32 num_pending_acks;

	int mNumberRcvMsgs;
	int mNumberLpAcksAlloced;
	struct VethCapData mMyCap;
	struct VethCapData mRemoteCap;
	unsigned long mAllocTaskPending;
	int mNumberAllocated;
	u32 ack_timeout;
	VETHSTACK(VethMsg) mMsgStack;
};

struct veth_port {
	struct net_device *mDev;
	struct net_device_stats stats;
	u64 mMyAddress;

	rwlock_t mcast_gate;
	int promiscuous;
	int all_mcast;
	int num_mcast;
	u64 mcast_addr[VETH_MAX_MCAST];
};

struct VethFabricMgr {
	u64 mEyecatcher;
	HvLpIndex mThisLp;
	struct VethLpConnection mConnection[HVMAXARCHITECTEDLPS];
	struct veth_port *mPorts[HVMAXARCHITECTEDVIRTUALLANS];
};

#endif	/* _ISERIES_VETH_H */
