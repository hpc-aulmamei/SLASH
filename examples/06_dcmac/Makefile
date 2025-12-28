# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
# 
#  Permission is hereby granted, free of charge, to any person obtaining a copy of this software
#  and associated documentation files (the "Software"), to deal in the Software without restriction,
#  including without limitation the rights to use, copy, modify, merge, publish, distribute,
#  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
# 
#  The above copyright notice and this permission notice shall be included in all copies or
#  substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
# NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
# ##################################################################################################

HLS_BUILD_DIR_TRAFFIC_PRODUCER=build_traffic_producer.xcv80-lsva4737-2MHP-e-S
DESIGN_NAME=06_dcmac
HOME_DIR=$(shell realpath .)
BUILD_DIR=$(shell realpath ./build)
HLS_DIR=$(shell realpath ./hls)
V80PP_PATH=$(shell realpath ../../submodules/v80-vitis-flow)
VPP_DIR=$(BUILD_DIR)/v80-vitis-flow

.PHONY: all setup hls hw app clean

all: setup hls hw app

hw_all: setup hls hw app

setup:
	mkdir -p $(BUILD_DIR)
	cp -r $(V80PP_PATH) $(BUILD_DIR)

hls:
	@echo "Running HLS step"
	$(MAKE) -C $(HLS_DIR)

hw: setup hls
	@echo "Running HW step"
	cd $(VPP_DIR) && \
	./scripts/v80++ --design-name $(DESIGN_NAME) --cfg $(HOME_DIR)/config.cfg --platform hw --segmented --kernels $(HLS_DIR)/$(HLS_BUILD_DIR_TRAFFIC_PRODUCER)/sol1 && \
	cp build/$(DESIGN_NAME)_hw.vrtbin $(BUILD_DIR)

app: setup
	@echo "Running user app build step"
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && \
	cmake .. && \
	make -j9
	@echo "Setting LD_LIBRARY_PATH"
	export LD_LIBRARY_PATH=$$(dirname $$(which vivado))/../lib/lnx64.o:$$LD_LIBRARY_PATH
	@echo "Setting PATH"
	export PATH=$$PATH:/usr/local/sbin

clean:
	rm -rf $(BUILD_DIR)
	make -C $(HLS_DIR) clean