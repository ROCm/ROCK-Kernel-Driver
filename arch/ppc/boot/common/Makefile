#
# arch/ppc/boot/common/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Tom Rini	January 2001
#

L_TARGET				:= lib.a

obj-y					:= string.o util.o misc-common.o
obj-$(CONFIG_PPC_PREP)			+= mpc10x_memory.o
obj-$(CONFIG_LOPEC)			+= mpc10x_memory.o
obj-$(CONFIG_PAL4)			+= cpc700_memory.o
obj-$(CONFIG_SERIAL_8250_CONSOLE)	+= ns16550.o
