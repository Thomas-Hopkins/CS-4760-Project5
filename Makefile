CC = gcc
CFLAGS = -Wall -g

EXE = oss user_proc
DEPS = shared.h
OBJS = shared.o

CLEAN = $(EXE) *.o $(OBJS) logfile

all: $(EXE)

oss: oss.o $(OBJS) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

user_proc: user_proc.o $(OBJS) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS) 

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	rm -f $(CLEAN)
