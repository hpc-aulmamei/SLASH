#include <ap_int.h>
#include <stdint.h>

#define DATA_WIDTH 256
typedef ap_uint<DATA_WIDTH> uint256_t;
#define LENGTH 0x1000000

extern "C" void hbm_bandwidth(
    uint256_t* hbm_ptr    // HBM memory-mapped pointer
) {
#pragma HLS INTERFACE m_axi port=hbm_ptr offset=slave bundle=gmem0 max_read_burst_length=64 max_write_burst_length=64 depth=536870912
#pragma HLS INTERFACE s_axilite port=hbm_ptr   bundle=control
#pragma HLS INTERFACE s_axilite port=return    bundle=control

    for (uint32_t i = 0; i < LENGTH; i++) {
    #pragma HLS PIPELINE II=1
        hbm_ptr[i] = i;
    }
}

