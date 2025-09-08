#!/bin/bash

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


usage() {
    echo "Usage: $0 <BDF> [--mm <queue_id> <direction>] [--st <queue_id>] [--dir <direction>]"
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

BDF=$1
shift

BDF_PCI="${BDF/.0/.1}"
BDF_QDMA="${BDF:0:2}${BDF:3:2}1"
PATH=$PATH:/usr/local/sbin

echo "Starting QDMA queue..." >> /var/log/setup_queues.log
echo "Setting qmax..." >> /var/log/setup_queues.log
echo 4096 > /sys/bus/pci/devices/0000\:${BDF_PCI}/qdma/qmax 

while [[ $# -gt 0 ]]; do
    key="$1"
    case $key in
        --mm)
        QUEUE_ID="$2"
        MM_DIRECTION="$3"
        echo "Adding MM queue with ID $QUEUE_ID and direction $MM_DIRECTION..." >> /var/log/setup_queues.log
        dma-ctl qdma${BDF_QDMA} q add idx $QUEUE_ID mode mm dir $MM_DIRECTION >> /var/log/setup_queues.log
        sleep 1
        dma-ctl qdma${BDF_QDMA} q start idx $QUEUE_ID idx_ringsz 15 dir $MM_DIRECTION >> /var/log/setup_queues.log
        sleep 1
        chmod 666 /dev/qdma${BDF_QDMA}-MM-$QUEUE_ID
        shift # past argument
        shift # past value
        shift # past direction
        ;;
        --st)
        QUEUE_ID="$2"
        shift # past argument
        shift # past value
        ;;
        --dir)
        DIRECTION="$2"
        echo "Adding Stream queue with ID $QUEUE_ID and direction $DIRECTION..." >> /var/log/setup_queues.log
        dma-ctl qdma${BDF_QDMA} q add idx $QUEUE_ID mode st dir $DIRECTION >> /var/log/setup_queues.log
        sleep 1
        dma-ctl qdma${BDF_QDMA} q start idx $QUEUE_ID idx_ringsz 15 dir $DIRECTION >> /var/log/setup_queues.log
        sleep 1
        chmod 666 /dev/qdma${BDF_QDMA}-ST-$QUEUE_ID
        shift # past argument
        shift # past value
        ;;
        *)
        echo "Unknown option $key"
        usage
        ;;
    esac
done