#ifndef COMMON_H_
#define COMMON_H_

/*
 * Copyright (C) 2016 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, version 2 or later.
 */

#include <errno.h>
#include <stdio.h>

#include <pulse/sample.h>

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
    printf("[Info] ");                          \
    printf(fmt "\n", ##__VA_ARGS__);            \
}

/* Type-safe min macro */
#define min(x, y) ({                            \
    typeof(x) _m1 = (x);                        \
    typeof(y) _m2 = (y);                        \
    (void) (&_m1 == &_m2);                      \
    _m1 < _m2 ? _m1 : _m2;                      \
})

/*
 * Book-keeping for the WAV audio file to be played
 */
struct audio_file {
    char *buf;                  /* Memory-mapped buffer of file's PCM audio data */
    size_t size;                /* File's _audio_ data size, in bytes */
    size_t readi;               /* Read index; bytes played so far */
    pa_sample_spec spec;        /* Audio format: bits per sample, freq, and channels */
};

struct audio_file *audio_file_new(char *filepath);

#endif /* COMMON_H_ */
