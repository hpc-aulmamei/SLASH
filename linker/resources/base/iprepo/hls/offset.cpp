/**
 * The MIT License (MIT)
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <ap_fixed.h>
#include <hls_stream.h>

void offset(ap_uint<32> size, ap_uint<32>* input, hls::stream<ap_uint<32> >& axis_out, ap_uint<32> m, ap_uint<32> n) {
#pragma hls interface mode=s_axilite port=size
#pragma hls interface m_axi bundle=gmem0 port=input max_widen_bitwidth=64
#pragma hls interface axis port=axis_out
#pragma hls interface mode=s_axilite port=m
#pragma hls interface mode=s_axilite port=n
#pragma hls interface mode=s_axilite port=return

    for(ap_uint<32> i = 0; i < size; i++) {
#pragma HLS PIPELINE II=1
        ap_uint<32> val = input[i];
        val = val * m + n;
        axis_out.write(val);
    }
}
