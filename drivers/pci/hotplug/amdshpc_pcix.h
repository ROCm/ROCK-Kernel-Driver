//+
// PCI Configuration Space
//-

#include "amdshpc.h"

#define AMD_ID		0x1022
#define GOLEM_ID	0x7450
#define POGO_ID		0x7458

#define PCI_ID_OFFSET		0x00
#define PCI_CMD_STATUS_OFFSET	0x04
#define PCI_CLASS_INFO_OFFSET	0x08
#define PCI_HEADER_INFO_OFFSET	0x0C
#define PCI_BASE_ADDR_OFFSET	0x10
#define PCI_BUS_INFO_OFFSET	0x18
#define PCI_CAP_POINTER_OFFSET	0x34

#define PCI_BUS_CLASS		0x06
#define PCIX_CAPABILITY_ID	0x07

union pci_cmd_status {
	struct {
	       	u32	IoSpace                 : 1;	// [0]
	       	u32	MemSpace                : 1;	// [1]
	       	u32     BusMaster               : 1;	// [2]
	       	u32     SpecialCycles           : 1;	// [3]
	       	u32     MemWriteEnable          : 1;	// [4]
	       	u32     VGASnoopEnable          : 1;	// [5]
	       	u32     ParityErrorResponse     : 1;	// [6]
	       	u32     SteppingControl         : 1;	// [7]
	       	u32     SERREnable              : 1;	// [8]
	       	u32     BackToBackEnable        : 1;	// [9]
	       	u32     Reserved1               : 6;	// [10..15]

	       	u32     Reserved2               : 4;	// [16..19]
	       	u32     CapabilitiesList        : 1;	// [20]
	       	u32     Capable66MHz            : 1;	// [21]
	       	u32     Reserved3               : 1;	// [22]
	       	u32     BackToBackCapable       : 1;	// [23]
	       	u32     MasterParityError       : 1;	// [24]
		u32     DEVSELTiming            : 2;	// [25..26]
		u32     TargetAbortSignalled    : 1;	// [27]
		u32     TargetAbortRcvd         : 1;	// [28]
		u32     MasterAbortRcvd         : 1;	// [29]
		u32     SERRSignalled           : 1;	// [30]
		u32     ParityErrorDetected     : 1;	// [31]
	}x;
	u32	AsDWord;
};


union pci_class_info {
	struct {
		u32     RevisionID      : 8;	// [7..0]
		u32     ProgIF          : 8;	// [15..8]
		u32     SubClassCode    : 8;	// [23..16]
		u32     ClassCode       : 8;	// [31..24]
	}x;
	u32              AsDWord;
};


union pci_header_info {
	struct {
		u32     CacheLineSize   : 8;	// [7..0]
		u32     LatencyTimer    : 8;	// [15..8]
		u32     HeaderType      : 7;	// [22..16]
		u32     MultiFunction   : 1;	// [23]
		u32     BIST            : 8;	// [31..24]
	}x;
	u32              AsDWord;
};

union pci_bus_info {
	struct {
		u32     PrimaryBus      : 8;	// [7..0]
		u32     SecondaryBus    : 8;	// [15..8]
		u32     SubordinateBus  : 8;	// [23..16]
		u32     SecLatencyTimer : 8;	// [31..24]
	}x;
	u32 		AsDWord;
};

union pci_cap_info {
	struct {
		u32     CapabilityID            : 8;	// [7..0]
		u32     PtrToNextCapability     : 8;	// [15..8]
		u32     FeatureSpecific         : 16;	// [31..16]
	}x;
	u32              AsDWord;
};

union pci_cmd_info {
	struct {
		u16     DataParityErrorRecoverEnable    : 1;	// [0]
		u16     EnableRelaxedOrdering           : 1;	// [1]
		u16     MaxMemoryReadByteCount          : 2;	// [3..2]
		u16     MaxOutstandingSplitTransactions : 3;	// [6..4]
		u16     Reserved1                       : 5;	// [11..7]
		u16     ListItemVersion                 : 2;	// [13..12]
		u16     Reserved2                       : 2;	// [15..14]
	}x;
	u16             AsWord;
};

union pcix_status_info {
	struct {
		u32     FunctionNumber                          : 3;	// [2..0]
		u32     DeviceNumber                            : 5;	// [7..3]
		u32     BusNumber                               : 8;	// [15..8]
		u32     Device64Bit                             : 1;	// [16]
		u32     Capable133MHz                           : 1;	// [17]
		u32     SplitCompletionDiscarded                : 1;	// [18]
		u32     UnexpectedSplitCompletion               : 1;	// [19]
		u32     DeviceComplexity                        : 1;	// [20]
		u32     DesignedMaxMemoryReadByteCount          : 2;	// [22..21]
		u32     DesignedMaxOutstandingSplitTransactions : 3;	// [25..23]
		u32     DesignedMaxCumulativeReadSize           : 3;	// [28..26]
		u32     ReceivedSplitCompletionErrorMessage     : 1;	// [29]
		u32     Reserved                                : 2;	// [31..30]
	}x;
	u32              AsDWord;
};


