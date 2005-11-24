LIB_TESTS = gethugepagesize test_root find_path unlinked_fd \
	readback truncate shared private empty_mounts straddle_4GB \
	huge_at_4GB_normal_below huge_below_4GB_normal_above
PRELOAD_TESTS = malloc malloc_manysmall
TESTS = $(LIB_TESTS) $(PRELOAD_TESTS)

CPPFLAGS = -I..
LDLIBS = -ldl
LDFLAGS32 = -L../obj32
LDFLAGS64 = -L../obj64

DEPFILES = $(TESTS:%=%.d) testutils.d

all:	$(foreach DIR,$(OBJDIRS),$(TESTS:%=$(DIR)/%))

obj32/%.o: %.c
	@mkdir -p obj32
	$(CC32) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

obj64/%.o: %.c
	@mkdir -p obj64
	$(CC64) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

$(LIB_TESTS:%=obj32/%): %: %.o obj32/testutils.o
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ $^ -lhugetlbfs $(LDLIBS)

$(LIB_TESTS:%=obj64/%): %: %.o obj64/testutils.o
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ $^ -lhugetlbfs $(LDLIBS)

$(PRELOAD_TESTS:%=obj32/%): %: %.o obj32/testutils.o
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ $^ $(LDLIBS)

$(PRELOAD_TESTS:%=obj64/%): %: %.o obj64/testutils.o
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ $^ $(LDLIBS)

clean:
	rm -f *~ *.o *.so *.a *.d core a.out
	rm -rf obj*
	rm -f $(TESTS)

%.d: %.c
	$(CC) $(CPPFLAGS) -MM -MT "$*.o $@" $< > $@

-include $(DEPFILES)
