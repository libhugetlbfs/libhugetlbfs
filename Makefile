LIBOBJS = hugeutils.o morecore.o debug.o

CPPFLAGS = -D__LIBHUGETLBFS__
CFLAGS = -Wall -fPIC
LDLIBS = -ldl

ARCH = $(shell uname -m)

ifeq ($(ARCH),ppc64)
CC32 = gcc
CC64 = gcc -m64
else
CC32 = gcc
endif

ifdef CC32
OBJDIRS += obj32
endif
ifdef CC64
OBJDIRS +=  obj64
endif

DEPFILES = $(LIBOBJS:%.o=%.d)

all:	libs tests

libs:	$(OBJDIRS:%=%/libhugetlbfs.so) $(OBJDIRS:%=%/libhugetlbfs.a)

.PHONY:	tests
tests:	tests/all

tests/%:
	$(MAKE) -C tests OBJDIRS="$(OBJDIRS)" CC32="$(CC32)" CC64="$(CC64)" $*

check:	all
	./run_tests.sh

checkv:	all
	./run_tests.sh -v -V

# Don't want to remake objects just 'cos the directory timestamp changes
$(OBJDIRS): %:
	@mkdir -p $@


.SECONDARY:

obj32/%.o: %.c
	@mkdir -p obj32
	$(CC32) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

obj64/%.o: %.c
	@mkdir -p obj64
	$(CC64) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

%/libhugetlbfs.a: $(foreach OBJ,$(LIBOBJS),%/$(OBJ))
	$(AR) $(ARFLAGS) $@ $^

obj32/libhugetlbfs.so: $(LIBOBJS:%=obj32/%)
	$(CC32) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS)

obj64/libhugetlbfs.so: $(LIBOBJS:%=obj64/%)
	$(CC64) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS)

%.i:	%.c
	$(CC) $(CPPFLAGS) -E $< > $@

clean:
	rm -f *~ *.o *.so *.a *.d *.i core a.out
	rm -rf obj*
	$(MAKE) -C tests clean

%.d: %.c
	$(CC) $(CPPFLAGS) -MM -MT "$(foreach DIR,$(OBJDIRS),$(DIR)/$*.o) $@" $< > $@

-include $(DEPFILES)
