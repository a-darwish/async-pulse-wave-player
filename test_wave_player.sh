#!/bin/bash

set -e

function separator() {
    echo "==========================================="
}

make

for audio_file in sample_wave_files/*.wav; do
    echo "[Script] Playing wave file '`basename $audio_file`'"
    ./wave_player $audio_file
    separator
done
