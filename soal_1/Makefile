CC := gcc
CFLAGS := -Wall -Wextra
TARGET := kenz_rescue
SRC := kenz_rescue.c
SOURCE_DIR ?= amba_files
MOUNT_DIR ?= mnt
PKG_CONFIG := pkg-config
FUSE_CFLAGS := $(shell $(PKG_CONFIG) --cflags fuse3)
FUSE_LIBS := $(shell $(PKG_CONFIG) --libs fuse3)

.PHONY: all compile mount run umount clean clear

all: compile

compile: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(FUSE_CFLAGS) $(FUSE_LIBS)

mount: compile umount
	mkdir -p $(MOUNT_DIR)
	./$(TARGET) $(SOURCE_DIR) $(MOUNT_DIR)

run: mount

umount:
	@if [ -d "$(MOUNT_DIR)" ]; then \
		if command -v fusermount3 >/dev/null 2>&1; then \
			fusermount3 -u "$(MOUNT_DIR)" 2>/dev/null || true; \
		elif command -v fusermount >/dev/null 2>&1; then \
			fusermount -u "$(MOUNT_DIR)" 2>/dev/null || true; \
		fi; \
	fi

clean: umount
	rm -f $(TARGET)

clear:
	clear
