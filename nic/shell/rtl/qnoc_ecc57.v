// C2H control ECC: 7 check bits over the 57-bit ECC input of PG347
// {17'h0, var_desc, drop_req, num_buf_ov, host_id[3:0], has_cmpt, marker,
//  port_id[2:0], qid[11:0], len[15:0]}. The matrix was measured in xsim from
// the encoder the CPM5 QDMA example design uses (57 data, 7 check bits):
// check bit k = parity of the data bits whose column has bit k set.
`timescale 1ns / 1ps
module qnoc_ecc57 (input wire [56:0] d, output wire [6:0] c);
    assign c[0] = ^(d & 57'h12cd2d32da65cb7);
    assign c[1] = ^(d & 57'h155555556aaad5b);
    assign c[2] = ^(d & 57'h19999999b33366d);
    assign c[3] = ^(d & 57'h1e1e1e1e3c3c78e);
    assign c[4] = ^(d & 57'h1fe01fe03fc07f0);
    assign c[5] = ^(d & 57'h1fffe0003fff800);
    assign c[6] = ^(d & 57'h1fffffffc000000);
endmodule
