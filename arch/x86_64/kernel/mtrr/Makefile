#
# Reuse the i386 MTRR driver.
#

obj-y		:= main.o if.o generic.o state.o
obj-y		+= amd.o
obj-y		+= cyrix.o
obj-y		+= centaur.o

$(obj)/main.c: $(obj)/mtrr.h
	@ln -sf ../../../i386/kernel/cpu/mtrr/main.c $(obj)/main.c
$(obj)/if.c: $(obj)/mtrr.h
	@ln -sf ../../../i386/kernel/cpu/mtrr/if.c $(obj)/if.c
$(obj)/generic.c: $(obj)/mtrr.h
	@ln -sf ../../../i386/kernel/cpu/mtrr/generic.c $(obj)/generic.c
$(obj)/state.c: $(obj)/mtrr.h
	@ln -sf ../../../i386/kernel/cpu/mtrr/state.c $(obj)/state.c
$(obj)/amd.c: $(obj)/mtrr.h
	@ln -sf ../../../i386/kernel/cpu/mtrr/amd.c $(obj)/amd.c
$(obj)/cyrix.c: $(obj)/mtrr.h
	@ln -sf ../../../i386/kernel/cpu/mtrr/cyrix.c $(obj)/cyrix.c
$(obj)/centaur.c: $(obj)/mtrr.h
	@ln -sf ../../../i386/kernel/cpu/mtrr/centaur.c $(obj)/centaur.c
$(obj)/mtrr.h:
	@ln -sf ../../../i386/kernel/cpu/mtrr/mtrr.h $(obj)/mtrr.h

clean-files += main.c if.c generic.c state.c amd.c cyrix.c centaur.c mtrr.h

