
PULSE_CFLAGS = $(shell pkg-config --cflags libpulse)
PULSE_LDFLAGS = $(shell pkg-config --libs libpulse)

CFLAGS += -Wall -g $(PULSE_CFLAGS)
LDFLAGS += $(PULSE_LDFLAGS)

OBJS = asynchronous-client.o audio_file.o
PROGRAM = wave-player

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -vf $(OBJS) $(PROGRAM)
