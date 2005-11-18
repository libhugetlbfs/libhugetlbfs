LIBOBJS = hugeutils.o morecore.o

CFLAGS = -Wall -fPIC

DEPFILES = $(LIBOBJS:%.o=%.d)

all:	libhugetlbfs.so libhugetlbfs.a alltests

test:	all
	./run_tests.sh

alltests:
	$(MAKE) -C tests all

libhugetlbfs.a: $(LIBOBJS)
	$(AR) $(ARFLAGS) $@ $^

libhugetlbfs.so: $(LIBOBJS)
	$(LINK.c) -shared -fPIC -o $@ $^

clean:
	rm -f *~ *.o *.so *.a *.d core a.out
	$(MAKE) -C tests clean

%.d: %.c
	$(CC) -MM -MG -MT "$*.o $@" $< > $@

-include $(DEPFILES)
