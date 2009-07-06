#
# Copyright (c) 2006 Silicon Graphics, Inc.
# All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it would be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write the Free Software Foundation,
# Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#

EXTRA_CFLAGS += -I$(src)/.. -I$(src)/../linux-2.6
EXTRA_CFLAGS += -I$(srctree)/fs/dmapi

ifeq ($(CONFIG_XFS_DEBUG),y)
	EXTRA_CFLAGS += -g -DDEBUG
endif

obj-$(CONFIG_XFS_DMAPI)		+= xfs_dmapi.o

xfs_dmapi-y			+= xfs_dm.o
