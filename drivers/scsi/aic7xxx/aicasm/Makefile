PROG=	aicasm

CSRCS=	aicasm.c aicasm_symbol.c
GENSRCS= aicasm_gram.c aicasm_scan.c

GENHDRS=	y.tab.h

SRCS=	${GENSRCS} ${CSRCS}
CLEANFILES= ${GENSRCS} ${GENHDRS} y.output
# Override default kernel CFLAGS.  This is a userland app.
AICASM_CFLAGS:= -I/usr/include -ldb1
YFLAGS= -d

NOMAN=	noman

ifdef DEBUG
CFLAGS+= -DDEBUG -g
YFLAGS+= -t -v
LFLAGS= -d
endif

.SUFFIXES= .l .y .c

$(PROG): $(SRCS)
	$(CC) $(AICASM_CFLAGS) $(SRCS) -o $(PROG)

clean:
	rm -f $(CLEANFILES) $(PROG)
