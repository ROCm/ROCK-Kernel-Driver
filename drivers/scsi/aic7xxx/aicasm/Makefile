PROG=	aicasm

.SUFFIXES= .l .y .c

CSRCS=	aicasm.c aicasm_symbol.c
GENSRCS= aicasm_gram.c aicasm_scan.c
GENHDRS= y.tab.h aicdb.h

SRCS=	${GENSRCS} ${CSRCS}
CLEANFILES= ${GENSRCS} ${GENHDRS} y.output
# Override default kernel CFLAGS.  This is a userland app.
AICASM_CFLAGS:= -I/usr/include -I. -ldb
YFLAGS= -d

NOMAN=	noman

ifneq ($(HOSTCC),)
AICASM_CC= $(HOSTCC)
else
AICASM_CC= $(CC)
endif

ifdef DEBUG
CFLAGS+= -DDEBUG -g
YFLAGS+= -t -v
LFLAGS= -d
endif

$(PROG):  $(SRCS) $(GENHDRS)
	$(AICASM_CC) $(AICASM_CFLAGS) $(SRCS) -o $(PROG)

aicdb.h:
	@if [ -e "/usr/include/db3/db_185.h" ]; then		\
		echo "#include <db3/db_185.h>" > aicdb.h;	\
	 elif [ -e "/usr/include/db2/db_185.h" ]; then		\
		echo "#include <db2/db_185.h>" > aicdb.h;	\
	 elif [ -e "/usr/include/db/db_185.h" ]; then		\
		echo "#include <db/db_185.h>" > aicdb.h;	\
	 elif [ -e "/usr/include/db_185.h" ]; then		\
		echo "#include <db_185.h>" > aicdb.h;		\
	 else							\
		echo "*** Install db development libraries";	\
	 fi

clean:
	rm -f $(CLEANFILES) $(PROG)

y.tab.h aicasm_gram.c: aicasm_gram.y
	$(YACC) $(YFLAGS) aicasm_gram.y
	mv y.tab.c aicasm_gram.c

aicasm_scan.c: y.tab.h
