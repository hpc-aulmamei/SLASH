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

DEVICE=xcv80-lsva4737-2MHP-e-S

INCREMENT_BUILD_DIR=build_increment.$(DEVICE)
ACCUMULATE_BUILD_DIR=build_accumulate.$(DEVICE)

all: $(INCREMENT_BUILD_DIR) $(ACCUMULATE_BUILD_DIR)

$(INCREMENT_BUILD_DIR):
	if [ ! -d "$(INCREMENT_BUILD_DIR)" ]; then \
		mkdir -p "$(INCREMENT_BUILD_DIR)"; \
		cp -f increment.cpp "$(INCREMENT_BUILD_DIR)/increment.cpp"; \
		cd "$(INCREMENT_BUILD_DIR)" && \
		  v++ -c --mode hls --config ../increment.cfg --work_dir . && \
		  vitis-run --mode hls --package --config ../increment.cfg --work_dir . ; \
	fi

$(ACCUMULATE_BUILD_DIR):
	if [ ! -d "$(ACCUMULATE_BUILD_DIR)" ]; then \
		mkdir -p "$(ACCUMULATE_BUILD_DIR)"; \
		cp -f accumulate.cpp "$(ACCUMULATE_BUILD_DIR)/accumulate.cpp"; \
		cd "$(ACCUMULATE_BUILD_DIR)" && \
		  v++ -c --mode hls --config ../accumulate.cfg --work_dir . && \
		  vitis-run --mode hls --package --config ../accumulate.cfg --work_dir . ; \
	fi

clean:
	rm -rf $(INCREMENT_BUILD_DIR) $(ACCUMULATE_BUILD_DIR) *.log
