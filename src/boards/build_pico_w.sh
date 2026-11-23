#!/bin/sh
cmake -G "Unix Makefiles" -DFAMILY=rp2040 -DPICO_BOARD=pico_w -DJEFF_CMSIS_DAP_DEBUG="${JEFF_CMSIS_DAP_DEBUG:-OFF}" -B build
