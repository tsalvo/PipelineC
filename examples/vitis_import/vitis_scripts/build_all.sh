#!/bin/bash

# Use ../build.sh to produce PipelineC ../OUT/package_axis_xo.tcl file

# Vitis build steps:
#TARGET=hw
TARGET=hw_emu
VITIS_PLATFORM=xilinx_u50_gen3x16_xdma_5_202210_1
vivado -source ../OUT/package_axis_xo.tcl -mode batch
./vitis_compile.sh ${TARGET} ${VITIS_PLATFORM}
./vitis_link.sh ${TARGET} ${VITIS_PLATFORM}
./gpp.sh
./test.sh ${TARGET} ${VITIS_PLATFORM}
