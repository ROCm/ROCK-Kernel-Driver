EXTRA_CFLAGS += -Idrivers/infiniband/include -DTS_KERNEL_2_6

obj-m += ib_client_query.o ib_sa_client.o ib_dm_client.o

ib_client_query-objs := \
    client_query.o \
    client_query_export.o \
    client_query_main.o

ib_sa_client-objs := \
    sa_client_main.o \
    sa_client_query.o \
    sa_client_path_record.o \
    sa_client_multicast.o \
    sa_client_port_info.o \
    sa_client_inform.o \
    sa_client_notice.o \
    sa_client_service.o \
    sa_client_export.o \
    sa_client_node_info.o

ib_dm_client-objs := \
    dm_client_main.o \
    dm_client_query.o \
    dm_client_class_port_info.o \
    dm_client_async_notify.o \
    dm_client_iou_info.o \
    dm_client_ioc_profile.o \
    dm_client_svc_entries.o \
    dm_client_host.o \
    dm_client_export.o
