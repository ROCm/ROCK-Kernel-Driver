#
# qc-usb, Logitech QuickCam video driver with V4L support
# Derived from qce-ga, linux V4L driver for the QuickCam Express and Dexxa QuickCam
#
# Makefile - driver build system
#
# Copyright (C) 2003  Tuukka Toivonen
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
#

# Called recursively by 2.6.x kernel build process

EXTRA_CFLAGS += -DNOKERNEL

quickcam-objs 	:= qc-driver.o qc-hdcs.o qc-pb0100.o qc-vv6410.o qc-formats.o qc-mjpeg.o qc-memory.o
obj-$(CONFIG_USB_QC)		+= quickcam.o

