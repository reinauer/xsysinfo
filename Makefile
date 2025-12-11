# xSysInfo Makefile for GCC (m68k-amigaos)

CC = m68k-amigaos-gcc
STRIP = m68k-amigaos-strip

# Include paths: our includes + identify.library reference includes
IDENTIFY_INC = 3rdparty/identify/reference

# Include paths: our includes + identify.library reference includes
IDENTIFY_INC = 3rdparty/identify/reference

CFLAGS = -O2 -m68000 -mtune=68020-60 -Wa,-m68881 -msoft-float -noixemul -Wall -Wextra \
         -I$(IDENTIFY_INC)

LDFLAGS = -noixemul
LIBS = -lamiga -lgcc

# Source files
SRCS = src/main.c \
       src/gui.c \
       src/hardware.c \
       src/benchmark.c \
       src/dhry_1.c \
       src/dhry_2.c \
       src/memory.c \
       src/drives.c \
       src/scsi.c \
       src/boards.c \
       src/software.c \
       src/cache.c \
       src/print.c \
       src/locale.c

OBJS = $(SRCS:.c=.o)

TARGET = xsysinfo

.PHONY: all clean flexcat identify

all: identify $(TARGET)

# FlexCat build
flexcat:
	$(MAKE) -C 3rdparty/flexcat bootstrap
	$(MAKE)	-C 3rdparty/flexcat

# Identify library build (requires FlexCat)
identify: flexcat
	export PATH="$(CURDIR)/3rdparty/flexcat/src/bin_unix:$(PATH)" && \
	$(MAKE) -C 3rdparty/identify reference/proto/identify.h reference/inline/identify.h

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	$(STRIP) $@

src/%.o: src/%.c src/xsysinfo.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
	$(MAKE) -C 3rdparty/flexcat clean
	$(MAKE) -C 3rdparty/identify clean

# Dependencies
src/main.o: src/main.c src/xsysinfo.h src/gui.h src/hardware.h src/locale_str.h
src/gui.o: src/gui.c src/xsysinfo.h src/gui.h src/hardware.h src/benchmark.h src/locale_str.h
src/hardware.o: src/hardware.c src/xsysinfo.h src/hardware.h
src/benchmark.o: src/benchmark.c src/xsysinfo.h src/benchmark.h
src/memory.o: src/memory.c src/xsysinfo.h src/memory.h src/locale_str.h
src/drives.o: src/drives.c src/xsysinfo.h src/drives.h src/scsi.h src/locale_str.h
src/scsi.o: src/scsi.c src/xsysinfo.h src/scsi.h src/gui.h src/locale_str.h
src/boards.o: src/boards.c src/xsysinfo.h src/boards.h src/locale_str.h
src/software.o: src/software.c src/xsysinfo.h src/software.h
src/cache.o: src/cache.c src/xsysinfo.h src/cache.h
src/print.o: src/print.c src/xsysinfo.h src/print.h src/hardware.h src/software.h
src/locale.o: src/locale.c src/xsysinfo.h src/locale_str.h
src/dhry_1.o: src/dhry_1.c src/dhry.h
src/dhry_2.o: src/dhry_2.c src/dhry.h

# Disk creation
DISK = xsysinfo.adf

# Downloads directory and files
DOWNLOAD_DIR = downloads
IDENTIFY_USR_LHA = $(DOWNLOAD_DIR)/IdentifyUsr.lha
IDENTIFY_PCI_LHA = $(DOWNLOAD_DIR)/IdentifyPci.lha
OPENPCI_LHA = $(DOWNLOAD_DIR)/openpci68k.lha

# MD5 checksums for verification
IDENTIFY_USR_MD5 = f8bd9feb9fa595bea979755224d08c5c
IDENTIFY_PCI_MD5 = 7771426e5c7a5e3dc882a973029099d1
OPENPCI_MD5 = afeef82072c8556559beb6923dccf91f

.PHONY: identify-all disk download-libs

# Create downloads directory
$(DOWNLOAD_DIR):
	mkdir -p $(DOWNLOAD_DIR)

# Download and verify IdentifyUsr.lha
$(IDENTIFY_USR_LHA): | $(DOWNLOAD_DIR)
	@if [ -f "$@" ] && echo "$(IDENTIFY_USR_MD5)  $@" | md5sum -c --status 2>/dev/null; then \
		echo "$@ already downloaded and verified"; \
	else \
		echo "Downloading IdentifyUsr.lha..."; \
		curl -sL http://aminet.net/util/libs/IdentifyUsr.lha -o $@; \
		echo "$(IDENTIFY_USR_MD5)  $@" | md5sum -c || (rm -f $@; exit 1); \
	fi

# Download and verify IdentifyPci.lha
$(IDENTIFY_PCI_LHA): | $(DOWNLOAD_DIR)
	@if [ -f "$@" ] && echo "$(IDENTIFY_PCI_MD5)  $@" | md5sum -c --status 2>/dev/null; then \
		echo "$@ already downloaded and verified"; \
	else \
		echo "Downloading IdentifyPci.lha..."; \
		curl -sL http://aminet.net/util/libs/IdentifyPci.lha -o $@; \
		echo "$(IDENTIFY_PCI_MD5)  $@" | md5sum -c || (rm -f $@; exit 1); \
	fi

# Download and verify openpci68k.lha
$(OPENPCI_LHA): | $(DOWNLOAD_DIR)
	@if [ -f "$@" ] && echo "$(OPENPCI_MD5)  $@" | md5sum -c --status 2>/dev/null; then \
		echo "$@ already downloaded and verified"; \
	else \
		echo "Downloading openpci68k.lha..."; \
		curl -sL http://aminet.net/driver/other/openpci68k.lha -o $@; \
		echo "$(OPENPCI_MD5)  $@" | md5sum -c || (rm -f $@; exit 1); \
	fi

# Download all libraries
download-libs: $(IDENTIFY_USR_LHA) $(IDENTIFY_PCI_LHA) $(OPENPCI_LHA)
	mkdir -p 3rdparty/identify/build
	# Extract Identify library
	lha xq $(IDENTIFY_USR_LHA) Identify/libs/identify.library
	mv Identify/libs/identify.library 3rdparty/identify/build/
	rm -rf Identify
	# Extract PCI database
	lha xq $(IDENTIFY_PCI_LHA) Identify/s/pci.db
	mv Identify/s/pci.db 3rdparty/identify/build/
	rm -rf Identify
	# Extract OpenPCI library
	lha xq $(OPENPCI_LHA) Libs/openpci.library
	mv Libs/openpci.library 3rdparty/identify/build/
	rm -rf Libs

disk: $(TARGET) download-libs
	xdftool $(DISK) format "xSysInfo"
	xdftool $(DISK) write $(TARGET) $(TARGET)
	xdftool $(DISK) write $(TARGET).info $(TARGET).info
	xdftool $(DISK) makedir Libs
	xdftool $(DISK) write 3rdparty/identify/build/identify.library Libs/identify.library
	xdftool $(DISK) write 3rdparty/identify/build/openpci.library Libs/openpci.library
	xdftool $(DISK) makedir S
	xdftool $(DISK) write Startup-Sequence S/Startup-Sequence
	xdftool $(DISK) write 3rdparty/identify/build/pci.db S/pci.db
	xdftool $(DISK) boot install
	xdftool $(DISK) list
