#!/bin/sh
cmake -G "Unix Makefiles" -DPICO_BOARD=pico2_w -DJEFF_CMSIS_DAP_DEBUG="${JEFF_CMSIS_DAP_DEBUG:-OFF}" -B build
