CROSS_COMPILE?=
CC ?= $(CROSS_COMPILE)gcc
CFLAGS?= -Wall -Werror
LDFLAGS?= -lpthread -lrt
INCLUDES?=


TARGET=aesdsocket
SOURCES=aesdsocket.c
OBJECTS=$(SOURCES:.c=.o)


all: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CROSS_COMPILE)$(CC) $(OBJECTS) -o $@  $(INCLUDES) $(LDFLAGS)

$(OBJECTS): $(SOURCES)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $<


clean:
	rm -f $(TARGET) $(OBJECTS)
