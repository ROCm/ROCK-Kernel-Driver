
.SUFFIXES= .l .y .c

GENSRCS= parser.code.c
GENHDRS= y.t.h

SRCS=	${GENSRCS} ${CSRCS}
CLEANFILES= ${GENSRCS} ${GENHDRS} y.output

# Override default kernel CFLAGS.  This is a userland app.
# PARS_CFLAGS:= -I/usr/include -I. -ldb
YFLAGS= -d -t -v -r -b parser


YACC=./yacc

#ifdef DEBUG
CFLAGS+= -DDEBUG -g
YFLAGS+= -t -v -r -b parser
LFLAGS= -d
#endif

#$(PROG):  $(SRCS) $(GENHDRS)
#	$(CC) $(PARS_CFLAGS) $(CFLAGS)  $(CSRCS) -o $(PROG)
#	$(CC) $(PARS_CFLAGS) $(CFLAGS)  $(SRCS) -o $(PROG)

clean:
	rm -f $(CLEANFILES) $(PROG)

parser.code.c: parser.y
	$(YACC) $(YFLAGS) parser.y




