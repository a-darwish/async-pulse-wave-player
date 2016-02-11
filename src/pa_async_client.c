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
 */

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/mainloop.h>
#include <pulse/mainloop-api.h>
#include <pulse/mainloop-signal.h>
#include <pulse/proplist.h>
#include <pulse/sample.h>
#include <pulse/stream.h>

#include "common.h"

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

    pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "asynchronous-client");
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
