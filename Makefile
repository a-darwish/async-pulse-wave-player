
PULSE_CFLAGS = $(shell pkg-config --cflags libpulse)
PULSE_LDFLAGS = $(shell pkg-config --libs libpulse)

CFLAGS += -Wall -g -I include/ $(PULSE_CFLAGS)
LDFLAGS += $(PULSE_LDFLAGS)

OBJS = src/pa_async_client.o src/audio_file.o
PROGRAM = wave_player

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -vf $(OBJS) $(PROGRAM)
