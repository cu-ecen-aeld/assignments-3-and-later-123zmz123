CROSS_COMPILE ?= #aarch64-none-linux-gnu-
CC = $(CROSS_COMPILE)gcc

writer: writer.c 
		$(CC) -o $@ $<

clean:
		rm -f writer

