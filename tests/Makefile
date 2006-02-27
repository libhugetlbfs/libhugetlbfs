LIB_TESTS = gethugepagesize test_root find_path unlinked_fd \
	readback truncate shared private empty_mounts meminfo_nohuge \
	ptrace-write-hugepage icache-hygeine slbpacaflush \
	chunk-overcommit
LIB_TESTS_64 = straddle_4GB huge_at_4GB_normal_below \
	huge_below_4GB_normal_above
NOLIB_TESTS = malloc malloc_manysmall dummy
LDSCRIPT_TESTS = zero_filesize_segment
HUGELINK_TESTS = linkhuge linkhuge_nofd
STRESS_TESTS = mmap-gettest mmap-cow shm-gettest shm-getraw shm-fork

CFLAGS = -O2 -Wall -g
CPPFLAGS = -I..
LDLIBS = -ldl -lpthread
LDFLAGS32 = -L../obj32
LDFLAGS64 = -L../obj64

TESTS = $(LIB_TESTS) $(NOLIB_TESTS) $(LDSCRIPT_TESTS) $(HUGELINK_TESTS) \
	$(HUGELINK_TESTS:%=xB.%) $(HUGELINK_TESTS:%=xBDT.%) \
	$(STRESS_TESTS)

TESTS_64 = $(LIB_TESTS_64)

ARCH = $(shell uname -m | sed -e s/i.86/i386/)

SCRIPTS=../ldscripts
SCRIPTS32 = $(SCRIPTS)/$(ELF32)
SCRIPTS64 = $(SCRIPTS)/$(ELF64)
HUGETLBFS_LD=../ld.hugetlbfs

ifdef V
VECHO = :
else
VECHO = echo -e "\t"
.SILENT:
endif

DEPFILES = $(LIB_TESTS:%=%.d) $(NOLIB_TESTS:%=%.d) $(HUGELINK_TESTS:%=%.d) \
	testutils.d

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
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ $^ $(LDLIBS) -lhugetlbfs 

$(LIB_TESTS:%=obj64/%) $(LIB_TESTS_64:%=obj64/%): %: %.o obj64/testutils.o
	@$(VECHO) LD64 "(lib test)" $@
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ $^ $(LDLIBS) -lhugetlbfs 

$(NOLIB_TESTS:%=obj32/%): %: %.o obj32/testutils.o
	@$(VECHO) LD32 "(nolib test)" $@
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ $^ $(LDLIBS)

$(NOLIB_TESTS:%=obj64/%): %: %.o obj64/testutils.o
	@$(VECHO) LD64 "(nolib test)" $@
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ $^ $(LDLIBS)

$(LDSCRIPT_TESTS:%=obj32/%): obj32/%: %.ld
	@$(VECHO) LD32 "(preload test)" $@
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ -Lobj32 $^ $(LDLIBS)

$(LDSCRIPT_TESTS:%=obj64/%): obj64/%: %.ld
	@$(VECHO) LD64 "(preload test)" $@
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ -Lobj64 $^ $(LDLIBS)

$(HUGELINK_TESTS:%=obj32/%): %: %.o obj32/testutils.o
	@$(VECHO) LD32 "(hugelink test)" $@
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ $^ $(LDLIBS)

$(HUGELINK_TESTS:%=obj64/%): %: %.o obj64/testutils.o
	@$(VECHO) LD64 "(hugelink test)" $@
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ $^ $(LDLIBS)

$(STRESS_TESTS:%=obj32/%): %: %.o obj32/testutils.o
	@$(VECHO) LD32 "(lib test)" $@
	$(CC32) $(LDFLAGS) $(LDFLAGS32) -o $@ $^ $(LDLIBS) -lhugetlbfs 

$(STRESS_TESTS:%=obj64/%): %: %.o obj64/testutils.o
	@$(VECHO) LD64 "(lib test)" $@
	$(CC64) $(LDFLAGS) $(LDFLAGS64) -o $@ $^ $(LDLIBS) -lhugetlbfs 

obj32/xB.%: $(SCRIPTS32).xB $(HUGETLBFS_LD) obj32/%.o obj32/testutils.o
	@$(VECHO) LD32 "(xB test)" $@
	@mkdir -p obj32
	@ln -sf ../$(HUGETLBFS_LD) obj32/ld
	$(CC32) -B./obj32  $(LDFLAGS) $(LDFLAGS32) -o $@ $(LDLIBS) -Wl,--hugetlbfs-link=B $(filter %.o,$^)

obj64/xB.%: $(SCRIPTS64).xB $(HUGETLBFS_LD) obj64/%.o obj64/testutils.o
	@$(VECHO) LD64 "(xB test)" $@
	@mkdir -p obj64
	@ln -sf ../$(HUGETLBFS_LD) obj64/ld
	$(CC64) -B./obj64 $(LDFLAGS) $(LDFLAGS64) -o $@ $(LDLIBS) -Wl,--hugetlbfs-link=B $(filter %.o,$^)

obj32/xBDT.%: $(SCRIPTS32).xBDT $(HUGETLBFS_LD) obj32/%.o obj32/testutils.o
	@$(VECHO) LD32 "(xBDT test)" $@
	@mkdir -p obj32
	@ln -sf ../$(HUGETLBFS_LD) obj32/ld
	$(CC32) -B./obj32 $(LDFLAGS) $(LDFLAGS32) -o $@ $(LDLIBS) -Wl,--hugetlbfs-link=BDT $(filter %.o,$^)

obj64/xBDT.%: $(SCRIPTS64).xBDT $(HUGETLBFS_LD) obj64/%.o obj64/testutils.o
	@$(VECHO) LD64 "(xBDT test)" $@
	@mkdir -p obj64
	@ln -sf ../$(HUGETLBFS_LD) obj64/ld
	$(CC64) -B./obj64 $(LDFLAGS) $(LDFLAGS64) -o $@ $(LDLIBS) -Wl,--hugetlbfs-link=BDT $(filter %.o,$^)

clean:
	@$(VECHO) CLEAN "(tests)"
	rm -f *~ *.o *.so *.a *.d core a.out
	rm -rf obj*
	rm -f $(TESTS)

%.d: %.c
	@$(CC) $(CPPFLAGS) -MM -MT "$(foreach DIR,$(OBJDIRS),$(DIR)/$*.o) $@" $< > $@

-include $(DEPFILES)
