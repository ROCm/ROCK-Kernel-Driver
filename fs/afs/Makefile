#
# Makefile for Red Hat Linux AFS client.
#

kafs-objs := \
	callback.o \
	cell.o \
	cmservice.o \
	dir.o \
	file.o \
	fsclient.o \
	inode.o \
	kafsasyncd.o \
	kafstimod.o \
	main.o \
	misc.o \
	mntpt.o \
	proc.o \
	server.o \
	super.o \
	vlclient.o \
	vlocation.o \
	vnode.o \
	volume.o

#	cache.o

obj-m  := kafs.o

# superfluous for 2.5, but needed for 2.4..
ifeq "$(VERSION).$(PATCHLEVEL)" "2.4"
kafs.o: $(kafs-objs)
	$(LD) -r -o kafs.o $(kafs-objs)
endif

include $(TOPDIR)/Rules.make
