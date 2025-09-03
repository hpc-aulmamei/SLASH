# SLASH  VRT (V80 RunTime) API

This repository contains SLASH, a SmartNIC platform for Alveo V80. SLASH consists of several components:
- card management functionality that layers on top of [AVED](https://github.com/Xilinx/AVED)
- V80 RunTime (VRT): the VRT API implementation, along with examples.
- (upcoming) 200Gbps MAC implementation for network-attached kernels and SmartNIC applications

> [!WARNING]
> The project was only tested with AMD Vivado & Vitis tools version 2024.2, on Ubuntu 22.04, kernel version 5.15. Please use same versions of tools/kernel.

## Dependencies

- libxml2
- ZeroMQ (zmq) for emulation & simulation
- jsoncpp for emulation & simulation

To install the dependencies:

```bash
sudo apt install libxml2-dev libzmq3-dev libjsoncpp-dev xvfb
```

### Submodules

SLASH depends on [AVED](https://github.com/Xilinx/AVED) and [QDMA](https://github.com/Xilinx/dma_ip_drivers) which can be pulled from git using:
```bash
git submodule update --init --recursive
```
## Deployment

In order to build and deploy all necessary software for this project to work, follow the steps shown in the [deployment instructions](deploy/README.md).

## How to build VRT API documentation

Follow the instructions in the [API documentation README](vrt/doc/README.md).

## How to build the examples

A [README](examples/README.md) file can be found in the examples directory. Please follow the instruction given there to proceed.

## Known limitations
- HLS arguments should not be Verilog or VHDL keywords (eg. `in`, `out` so on). Some issues may appear in the linker with this configuration.
- In emulation, HLS kernels must include at least one axi4lite interface to work.
- In the current version of the [linker](submodules/v80-vitis-flow), a maximum number of 15 kernels can be instantiated. This will be fixed in future versions.

## Notes
All the hardware examples are built using the Segmented Configuration flow.
