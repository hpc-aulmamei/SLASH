# SLASH components deployment

In order to deploy compute kernels on the SLASH platform, a few steps are necessary.

- Apply the AVED patch to the repository
- Build the AVED software stack
- Install the AVED software stack
- Build the VRT software stack
- Install the VRT software stack
- Build and install the QDMA driver

## Apply the AVED patch to the repository

In order to apply the AVED patch to the repository, run the following commands:

```bash
cd <project_root>/submodules/v80-vitis-flow/submodules/aved/
git apply ../../../../deploy/aved.patch
```

## Create the base PDI for segmented configuration

In order to build the base PDI for segmented configuration, run the following commands:
```bash
cd <project_root>/deploy/base_pdi
python3 build.py --platform compute
```
This will take a few hours.

## Build the AVED software stack
In order to build the AVED software stack, run the following commands:

```bash
cd <project_root>/submodules/v80-vitis-flow/submodules/aved/sw/AMI/
./scripts/gen_package.py
```

## Install the AVED software stack

The previous commands generate a directory, called `output`. Inside of that directory, a new directory is created, with the name `yyyy-mm-dd-hh-mm-ss`. Run the following commands:

```bash
cd <project_root>/submodules/v80-vitis-flow/submodules/aved/sw/AMI/output/yyyy-mm-dd-hh-mm-ss
sudo apt install ./ami_<version>_<arch>_<os version>.deb
```
This should install the AVED software stack. To verify the install was correct, run:

```bash
ami_tool overview

AMI             
-------------------------------------------------------------
Version          | 2.3.0  (0)                                
Branch          
Hash             | 0bab29e568f64a25f17425c0ffd1c0e89609b6d1  
Hash Date        | 20240307                                  
Driver Version   | 2.3.0  (0)                                


BDF       | Device          | UUID                               | AMC          | State  
-----------------------------------------------------------------------------------------
21:00.0   | ALVEO V80 INT   | 3907c6f088e5c23471ab99aae09a9928   | 2.3.0  (0)   | READY  
e2:00.0   | ALVEO V80 INT   | 3907c6f088e5c23471ab99aae09a9928   | 2.3.0  (0)   | READY
```

## Build the VRT software stack

In order to build the VRT software stack, run the following commands:

```bash
cd <project_root>
./deploy/package/package.py
```

## Install the VRT software stack

In the directory `<project_root>/deploy/output` the previous commands generate a directory called `amd-vrt_<version>_<timestamp>` directory. Navigate to that directory and run:

```bash
cd <project_root>/deploy/output/amd-vrt_<version>_<timestamp>
sudo apt install ./amd-vrt_<version>_<timestamp>_<arch>.deb
```
This will install the runtime API and CLI utility function. You can verify the successful installation by running:

```bash
v80-smi list
--------------------------------------------------------------------
Listing V80 devices 
--------------------------------------------------------------------
V80 device found with BDF: 0000:e2:00.0
--------------------------------------------------------------------
V80 device found with BDF: 0000:21:00.0
--------------------------------------------------------------------
```

## Build and install the QDMA driver
In order to build and install the QDMA driver, a few steps are necessary:

```bash
cd <project_root>/submodules/qdma-drv/QDMA/linux-kernel/driver/src
```
The PCIe identifier for V80 cards needs to be inserted in the file `pci_ids.h`, as follows:

```bash
lspci -vd 10ee:
21:00.0 Processing accelerators: Xilinx Corporation Device 50b4
        Subsystem: Xilinx Corporation Device 000e
        Physical Slot: 2-1
        Flags: bus master, fast devsel, latency 0, NUMA node 2, IOMMU group 27
        Memory at 2bf70000000 (64-bit, prefetchable) [size=256M]
        Capabilities: <access denied>
        Kernel driver in use: ami
        Kernel modules: ami

21:00.1 Processing accelerators: Xilinx Corporation Device 50b5
        Subsystem: Xilinx Corporation Device 000e
        Physical Slot: 2-1
        Flags: bus master, fast devsel, latency 0, NUMA node 2, IOMMU group 27
        Memory at 2bf80000000 (64-bit, prefetchable) [size=512K]
        Capabilities: <access denied>
        Kernel driver in use: qdma-pf
        Kernel modules: qdma_pf, ami, qdma_vf

# At the same line with 21:00.1, you can see the PCIe identifier as 50b5
# 50b5 should be added to end of src/pci_ids.h in following form
{ PCI_DEVICE(0x10ee, 0x50b5), },        /** V80 */
```
Then the driver can be built:

```bash
cd <project_root>/submodules/qdma-drv/QDMA/linux-kernel && make
sudo make install
```

In order to verify the correct installation of the qdma driver, run the command:

```bash
lspci -vd 10ee:
21:00.0 Processing accelerators: Xilinx Corporation Device 50b4
        Subsystem: Xilinx Corporation Device 000e
        Flags: bus master, fast devsel, latency 0, NUMA node 0, IOMMU group 29
        Memory at 2bf70000000 (64-bit, prefetchable) [size=256M]
        Capabilities: <access denied>
        Kernel driver in use: ami
        Kernel modules: ami

21:00.1 Processing accelerators: Xilinx Corporation Device 50b5
        Subsystem: Xilinx Corporation Device 000e
        Flags: bus master, fast devsel, latency 0, NUMA node 0, IOMMU group 29
        Memory at 2bf80000000 (64-bit, prefetchable) [size=512K]
        Capabilities: <access denied>
        Kernel driver in use: qdma-pf
        Kernel modules: qdma_pf, ami, qdma_vf
```

## New card preparation

In order for VRT to work, the [AVED card installation](https://xilinx.github.io/AVED/latest/AVED%2BUpdating%2BFPT%2BImage%2Bin%2BFlash.html) process should be followed to prepare the card.

After the card installation is complete, partition 1 of the OSPI memory needs to be overwritten with the Segmented Configuration image. The Segmented Configuration image can be found at `/opt/amd/vrt/design.pdi`. In order to program this, run the following command:

```bash
sudo ami_tool cfgmem_program -d 21:00.0 -i /opt/amd/vrt/design.pdi -t primary -p 1
```
Replace `21:00.0` with your card's BDF.