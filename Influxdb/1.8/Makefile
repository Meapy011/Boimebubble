CC = gcc
CFLAGS = -Wall -O2 -I../../include
LDFLAGS = -lcurl -lm

SRCS = main.c \
       ../../src/sensirion_i2c_hal.c \
       ../../src/sensirion_i2c.c \
       ../../src/sensirion_common.c \
       ../../src/sen44_i2c.c \
       ../../src/scd30_i2c.c \
       ../../src/sfa3x_i2c.c \
       ../../src/sen66_i2c.c \
       ../../src/sen5x_i2c.c

OBJS = $(SRCS:.c=.o)
TARGET = sensors-db

.PHONY: all clean

all: $(TARGET)
	@echo "Build complete. Binary: $(TARGET)"

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Cleaning up object files..."
	rm -f $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning all object files and binary..."
	rm -f $(OBJS) $(TARGET)
