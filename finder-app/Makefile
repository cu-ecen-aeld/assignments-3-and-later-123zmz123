CC = gcc
LDFLAGS=
CFLAGS= -Wall -Werror
CROSS_COMPILE=
INCLUDE=

SOURCES=writer.c
OBJECTS=$(SOURCES:.c=.o) # replace .c to .o of all the sources file
TARGET=writer

CROSS_COMPILE=
     
all: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(OBJECTS): $(SOURCES)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $<


clean:
	rm -f $(TARGET) $(OBJECTS)