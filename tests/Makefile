LIB_TESTS = gethugepagesize test_root find_path unlinked_fd \
	readback truncate shared private empty_mounts \
	ptrace-write-hugepage
LIB_TESTS_64 = straddle_4GB huge_at_4GB_normal_below \
	huge_below_4GB_normal_above
PRELOAD_TESTS = malloc malloc_manysmall
ELFLINK_TESTS = linkhuge linkhuge_nofd

CFLAGS = -O2 -Wall -g
CPPFLAGS = -I..
LDLIBS = -ldl
LDFLAGS32 = -L../obj32
LDFLAGS64 = -L../obj64

TESTS = $(LIB_TESTS) $(PRELOAD_TESTS) $(ELFLINK_TESTS) \
	$(ELFLINK_TESTS:%=xB.%) $(ELFLINK_TESTS:%=xBDT.%)
TESTS_64 = $(LIB_TESTS_64)

ARCH = $(shell uname -m | sed -e s/i.86/i386/)

ifeq ($(ARCH),ppc64)
SCRIPTS32 = ../ldscripts/elf32ppclinux
SCRIPTS64 = ../ldscripts/elf64ppc
else
ifeq ($(ARCH),ppc)
SCRIPTS32 = ../ldscripts/elf32ppclinux
else
ifeq ($(ARCH),i386)
SCRIPTS32 = ../ldscripts/elf_i386
endif
endif
endif

ifdef V
VECHO = :
else
VECHO = echo -e "\t"
.SILENT:
endif

DEPFILES = $(TESTS:%=%.d) testutils.d

ALLTESTS = $(foreach DIR,$(OBJDIRS),$(TESTS:%=$(DIR)/%))
ifdef CC64
ALLTESTS += $(TESTS_64:%=obj64/%)
endif

all:	$(ALLTESTS)

obj32/%.o: %.c
	@$(VECHO) CC32 $@
	@mkdir -p obj32
	$(CC32) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

obj64/%.o: %.c
	@$(VECHO) CC64 $@
	@mkdir -p obj64
	$(CC64) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

$(LIB_TESTS:%=obj32/%): %: %.o obj32/testutils.o
	@$(VECHO) LD32 "(lib test)" $@
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ $^ -lhugetlbfs $(LDLIBS)

$(LIB_TESTS:%=obj64/%) $(LIB_TESTS_64:%=obj64/%): %: %.o obj64/testutils.o
	@$(VECHO) LD64 "(lib test)" $@
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ $^ -lhugetlbfs $(LDLIBS)

$(PRELOAD_TESTS:%=obj32/%): %: %.o obj32/testutils.o
	@$(VECHO) LD32 "(preload test)" $@
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ $^ $(LDLIBS)

$(PRELOAD_TESTS:%=obj64/%): %: %.o obj64/testutils.o
	@$(VECHO) LD64 "(preload test)" $@
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ $^ $(LDLIBS)

$(ELFLINK_TESTS:%=obj32/%): %: %.o obj32/testutils.o
	@$(VECHO) LD32 "(elflink test)" $@
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ $^ $(LDLIBS)

$(ELFLINK_TESTS:%=obj64/%): %: %.o obj64/testutils.o
	@$(VECHO) LD64 "(elflink test)" $@
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ $^ $(LDLIBS)

obj32/xB.%: $(SCRIPTS32).xB obj32/%.o obj32/testutils.o
	@$(VECHO) LD32 "(xB test)" $@
	@mkdir -p obj32
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ -Wl,-T $^ $(LDLIBS)

obj64/xB.%: $(SCRIPTS64).xB obj64/%.o obj64/testutils.o
	@$(VECHO) LD64 "(xB test)" $@
	@mkdir -p obj64
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ -Wl,-T $^ $(LDLIBS)

obj32/xBDT.%: $(SCRIPTS32).xBDT obj32/%.o obj32/testutils.o
	@$(VECHO) LD32 "(xBDT test)" $@
	@mkdir -p obj32
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ -Wl,-T $^ $(LDLIBS)

obj64/xBDT.%: $(SCRIPTS64).xBDT obj64/%.o obj64/testutils.o
	@$(VECHO) LD64 "(xBDT test)" $@
	@mkdir -p obj64
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ -Wl,-T $^ $(LDLIBS)

clean:
	@$(VECHO) CLEAN "(tests)"
	rm -f *~ *.o *.so *.a *.d core a.out
	rm -rf obj*
	rm -f $(TESTS)

%.d: %.c
	@$(CC) $(CPPFLAGS) -MM -MT "$*.o $@" $< > $@

-include $(DEPFILES)
