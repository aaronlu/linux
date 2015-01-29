CC		= $(CROSS_COMPILE)gcc
BUILD_OUTPUT	:= $(PWD)
PREFIX		:= /usr
DESTDIR		:=

turbostat : turbostat.o resolve.o
	@mkdir -p $(BUILD_OUTPUT)
	$(CC) $(LDFLAGS) $^ -o $(BUILD_OUTPUT)/$@

CFLAGS +=	-Wall
CFLAGS +=	-DMSRHEADER='"../../../../arch/x86/include/uapi/asm/msr-index.h"'

%.o: %.c
	@mkdir -p $(BUILD_OUTPUT)
	$(CC) $(CFLAGS) $< -c -o $(BUILD_OUTPUT)/$@

.PHONY : clean
clean :
	@rm -f $(BUILD_OUTPUT)/turbostat turbostat.o resolve.o

install : turbostat
	install -d  $(DESTDIR)$(PREFIX)/bin
	install $(BUILD_OUTPUT)/turbostat $(DESTDIR)$(PREFIX)/bin/turbostat
	install -d  $(DESTDIR)$(PREFIX)/share/man/man8
	install turbostat.8 $(DESTDIR)$(PREFIX)/share/man/man8
