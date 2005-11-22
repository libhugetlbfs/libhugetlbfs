LIBOBJS = hugeutils.o morecore.o debug.o

CPPFLAGS = -D__LIBHUGETLBFS__
CFLAGS = -Wall -fPIC
LDLIBS = -ldl

DEPFILES = $(LIBOBJS:%.o=%.d)

all:	libhugetlbfs.so libhugetlbfs.a tests

check:	all
	./run_tests.sh

checkv:	all
	./run_tests.sh -v -V

.PHONY:	tests
tests:
	$(MAKE) -C tests all

libhugetlbfs.a: $(LIBOBJS)
	$(AR) $(ARFLAGS) $@ $^

libhugetlbfs.so: $(LIBOBJS)
	$(LINK.c) -shared -o $@ $^ $(LDLIBS)

%.i:	%.c
	$(CC) $(CPPFLAGS) -E $< > $@

clean:
	rm -f *~ *.o *.so *.a *.d *.i core a.out
	$(MAKE) -C tests clean

%.d: %.c
	$(CC) $(CPPFLAGS) -MM -MT "$*.o $@" $< > $@

-include $(DEPFILES)
