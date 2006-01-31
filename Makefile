CPPFLAGS = -D__LIBHUGETLBFS__
CFLAGS = -O2 -Wall -fPIC -g
LDLIBS =

LIBOBJS = hugeutils.o elflink.o morecore.o debug.o

ARCH = $(shell uname -m)

ifeq ($(ARCH),ppc64)
CC32 = gcc
CC64 = gcc -m64
else
ifeq ($(ARCH),x86_64)
CC32 = gcc -m32
CC64 = gcc -m64
else
CC32 = gcc
endif
endif

ifdef CC32
OBJDIRS += obj32
endif
ifdef CC64
OBJDIRS +=  obj64
endif

ifdef V
VECHO = :
else
VECHO = echo -e "\t"
.SILENT:
endif

DEPFILES = $(LIBOBJS:%.o=%.d)

all:	libs tests

.PHONY:	tests libs

libs:	$(OBJDIRS:%=%/libhugetlbfs.so) $(OBJDIRS:%=%/libhugetlbfs.a)

tests:	libs	# Force make to build the library first
tests:	tests/all

tests/%:
	$(MAKE) -C tests OBJDIRS="$(OBJDIRS)" CC32="$(CC32)" CC64="$(CC64)" $*

check:	all
	./run_tests.sh

checkv:	all
	./run_tests.sh -vV

func:	all
	./run_tests.sh -t func

funcv:	all
	./run_tests.sh -t func -vV

stress:	all
	./run_tests.sh -t stress

stressv: all
	./run_tests.sh -t stress -vV

# Don't want to remake objects just 'cos the directory timestamp changes
$(OBJDIRS): %:
	@mkdir -p $@


.SECONDARY:

obj32/%.o: %.c
	@$(VECHO) CC32 $@
	@mkdir -p obj32
	$(CC32) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

obj64/%.o: %.c
	@$(VECHO) CC64 $@
	@mkdir -p obj64
	$(CC64) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

%/libhugetlbfs.a: $(foreach OBJ,$(LIBOBJS),%/$(OBJ))
	@$(VECHO) AR $@
	$(AR) $(ARFLAGS) $@ $^

obj32/libhugetlbfs.so: $(LIBOBJS:%=obj32/%)
	@$(VECHO) LD32 "(shared)" $@
	$(CC32) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS)

obj64/libhugetlbfs.so: $(LIBOBJS:%=obj64/%)
	@$(VECHO) LD64 "(shared)" $@
	$(CC64) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS)

obj32/%.i:	%.c
	@$(VECHO) CPP $@
	$(CC32) $(CPPFLAGS) -E $< > $@

obj64/%.i:	%.c
	@$(VECHO) CPP $@
	$(CC64) $(CPPFLAGS) -E $< > $@

obj32/%.s:	%.c
	@$(VECHO) CC32 -S $@
	$(CC32) $(CPPFLAGS) $(CFLAGS) -o $@ -S $<

obj64/%.s:	%.c
	@$(VECHO) CC32 -S $@
	$(CC64) $(CPPFLAGS) $(CFLAGS) -o $@ -S $<

clean:
	@$(VECHO) CLEAN
	rm -f *~ *.o *.so *.a *.d *.i core a.out
	rm -rf obj*
	rm -f ldscripts/*~
	$(MAKE) -C tests clean

%.d: %.c
	@$(CC) $(CPPFLAGS) -MM -MT "$(foreach DIR,$(OBJDIRS),$(DIR)/$*.o) $@" $< > $@

-include $(DEPFILES)
