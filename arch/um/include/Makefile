all : sc.h

sc.h : ../util/mk_sc
	../util/mk_sc > $@

../util/mk_sc :
	$(MAKE) -C ../util mk_sc
