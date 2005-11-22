LIB_TESTS = gethugepagesize test_root find_path unlinked_fd \
	readback truncate shared private empty_mounts
PRELOAD_TESTS = malloc malloc_manysmall
TESTS = $(LIB_TESTS) $(PRELOAD_TESTS)

CPPFLAGS = -I..
LDFLAGS = -L..
LDLIBS = -ldl

DEPFILES = $(TESTS:%=%.d) testutils.d

all:	$(TESTS)

$(LIB_TESTS): %: %.o testutils.o
	$(LINK.c) -o $@ $^ -lhugetlbfs

$(PRELOAD_TESTS): %: %.o testutils.o
	$(LINK.c) -o $@ $^

clean:
	rm -f *~ *.o *.so *.a *.d core a.out
	rm -f $(TESTS)

%.d: %.c
	$(CC) $(CPPFLAGS) -MM -MT "$*.o $@" $< > $@

-include $(DEPFILES)
