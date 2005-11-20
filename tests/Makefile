TESTS = gethugepagesize test_root_hugetlbfs find_path unlinked_fd

CPPFLAGS = -I..
LDFLAGS = -L..
LDLIBS = -lhugetlbfs

DEPFILES = $(TESTS:%=%.d) testutils.d

all:	$(TESTS)

$(TESTS): %: %.o testutils.o

clean:
	rm -f *~ *.o *.so *.a *.d core a.out
	rm -f $(TESTS)

%.d: %.c
	$(CC) -MM -MG -MT "$*.o $@" $< > $@

-include $(DEPFILES)
