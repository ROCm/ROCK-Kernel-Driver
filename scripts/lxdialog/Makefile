HOST_EXTRACFLAGS := -DLOCALE 
HOST_LOADLIBES   := -lncurses

ifeq (/usr/include/ncurses/ncurses.h, $(wildcard /usr/include/ncurses/ncurses.h))
        HOST_EXTRACFLAGS += -I/usr/include/ncurses -DCURSES_LOC="<ncurses.h>"
else
ifeq (/usr/include/ncurses/curses.h, $(wildcard /usr/include/ncurses/curses.h))
        HOST_EXTRACFLAGS += -I/usr/include/ncurses -DCURSES_LOC="<ncurses/curses.h>"
else
ifeq (/usr/include/ncurses.h, $(wildcard /usr/include/ncurses.h))
        HOST_EXTRACFLAGS += -DCURSES_LOC="<ncurses.h>"
else
	HOST_EXTRACFLAGS += -DCURSES_LOC="<curses.h>"
endif
endif
endif

host-progs	:= lxdialog
always		:= $(host-progs)

lxdialog-objs := checklist.o menubox.o textbox.o yesno.o inputbox.o \
		 util.o lxdialog.o msgbox.o

first_rule: ncurses

.PHONY: ncurses
ncurses:
	@echo "main() {}" > lxtemp.c
	@if $(HOSTCC) lxtemp.c  $(HOST_LOADLIBES); then \
		rm -f lxtemp.c a.out; \
	else \
		rm -f lxtemp.c; \
		echo -e "\007" ;\
		echo ">> Unable to find the Ncurses libraries." ;\
		echo ">>" ;\
		echo ">> You must have Ncurses installed in order" ;\
		echo ">> to use 'make menuconfig'" ;\
		echo ;\
		exit 1 ;\
	fi
