#define MPTDIAGREGISTER				_IOWR(MPT_MAGIC_NUMBER,26,mpt_diag_register_t)
#define MPTDIAGRELEASE				_IOWR(MPT_MAGIC_NUMBER,27,mpt_diag_release_t)
#define MPTDIAGUNREGISTER			_IOWR(MPT_MAGIC_NUMBER,28,mpt_diag_unregister_t)
#define MPTDIAGQUERY				_IOWR(MPT_MAGIC_NUMBER,29,mpt_diag_query_t)
#define MPTDIAGREADBUFFER			_IOWR(MPT_MAGIC_NUMBER,30,mpt_diag_read_buffer_t)

#define MPI_FW_DIAG_IOCTL			(0x80646961)
#define MPI_FW_DIAG_TYPE_REGISTER		(0x00000001)
#define MPI_FW_DIAG_TYPE_UNREGISTER		(0x00000002)
#define MPI_FW_DIAG_TYPE_QUERY			(0x00000003)
#define MPI_FW_DIAG_TYPE_READ_BUFFER		(0x00000004)
#define MPI_FW_DIAG_TYPE_RELEASE		(0x00000005)

#define MPI_FW_DIAG_INVALID_UID			(0x00000000)
#define FW_DIAGNOSTIC_BUFFER_COUNT		(3)
#define FW_DIAGNOSTIC_UID_NOT_FOUND		(0xFF)

#define MPI_FW_DIAG_ERROR_SUCCESS		(0x00000000)
#define MPI_FW_DIAG_ERROR_FAILURE		(0x00000001)
#define MPI_FW_DIAG_ERROR_INVALID_PARAMETER	(0x00000002)
#define MPI_FW_DIAG_ERROR_POST_FAILED		(0x00000010)
#define MPI_FW_DIAG_ERROR_INVALID_UID		(0x00000011)

#define MPI_FW_DIAG_ERROR_RELEASE_FAILED	(0x00000012)
#define MPI_FW_DIAG_ERROR_NO_BUFFER		(0x00000013)
#define MPI_FW_DIAG_ERROR_ALREADY_RELEASED	(0x00000014)

#define MPT_DIAG_CAPABILITY(bufftype)	(MPI_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER << bufftype)

#define MPT_DIAG_BUFFER_IS_REGISTERED 		1
#define MPT_DIAG_BUFFER_IS_RELEASED 		2

typedef struct _MPI_FW_DIAG_REGISTER {
	u8			TraceLevel;
	u8			BufferType;
	u16			Flags;
	u32			ExtendedType;
	u32			ProductSpecific[4];
	u32			RequestedBufferSize;
	u32			UniqueId;
} MPI_FW_DIAG_REGISTER, *PTR_MPI_FW_DIAG_REGISTER;

typedef struct _mpt_diag_register {
	mpt_ioctl_header	hdr;
	MPI_FW_DIAG_REGISTER	data;
} mpt_diag_register_t;

typedef struct _MPI_FW_DIAG_UNREGISTER {
	u32			UniqueId;
} MPI_FW_DIAG_UNREGISTER, *PTR_MPI_FW_DIAG_UNREGISTER;

typedef struct _mpt_diag_unregister {
	mpt_ioctl_header	 hdr;
	MPI_FW_DIAG_UNREGISTER	data;
} mpt_diag_unregister_t;

#define MPI_FW_DIAG_FLAG_APP_OWNED          (0x0001)
#define MPI_FW_DIAG_FLAG_BUFFER_VALID       (0x0002)
#define MPI_FW_DIAG_FLAG_FW_BUFFER_ACCESS   (0x0004)

typedef struct _MPI_FW_DIAG_QUERY {
	u8			TraceLevel;
	u8			BufferType;
	u16			Flags;
	u32			ExtendedType;
	u32			ProductSpecific[4];
	u32			DataSize;
	u32			DriverAddedBufferSize;
	u32			UniqueId;
} MPI_FW_DIAG_QUERY, *PTR_MPI_FW_DIAG_QUERY;

typedef struct _mpt_diag_query {
	mpt_ioctl_header	hdr;
	MPI_FW_DIAG_QUERY	data;
} mpt_diag_query_t;

typedef struct _MPI_FW_DIAG_RELEASE {
	u32			UniqueId;
} MPI_FW_DIAG_RELEASE, *PTR_MPI_FW_DIAG_RELEASE;

typedef struct _mpt_diag_release {
	mpt_ioctl_header	hdr;
	MPI_FW_DIAG_RELEASE	data;
} mpt_diag_release_t;

#define MPI_FW_DIAG_FLAG_REREGISTER         (0x0001)

typedef struct _MPI_FW_DIAG_READ_BUFFER {
	u8			Status;
	u8			Reserved;
	u16			Flags;
	u32			StartingOffset;
	u32			BytesToRead;
	u32			UniqueId;
	u32			DiagnosticData[1];
} MPI_FW_DIAG_READ_BUFFER, *PTR_MPI_FW_DIAG_READ_BUFFER;

typedef struct _mpt_diag_read_buffer {
	mpt_ioctl_header	hdr;
	MPI_FW_DIAG_READ_BUFFER	data;
} mpt_diag_read_buffer_t;
