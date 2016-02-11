/*
 * A WAVE audio player using PulseAudio asynchronous APIs
 *
 * Copyright (C) 2016 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, version 2 or later.
 *
 * This is an as minimal as it can get implementation for the
 * purpose of studying PulseAudio asynchronous APIs.
 *
 * TODO: Re-factor
 */

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/mainloop.h>
#include <pulse/mainloop-api.h>
#include <pulse/mainloop-signal.h>
#include <pulse/proplist.h>
#include <pulse/sample.h>
#include <pulse/stream.h>

#define __error(fmt, suffix, ...) {             \
    fprintf(stderr, "[Error] ");                \
    fprintf(stderr, fmt suffix, ##__VA_ARGS__); \
}

#define error(fmt, ...)                         \
    __error(fmt, "\n", ##__VA_ARGS__);

/* perror, but with printf-like formatting */
#define errorp(fmt, ...) {                      \
    __error(fmt, ": ", ##__VA_ARGS__);          \
    perror("");                                 \
}

#define out(fmt, ...) {                         \
    printf(fmt "\n", ##__VA_ARGS__);            \
}

/* Type-safe min macro */
#define min(x, y) ({                            \
    typeof(x) _m1 = (x);                        \
    typeof(y) _m2 = (y);                        \
    (void) (&_m1 == &_m2);                      \
    _m1 < _m2 ? _m1 : _m2;                      \
})

struct wave_header {
    char     id[4];             /* "RIFF" */
    uint8_t  ignored[16];       /* ... */
    uint16_t audio_format;      /* PCM = 1, else compression used */
    uint16_t channels;          /* channels count (mono, stereo, ..) */
    uint32_t frequency;         /* 44KHz? */
    uint8_t  ignored2[6];       /* ... */
    uint16_t bits_per_sample;   /* 8 bits, 16 bits, etc. per sample */
    uint32_t ignored3;          /* ... */
    uint32_t audio_data_size;   /* nr_samples * bits_per_sample * channels */
} __attribute__((packed));

/*
 * Book-keeping for the audio file to be played
 */
struct audio_file {
    char *buf;                  /* Memory-mapped buffer of file audio data */
    size_t size;                /* File's audio data size, in bytes */
    size_t readi;               /* Read index; bytes played so far */
    pa_sample_spec spec;        /* Audio sample format, rate, and channels */
};

/*
 * Stream state callbacks
 *
 * A 'stream' represents a data path between the client and server.
 * Sample streams include a playback stream, a recording stream, or
 * a file upload stream.
 *
 * A single client-server connection ('context') can have multiple
 * streams. Each stream can have its own latency and time fragment
 * requirements through PulseAudio buffer attributes. A stream can
 * be moved to a different sink during its lifetime.
 */
static void stream_state_callback(pa_stream *stream, void *userdata) {

    switch (pa_stream_get_state(stream)) {
    case PA_STREAM_CREATING:
    case PA_STREAM_TERMINATED:
        break;

    case PA_STREAM_READY:
        out("Stream succesfully created");
        break;

    case PA_STREAM_FAILED:
    default:
        error("PulseAudio stream error: %s",
              pa_strerror(pa_context_errno(pa_stream_get_context(stream))));
        goto fail;
    }

    return;

fail:
    exit(EXIT_FAILURE);
}

/* Callback to be called whenever new data may be written to the
 * playback data stream */
static void stream_write_callback(pa_stream *stream, size_t length, void *userdata) {
    struct audio_file *file = userdata;
    size_t to_write, write_unit;
    int ret;

    assert(file);
    assert(file->buf);
    assert(file->readi <= file->size);

    /* Writes must be in multiple of audio sample size * channel count */
    write_unit = pa_frame_size(&file->spec);

    to_write = file->size - file->readi;
    to_write = min(length, to_write);
    to_write -= (to_write % write_unit);

    ret = pa_stream_write(stream, &file->buf[file->readi], to_write, NULL, 0,
                          PA_SEEK_RELATIVE);
    if (ret < 0) {
        error("Failed writing audio data to stream: %s",
              pa_strerror(pa_context_errno(pa_stream_get_context(stream))));
        goto fail;
    }

    file->readi += to_write;
    assert(file->readi <= file->size);

    if ((file->size - file->readi) < write_unit) {
        out("Success! - Reached end of file");
        exit(EXIT_SUCCESS);
    }

    return;

fail:
    exit(EXIT_FAILURE);
}

/*
 * Context state callbacks
 *
 * A 'context' represents the connection handle between a PulseAudio
 * client and its server. It multiplexes everything in that connection
 * including data streams , bi-directional commands, and events.
 */
