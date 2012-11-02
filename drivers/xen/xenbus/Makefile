obj-y += xenbus_client.o xenbus_comms.o xenbus_xs.o xenbus_probe.o
backend-standalone-$(CONFIG_XEN) += xenbus_be.o
obj-$(CONFIG_PARAVIRT_XEN) += xenbus_dev_frontend.o

xenbus_be-objs =
xenbus_be-objs += xenbus_backend_client.o
xenbus_be-objs += xenbus_dev_backend.o

xenbus-$(CONFIG_XEN_BACKEND) += xenbus_probe_backend.o
obj-y += $(xenbus-y) $(xenbus-m)
obj-$(CONFIG_XEN_XENBUS_DEV) += xenbus_dev.o

obj-$(CONFIG_PARAVIRT_XEN_BACKEND) += xenbus_probe_backend.o
backend-standalone-$(CONFIG_PARAVIRT_XEN) += xenbus_dev_backend.o
obj-$(CONFIG_XEN_XENBUS_FRONTEND) += xenbus_probe_frontend.o

obj-$(CONFIG_XEN_BACKEND) += $(backend-standalone-y)
