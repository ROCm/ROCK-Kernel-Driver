
host-progs	:= genksyms
always		:= $(host-progs)

genksyms-objs	:= genksyms.o parse.o lex.o

HOSTCFLAGS_parse.o := -Wno-uninitialized

# dependencies on generated files need to be listed explicitly

$(obj)/lex.o: $(obj)/parse.h $(obj)/keywords.c


ifdef GENERATE_PARSER

# gperf

quiet_cmd_keywords.c = GPERF   $@
      cmd_keywords.c = gperf -L ANSI-C -a -C -E -g -H is_reserved_hash	\
		       -k 1,3,$$ -N is_reserved_word -p -t $< > $@

$(obj)/keywords.c: $(obj)/keywords.gperf FORCE
	$(call if_changed,keywords.c)

# flex

quiet_cmd_lex.c = FLEX    $@
      cmd_lex.c = flex -o$@ -d $(filter-out FORCE,$^)

$(obj)/lex.c: $(obj)/lex.l $(obj)/parse.h FORCE
	$(call if_changed,lex.c)

# bison

quiet_cmd_parse.c = BISON   $@
      cmd_parse.c = bison -o$@ -dtv $(filter-out FORCE,$^)

$(obj)/parse.c: $(obj)/parse.y FORCE
	$(call if_changed,parse.c)

$(obj)/parse.h: $(obj)/parse.c ;

clean-files	+= parse.output

endif

targets += $(obj)/keywords.c $(obj)/lex.c $(obj)/parse.c $(obj)/parse.h
