/* File veth.h created by Kyle A. Lucke on Mon Aug  7 2000. */

/* Change Activity: */
/* End Change Activity */

#ifndef _VETH_H
#define _VETH_H

#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>
#include <linux/netdevice.h>

#define VethEventTypeCap (0)
#define VethEventTypeFrames (1)
#define VethEventTypeMonitor (2)
#define VethEventTypeFramesAck (3)

#define VethMaxFramesMsgsAcked (20)
#define VethMaxFramesMsgs (0xFFFF)
#define VethMaxFramesPerMsg (6)
#define VethAckTimeoutUsec (1000000)

#define VETHSTACK(T) \
	struct VethStack##T \
	{ \
		struct T *head; \
		spinlock_t lock; \
	}
#define VETHSTACKCTOR(s) \
	do { (s)->head = NULL; spin_lock_init(&(s)->lock); } while(0)
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
	u32 mAddress[6];
	u16 mLength[6];
	u32 mEofMask:6;
	u32 mReserved:26;
};

struct VethFramesAckData {
	u16 mToken[VethMaxFramesMsgsAcked];
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

struct VethFastPathData {
	u64 mData1;
	u64 mData2;
	u64 mData3;
	u64 mData4;
	u64 mData5;
};

struct VethLpEvent {
	struct HvLpEvent mBaseEvent;
	union {
		struct VethFramesData mSendData;
		struct VethCapData mCapabilitiesData;
		struct VethFramesAckData mFramesAckData;
		struct VethFastPathData mFastPathData;
	} mDerivedData;

};

struct VethMsg {
	struct VethMsg *next;
	union {
		struct VethFramesData mSendData;
		u64 raw[5];
	} mEvent;
	int mIndex;
	unsigned long mInUse;
	struct sk_buff *skb;
};

struct VethLpConnection {
	u64 mEyecatcher;
	HvLpIndex remote_lp;
	HvLpInstanceId src_inst;
	HvLpInstanceId dst_inst;
	u32 mNumMsgs;
	struct VethMsg *mMsgs;
	int mNumberRcvMsgs;
	int mNumberLpAcksAlloced;
	union {
		struct VethFramesAckData mAckData;
		u64 raw[5];
	} mEventData;
	spinlock_t ack_gate;
	u32 mNumAcks;
	spinlock_t status_gate;
	struct {
		u64 mOpen:1;
		u64 mCapMonAlloced:1;
		u64 mBaseMsgsAlloced:1;
		u64 mSentCap:1;
		u64 mCapAcked:1;
		u64 mGotCap:1;
		u64 mGotCapAcked:1;
		u64 mSentMonitor:1;
		u64 mPopulatedRings:1;
		u64 mReserved:54;
		u64 mFailed:1;
	} status;
	struct VethCapData mMyCap;
	struct VethCapData mRemoteCap;
	unsigned long mCapAckTaskPending;
	struct work_struct mCapAckTaskTq;
	struct VethLpEvent mCapAckEvent;
	unsigned long mCapTaskPending;
	struct work_struct mCapTaskTq;
	struct VethLpEvent mCapEvent;
	unsigned long mMonitorAckTaskPending;
	struct work_struct mMonitorAckTaskTq;
	struct VethLpEvent mMonitorAckEvent;
	unsigned long mAllocTaskPending;
	struct work_struct mAllocTaskTq;
	int mNumberAllocated;
	struct timer_list ack_timer;
	u32 mTimeout;
	VETHSTACK(VethMsg) mMsgStack;
};

#define HVMAXARCHITECTEDVIRTUALLANS 16
struct veth_port {
	struct net_device *mDev;
	struct net_device_stats stats;
	u64 mMyAddress;
	int mPromiscuous;
	int all_mcast;
	rwlock_t mcast_gate;
	int mNumAddrs;
	u64 mMcasts[12];
};

struct VethFabricMgr {
	u64 mEyecatcher;
	HvLpIndex mThisLp;
	struct VethLpConnection mConnection[HVMAXARCHITECTEDLPS];
	struct veth_port *mPorts[HVMAXARCHITECTEDVIRTUALLANS];
};

#endif	/* _VETH_H */
