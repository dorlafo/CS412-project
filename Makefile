SRCDIR   = src
BINDIR   = bin
INCLUDES = include

CC=gcc
CFLAGS=-Wall -Wextra -g -m32 -fno-stack-protector -z execstack -lpthread -std=gnu11 -I $(INCLUDES)/ 
DEPS = $(wildcard $(INCLUDES)/%.h)

all: $(BINDIR)/client $(BINDIR)/server $(DEPS)

$(BINDIR)/client: $(SRCDIR)/client.c $(SRCDIR)/grass.c $(INCLUDES)/grass.h | $(BINDIR)
	$(CC) $(CFLAGS) $< $(SRCDIR)/grass.c -o $@

$(BINDIR)/server: $(SRCDIR)/server.c $(SRCDIR)/grass.c $(INCLUDES)/grass.h | $(BINDIR)
	$(CC) $(CFLAGS) $< $(SRCDIR)/grass.c -o $@

$(BINDIR):
	mkdir $(BINDIR)

.PHONY: clean
clean:
	rm -f $(BINDIR)/client $(BINDIR)/server
