OBJ = built-in.o

OBJS =

all: $(OBJ)

$(OBJ): $(OBJS)
	rm -f $@
	$(LD) $(LINKFLAGS) --start-group $^ --end-group -o $@
clean:
	rm -f $(OBJS)

fastdep:

archmrproper:

archclean:
	rm -f link.ld
	@$(MAKEBOOT) clean

archdep:
	@$(MAKEBOOT) dep

modules:
