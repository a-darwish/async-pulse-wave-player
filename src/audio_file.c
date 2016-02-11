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
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <pulse/sample.h>

#include "common.h"

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

static pa_sample_format_t bits_per_sample_to_pa_spec_format(int bits) {
    switch (bits) {
    case  8: return PA_SAMPLE_U8;
    case 16: return PA_SAMPLE_S16LE;
    case 32: return PA_SAMPLE_S16BE;
    default: return PA_SAMPLE_INVALID;
    }
}

struct audio_file *audio_file_new(char *filepath) {
    struct audio_file *file = NULL;
    struct stat filestat = { 0, };
    struct wave_header *header = NULL;
    pa_sample_format_t sample_format;
    size_t header_size, audio_size;
    int fd = -1, ret;

    file = malloc(sizeof(struct audio_file));
    if (!file)
        goto fail;

    memset(file, 0, sizeof(*file));

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
        error("Too small file size < WAV header %lu bytes", header_size);
        goto fail;
    }

    header = mmap(NULL, filestat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (header == MAP_FAILED) {
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

    sample_format = bits_per_sample_to_pa_spec_format(header->bits_per_sample);
    if (sample_format == PA_SAMPLE_INVALID) {
        error("Unrecognized WAV file format with %u bits per sample!",
              header->bits_per_sample);
        goto fail;
    }

    /* Guard against corrupted WAV files where the reported audio
     * data size is much larger than what's really in the file. */
    audio_size = min((size_t)header->audio_data_size,
                     (filestat.st_size - header_size));

    file->buf = (void *)(header + 1);
    file->size = audio_size;
    file->readi = 0;
    file->spec.format = sample_format;
    file->spec.rate = header->frequency;
    file->spec.channels = header->channels;

    return file;

fail:
    if (header && header != MAP_FAILED) {
        assert(filestat.st_size > 0);
        munmap(header, filestat.st_size);
    }

    if (fd != -1)
        close(fd);

    if (file)
        free(file);

    return NULL;
}