static void context_state_callback(pa_context *context, void *userdata) {
    struct audio_file *file = userdata;
    pa_stream *stream;
    int ret;

    switch (pa_context_get_state(context)) {
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_SETTING_NAME:
        break;

    case PA_CONTEXT_READY:
        out("Connection established with PulseAudio server");

        stream = pa_stream_new(context, "playback stream", &file->spec, NULL);
        if (!stream)
            goto fail;

        pa_stream_set_state_callback(stream, stream_state_callback, NULL);
        pa_stream_set_write_callback(stream, stream_write_callback, userdata);

        /* Connect this stream with a sink chosen by PulseAudio */
        ret = pa_stream_connect_playback(stream, NULL, NULL, 0, NULL, NULL);
        if (ret < 0) {
            error("pa_stream_connect_playback() failed: %s",
                  pa_strerror(pa_context_errno(context)));
            goto fail;
        }

        break;

    case PA_CONTEXT_TERMINATED:
        exit(EXIT_SUCCESS);
        break;

    case PA_CONTEXT_FAILED:
    default:
        error("PulseAudio context connection failure: %s",
              pa_strerror(pa_context_errno(context)));
        goto fail;
    }

    return;

fail:
    exit(EXIT_FAILURE);
}

static pa_sample_format_t bits_per_sample_to_pa_spec_format(int bits) {
    switch (bits) {
    case  8: return PA_SAMPLE_U8;
    case 16: return PA_SAMPLE_S16LE;
    case 32: return PA_SAMPLE_S16BE;
    default:
        error("Unrecognized WAV file with %u bits per sample", bits);
        exit(EXIT_FAILURE);
    }
}

struct audio_file *audio_file_new(char *filepath) {
    struct audio_file *file;
    struct stat filestat;
    struct wave_header *header;
    size_t header_size;
    int fd, ret;

    file = malloc(sizeof(struct audio_file));
    if (!file)
        goto fail;

    fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        errorp("open('%s')", filepath);
        goto fail;
    }

    ret = fstat(fd, &filestat);
    if (ret < 0) {
        errorp("fstat('%s')", filepath);
        goto fail;
    }

    header_size = sizeof(struct wave_header);
    if (filestat.st_size < header_size) {
        errorp("Too small file size < WAV header's %lu bytes", header_size);
        goto fail;
    }

    header = mmap(NULL, filestat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (!header) {
        errorp("mmap('%s')", filepath);
        goto fail;
    }

    if (strncmp(header->id, "RIFF", 4)) {
        error("File '%s' is not a WAV file", filepath);
        goto fail;
    }

    if (header->audio_format != 1) {
        error("Cannot play audio file '%s'", filepath);
        error("Audio data is not in raw, uncompressed, PCM format");
        goto fail;
    }

    file->buf = (void *)(header + 1);
    file->size = header->audio_data_size;
    file->readi = 0;
    file->spec.format = bits_per_sample_to_pa_spec_format(header->bits_per_sample);
    file->spec.rate = header->frequency;
    file->spec.channels = header->channels;

    return file;

fail:
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    pa_proplist *proplist = NULL;
    pa_mainloop *m = NULL;
    pa_mainloop_api *api = NULL;
    pa_context *context = NULL;
    struct audio_file *file;
    char *filepath;
    int ret;

    if (argc != 2) {
        error("usage: %s <WAVE-AUDIO-FILE>", argv[0]);
        goto quit;
    }

    filepath = argv[1];
    file = audio_file_new(filepath);
    if (!file)
        goto quit;

    proplist = pa_proplist_new();
    if (!proplist) {
        error("Couldn't create a PulseAudio property list");
        goto quit;
    }

    pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "sampleClient");
    pa_proplist_sets(proplist, PA_PROP_MEDIA_NAME, filepath);

    m = pa_mainloop_new();
    if (!m) {
        error("Couldn't create PulseAudio mainloop");
        goto quit;
    }

    api = pa_mainloop_get_api(m);
    context = pa_context_new_with_proplist(api, NULL, proplist);
    if (!context) {
        error("Couldn't create client context");
        goto quit;
    }

    pa_context_set_state_callback(context, context_state_callback, file);

    ret = pa_context_connect(context, NULL, 0, NULL);
    if (ret < 0) {
        error ("Couldn't connect to PulseAudio server: %s",
               pa_strerror(pa_context_errno(context)));
        goto quit;
    }

    pa_mainloop_run(m, &ret);

    return ret;

 quit:
     exit(EXIT_FAILURE);
}
