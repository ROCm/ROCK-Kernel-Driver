#
# Makefile for the IBM 4xx on-chip  ethernet drivers
#

# Module support doesn't work yet.
export-objs     := 

obj-$(CONFIG_IBM_OCP_ENET) += ibm_ocp.o

# NB! Link order matters

ibm_ocp-objs := ibm_ocp_mal.o ibm_ocp_enet.o ibm_ocp_phy.o

# These chips have a ZMII
ibm_ocp-$(CONFIG_NP405L)			+= ibm_ocp_zmii.o
ibm_ocp-$(CONFIG_NP405H)			+= ibm_ocp_zmii.o
ibm_ocp-$(CONFIG_44x)				+= ibm_ocp_zmii.o

# Only need this if you want to see additional debug messages
ibm_ocp-$(CONFIG_IBM_OCP_ENET_ERROR_MSG)	+= ibm_ocp_debug.o
