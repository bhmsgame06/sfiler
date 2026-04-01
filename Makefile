SRC_DIR := src
BUILD_DIR := bin
LIBS := -lz
CC := cc
CFLAGS := -O2 -g
LD := cc
LDFLAGS := $(LIBS)

SPUSH_OBJS := $(SRC_DIR)/spush/main.o
SPULL_OBJS := $(SRC_DIR)/spull/main.o
SRM_OBJS := $(SRC_DIR)/srm/main.o
OBJS := $(SRC_DIR)/serial.o \
		$(SRC_DIR)/progress.o

PREFIX := /usr/local

all: spush spull srm

spush: $(SPUSH_OBJS) $(OBJS)
	mkdir -p $(BUILD_DIR)
	$(LD) -o $(BUILD_DIR)/$@ $^ $(LDFLAGS)

spull: $(SPULL_OBJS) $(OBJS)
	mkdir -p $(BUILD_DIR)
	$(LD) -o $(BUILD_DIR)/$@ $^ $(LDFLAGS)

srm: $(SRM_OBJS) $(OBJS)
	mkdir -p $(BUILD_DIR)
	$(LD) -o $(BUILD_DIR)/$@ $^ $(LDFLAGS)

%.o: %.c %.h
	$(CC) -c -o $@ $< $(CFLAGS)

install:
	install -Dm755 $(BUILD_DIR)/spush $(BUILD_DIR)/spull $(BUILD_DIR)/srm $(PREFIX)/bin

clean:
	rm -f $(SPUSH_OBJS) $(SPULL_OBJS) $(SRM_OBJS) $(OBJS)
