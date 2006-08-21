PREFIX = /usr/local

LIBOBJS = hugeutils.o elflink.o morecore.o debug.o
SBINOBJS = hugetlbd
INSTALL_OBJ_LIBS = libhugetlbfs.so libhugetlbfs.a
LDSCRIPT_TYPES = B BDT
INSTALL_OBJSCRIPT = ld.hugetlbfs

INSTALL = install

LDFLAGS = --no-undefined-version --version-script=VERSION
CFLAGS = -O2 -Wall -fPIC -g
CPPFLAGS = -D__LIBHUGETLBFS__

ARCH = $(shell uname -m | sed -e s/i.86/i386/)

ifeq ($(ARCH),ppc64)
CC64 = gcc -m64
ELF64 = elf64ppc
LIB64 = lib64
LIB32 = lib
ifneq ($(BUILDTYPE),NATIVEONLY)
CC32 = gcc
ELF32 = elf32ppclinux
endif
else
ifeq ($(ARCH),ppc)
CC32 = gcc
ELF32 = elf32ppclinux
LIB32 = lib
else
ifeq ($(ARCH),i386)
CC32 = gcc
ELF32 = elf_i386
LIB32 = lib
else
ifeq ($(ARCH),x86_64)
CC64 = gcc -m64
ELF64 = elf_x86_64
LIB64 = lib64
LIB32 = lib
ifneq ($(BUILDTYPE),NATIVEONLY)
CC32 = gcc -m32
ELF32 = elf_i386
endif
endif
endif
endif
endif

ifdef CC32
OBJDIRS += obj32
endif
ifdef CC64
OBJDIRS +=  obj64
endif

LIBDIR32 = $(DESTDIR)$(PREFIX)/$(LIB32)
LIBDIR64 = $(DESTDIR)$(PREFIX)/$(LIB64)
LDSCRIPTDIR = $(PREFIX)/$(LIB32)/ldscripts
BINDIR = $(DESTDIR)$(PREFIX)/bin
SBINDIR = $(DESTDIR)$(PREFIX)/sbin
DOCDIR = $(DESTDIR)$(PREFIX)/share/doc/libhugetlbfs

EXTRA_DIST = \
	README \
	HOWTO \
	LGPL-2.1

ifdef CC32
INSTALL_LDSCRIPTS = $(foreach type,$(LDSCRIPT_TYPES),$(ELF32).x$(type))
endif
ifdef CC64
INSTALL_LDSCRIPTS += $(foreach type,$(LDSCRIPT_TYPES),$(ELF64).x$(type))
endif


ifdef V
VECHO = :
else
VECHO = echo -e "\t"
.SILENT:
endif

DEPFILES = $(LIBOBJS:%.o=%.d)

all:	libs sbin tests

.PHONY:	tests libs

libs:	$(foreach file,$(INSTALL_OBJ_LIBS),$(OBJDIRS:%=%/$(file)))
sbin:	$(foreach file,$(SBINOBJS),$(OBJDIRS:%=%/$(file)))

tests:	libs sbin	# Force make to build the library first
tests:	tests/all

tests/%:
	$(MAKE) -C tests OBJDIRS="$(OBJDIRS)" CC32="$(CC32)" CC64="$(CC64)" ELF32="$(ELF32)" ELF64="$(ELF64)" $*

check:	all
	cd tests; ./run_tests.sh

checkv:	all
	cd tests; ./run_tests.sh -vV

func:	all
	cd tests; ./run_tests.sh -t func

funcv:	all
	cd tests; ./run_tests.sh -t func -vV

stress:	all
	cd tests; ./run_tests.sh -t stress

stressv: all
	cd tests; ./run_tests.sh -t stress -vV

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
	@$(VECHO) CC64 -S $@
	$(CC64) $(CPPFLAGS) $(CFLAGS) -o $@ -S $<

obj32/hugetlbd:	hugetlbd.c $(LIBOBJS:%=obj32/%)
	@$(VECHO) CC32 $@
	$(CC32) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS32) -o $@ obj32/hugeutils.o $<

obj64/hugetlbd:	hugetlbd.c $(LIBOBJS:%=obj64/%)
	@$(VECHO) CC64 $@
	$(CC64) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS64) -o $@ obj64/hugeutils.o $<

clean:
	@$(VECHO) CLEAN
	rm -f *~ *.o *.so *.a *.d *.i core a.out
	rm -rf obj*
	rm -f ldscripts/*~
	rm -f libhugetlbfs-sock
	$(MAKE) -C tests clean

%.d: %.c
	@$(CC) $(CPPFLAGS) -MM -MT "$(foreach DIR,$(OBJDIRS),$(DIR)/$*.o) $@" $< > $@

-include $(DEPFILES)

obj32/install:
	@$(VECHO) INSTALL32 $(LIBDIR32)
	$(INSTALL) -d $(LIBDIR32)
	$(INSTALL) $(INSTALL_OBJ_LIBS:%=obj32/%) $(LIBDIR32)
	$(INSTALL) -d $(SBINDIR)
	for x in $(SBINOBJS); do $(INSTALL) obj32/$$x $(SBINDIR)/$$x; done

obj64/install:
	@$(VECHO) INSTALL64 $(LIBDIR64)
	$(INSTALL) -d $(LIBDIR64)
	$(INSTALL) $(INSTALL_OBJ_LIBS:%=obj64/%) $(LIBDIR64)
	$(INSTALL) -d $(SBINDIR)
	for x in $(SBINOBJS); do $(INSTALL) obj64/$$x $(SBINDIR)/$$x; done

objscript.%: %
	@$(VECHO) OBJSCRIPT $*
	sed "s!### SET DEFAULT LDSCRIPT PATH HERE ###!HUGETLB_LDSCRIPT_PATH=$(LDSCRIPTDIR)!" < $< > $@

install: all $(OBJDIRS:%=%/install) $(INSTALL_OBJSCRIPT:%=objscript.%)
	@$(VECHO) INSTALL
	$(INSTALL) -d $(DESTDIR)$(LDSCRIPTDIR)
	$(INSTALL) -m 644 $(INSTALL_LDSCRIPTS:%=ldscripts/%) $(DESTDIR)$(LDSCRIPTDIR)
	$(INSTALL) -d $(BINDIR)
	for x in $(INSTALL_OBJSCRIPT); do \
		$(INSTALL) -m 755 objscript.$$x $(BINDIR)/$$x; done

install-docs:
	$(INSTALL) -d $(DOCDIR)
	for x in $(EXTRA_DIST); do $(INSTALL) -m 755 $$x $(DOCDIR)/$$x; done

install-tests: install	# Force make to install the library first
	${MAKE} -C tests install DESTDIR=$(DESTDIR) OBJDIRS="$(OBJDIRS)" LIB32=$(LIB32) LIB64=$(LIB64)
