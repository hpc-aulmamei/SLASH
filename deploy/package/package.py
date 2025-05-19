#!/usr/bin/env python3

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

import os
import sys
import subprocess
import shutil
from datetime import datetime

PACKAGE_NAME = "amd-vrt"
MAINTAINER = "AMD <support@amd.com>"
ARCHITECTURE = "amd64"
DESCRIPTION = "AMD V80 Runtime API, SMI and PCIe driver package"
DEPENDS = "libxml2, libzmq3-dev, libjsoncpp-dev"

def get_version_from_header(repo_root):
    """Extract version from vrt_version.hpp header file"""
    version_file = os.path.join(repo_root, "vrt", "include", "api", "vrt_version.hpp")
    
    if not os.path.exists(version_file):
        print(f"Warning: Version file not found at {version_file}")
        return "1.0.0"  # Default version if file not found
    
    major = "1"
    minor = "0"
    patch = "0"
    git_tag = ""
    
    try:
        with open(version_file, 'r') as f:
            content = f.read()
            import re

            major_match = re.search(r'#define\s+VRT_VERSION_MAJOR\s+(\d+)', content)
            if major_match:
                major = major_match.group(1)
                
            minor_match = re.search(r'#define\s+VRT_VERSION_MINOR\s+(\d+)', content)
            if minor_match:
                minor = minor_match.group(1)
                
            patch_match = re.search(r'#define\s+VRT_VERSION_PATCH\s+(\d+)', content)
            if patch_match:
                patch = patch_match.group(1)
                
            git_tag_match = re.search(r'#define\s+GIT_TAG\s+"([^"]+)"', content)
            if git_tag_match:
                git_tag = git_tag_match.group(1)
    
        version = f"{major}.{minor}.{patch}"
        
        if version == "1.0.0" and git_tag.startswith('v'):
            version = git_tag[1:]
            print(f"Using version from GIT_TAG: {version}")
        else:
            print(f"Extracted version from components: {version}")
            
        return version
    
    except Exception as e:
        print(f"Error extracting version from header file: {e}")
        return "1.0.0"

def run_command(cmd, cwd=None):
    """Run a shell command and return the output"""
    try:
        result = subprocess.run(cmd, shell=True, check=True, text=True, 
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd)
        return result.stdout
    except subprocess.CalledProcessError as e:
        print(f"Error executing command: {cmd}")
        print(f"Error output: {e.stderr}")
        sys.exit(1)

def create_temp_directory():
    """Create a temporary directory structure for building the deb package"""
    timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    
    repo_root = os.path.abspath(os.getcwd())
    
    output_dir = os.path.join(repo_root, "deploy", "output")
    os.makedirs(output_dir, exist_ok=True)
    
    temp_dir = os.path.join(output_dir, f"amd-vrt-{timestamp}")
    os.makedirs(temp_dir, exist_ok=True)
    
    print(f"Created build directory: {temp_dir}")
    
    debian_dir = os.path.join(temp_dir, "DEBIAN")
    os.makedirs(debian_dir, exist_ok=True)
    
    os.makedirs(os.path.join(temp_dir, "usr/local/bin"), exist_ok=True)
    os.makedirs(os.path.join(temp_dir, "usr/local/lib"), exist_ok=True)
    os.makedirs(os.path.join(temp_dir, "usr/local/vrt/include"), exist_ok=True)
    os.makedirs(os.path.join(temp_dir, "usr/src/pcie-hotplug-drv"), exist_ok=True)
    os.makedirs(os.path.join(temp_dir, "opt/amd/vrt"), exist_ok=True)

    return temp_dir

def copy_design_pdi(repo_root, temp_dir):
    """Copy the design.pdi file to the package directory"""
    pdi_src = os.path.join(repo_root, "deploy", "design.pdi")
    pdi_dst = os.path.join(temp_dir, "opt/amd/vrt/design.pdi") 
    
    if os.path.exists(pdi_src):
        shutil.copy2(pdi_src, pdi_dst)
        print("design.pdi copied to package")
    else:
        print(f"Warning: design.pdi not found at {pdi_src}")

def build_and_copy_vrt(repo_root, temp_dir):
    vrt_dir = os.path.join(repo_root, "vrt")
    build_dir = os.path.join(vrt_dir, "build")
    
    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    os.makedirs(build_dir, exist_ok=True)
    
    run_command("cmake ..", cwd=build_dir)
    run_command("make -j9", cwd=build_dir)
    
    lib_dir = os.path.join(build_dir, "lib")
    for lib_file in os.listdir(lib_dir):
        if lib_file.startswith("libvrt") and (lib_file.endswith(".so") or lib_file.endswith(".a")):
            shutil.copy2(
                os.path.join(lib_dir, lib_file),
                os.path.join(temp_dir, "usr/local/lib", lib_file)
            )
    
    include_src = os.path.join(vrt_dir, "include")
    include_dst = os.path.join(temp_dir, "usr/local/vrt/include")
    if os.path.exists(include_src):
        for root, dirs, files in os.walk(include_src):
            for file in files:
                if file.endswith((".h", ".hpp")):
                    rel_path = os.path.relpath(root, include_src)
                    dst_dir = os.path.join(include_dst, rel_path)
                    os.makedirs(dst_dir, exist_ok=True)
                    shutil.copy2(
                        os.path.join(root, file),
                        os.path.join(dst_dir, file)
                    )
    
    scripts_src = os.path.join(vrt_dir, "scripts")
    if os.path.exists(scripts_src):
        scripts_dst = os.path.join(temp_dir, "usr/local/vrt/")
        os.makedirs(scripts_dst, exist_ok=True)
        for item in os.listdir(scripts_src):
            s = os.path.join(scripts_src, item)
            d = os.path.join(scripts_dst, item)
            if os.path.isfile(s):
                shutil.copy2(s, d)
                if s.endswith((".sh", ".py")) or os.access(s, os.X_OK):
                    os.chmod(d, 0o755)
    
    print("VRT API built and files copied to package")

def build_and_copy_smi(repo_root, temp_dir):
    """Build SMI CLI app and copy files to the package directory"""
    smi_dir = os.path.join(repo_root, "smi")
    build_dir = os.path.join(smi_dir, "build")
    
    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    os.makedirs(build_dir, exist_ok=True)
    
    run_command("cmake ..", cwd=build_dir)
    run_command("make -j9", cwd=build_dir)
    
    for root, _, files in os.walk(build_dir):
        for file in files:
            if file in ["v80-smi"] or (file.endswith("-smi") and os.access(os.path.join(root, file), os.X_OK)):
                print(f"Found SMI binary: {file}")
                shutil.copy2(
                    os.path.join(root, file),
                    os.path.join(temp_dir, "usr/local/bin", file)
                )
                os.chmod(os.path.join(temp_dir, "usr/local/bin", file), 0o755)
    
    print("SMI CLI app built and files copied to package")

def copy_pcie_driver(repo_root, temp_dir):
    """Copy PCIe hotplug driver source to the package directory"""
    driver_src = os.path.join(repo_root, "submodules/pcie-hotplug-drv")
    driver_dst = os.path.join(temp_dir, "usr/src/pcie-hotplug-drv")
    
    if not os.path.exists(driver_src):
        print(f"Warning: PCIe driver directory not found at {driver_src}")
        return
    
    for item in os.listdir(driver_src):
        s = os.path.join(driver_src, item)
        d = os.path.join(driver_dst, item)
        
        if os.path.isdir(s):
            shutil.copytree(s, d, symlinks=True)
        else:
            shutil.copy2(s, d)
    
    print("PCIe hotplug driver source copied to package")

def create_debian_control_file(temp_dir):
    """Create the DEBIAN/control file"""
    control_path = os.path.join(temp_dir, "DEBIAN/control")
    
    with open(control_path, "w") as control:
        control.write(f"""Package: {PACKAGE_NAME}
Version: {VERSION}
Architecture: {ARCHITECTURE}
Maintainer: {MAINTAINER}
Depends: {DEPENDS}
Section: utils
Priority: optional
Homepage: https://www.amd.com/
Description: {DESCRIPTION}
 This package includes:
 * VRT API - Runtime API for AMD V80 acceleration
 * SMI CLI - System Management Interface command-line utility
 * PCIe hotplug driver - Driver for PCIe hotplug functionality
""")

def create_postinst_script(temp_dir):
    """Create post-installation script"""
    script_path = os.path.join(temp_dir, "DEBIAN/postinst")
    
    with open(script_path, "w") as script:
        script.write("""#!/bin/bash
set -e

# Add libraries to ldconfig
echo "/usr/local/lib" > /etc/ld.so.conf.d/amd-vrt.conf
ldconfig

# Build and install PCIe hotplug driver
if [ -d "/usr/src/pcie-hotplug-drv" ]; then
    echo "Building PCIe hotplug driver..."
    cd /usr/src/pcie-hotplug-drv
    make clean
    make
    
    # Install the driver
    make install
    
    # Add the module to modules-load.d to ensure it loads at boot
    echo "Configuring pcie_hotplug module to load at boot..."
    echo "pcie_hotplug" > /etc/modules-load.d/amd-vrt.conf
    
    # Create comprehensive udev rules first
    echo "Creating VRT device permission rules..."
    cat > /etc/udev/rules.d/99-amd-vrt-permissions.rules << 'EOF'
# For PCIe hotplug devices - match on both the specific name and wildcard
KERNEL=="pcie_hotplug", MODE="0666", GROUP="users"
KERNEL=="pcie_hotplug*", MODE="0666", GROUP="users"
EOF
    
    # Reload rules before loading the module
    udevadm control --reload-rules
    
    # Create a persistent device setup script that also loads the module if needed
    cat > /usr/local/bin/vrt-setup-devices.sh << 'EOF'
#!/bin/bash
# This script ensures VRT devices have proper permissions and the module is loaded
# It runs both at boot time and can be run manually after module reloading

# Check if module is loaded, if not, load it
if ! lsmod | grep -q "pcie_hotplug"; then
    echo "Loading pcie_hotplug module..."
    modprobe pcie_hotplug || echo "Warning: Failed to load pcie_hotplug module"
    # Give udev time to create device nodes
    sleep 1
fi

# Set permissions for all VRT-related devices
for dev in /dev/pcie_hotplug*; do
  if [ -e "$dev" ]; then
    chmod 666 "$dev" || echo "Warning: Failed to set permissions for $dev"
    chown root:users "$dev" || echo "Warning: Failed to change owner for $dev"
    echo "Set permissions for $dev"
  fi
done
EOF
    
    chmod +x /usr/local/bin/vrt-setup-devices.sh
    
    # Create systemd service to run at boot
    cat > /etc/systemd/system/vrt-devices.service << 'EOF'
[Unit]
Description=VRT Device Permissions
After=systemd-udev-settle.service
After=systemd-modules-load.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/vrt-setup-devices.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
    
    # Enable the service for future boots
    systemctl daemon-reload
    systemctl enable vrt-devices.service
    
    # Now load the module
    if lsmod | grep -q "pcie_hotplug"; then
        echo "Reloading pcie_hotplug module..."
        rmmod pcie_hotplug || echo "Warning: Failed to unload pcie_hotplug module"
        modprobe pcie_hotplug || echo "Warning: Failed to load pcie_hotplug module"
    else
        echo "Loading pcie_hotplug module..."
        modprobe pcie_hotplug || echo "Warning: Failed to load pcie_hotplug module"
    fi
    
    # Apply permissions immediately after module loading
    udevadm trigger
    sleep 1  # Give udev a moment to create the device nodes
    /usr/local/bin/vrt-setup-devices.sh
fi

exit 0
""")
    
    os.chmod(script_path, 0o755)

def create_prerm_script(temp_dir):
    """Create pre-removal script"""
    script_path = os.path.join(temp_dir, "DEBIAN/prerm")
    
    with open(script_path, "w") as script:
        script.write("""#!/bin/bash
set -e

# Unload the PCIe hotplug driver if loaded
if lsmod | grep -q "pcie_hotplug"; then
    echo "Unloading pcie_hotplug module..."
    rmmod pcie_hotplug || echo "Warning: Failed to unload pcie_hotplug module"
fi

exit 0
""")
    
    os.chmod(script_path, 0o755)

def create_postrm_script(temp_dir):
    """Create post-removal script"""
    script_path = os.path.join(temp_dir, "DEBIAN/postrm")
    
    with open(script_path, "w") as script:
        script.write("""#!/bin/bash
set -e

# Remove module loading configuration
if [ -f "/etc/modules-load.d/amd-vrt.conf" ]; then
    rm -f /etc/modules-load.d/amd-vrt.conf
fi

# Remove udev rules
if [ -f "/etc/udev/rules.d/99-amd-vrt-permissions.rules" ]; then
    rm -f /etc/udev/rules.d/99-amd-vrt-permissions.rules
    udevadm control --reload-rules
fi

# Remove systemd service
if [ -f "/etc/systemd/system/vrt-devices.service" ]; then
    systemctl disable vrt-devices.service || true
    systemctl stop vrt-devices.service || true
    rm -f /etc/systemd/system/vrt-devices.service
    systemctl daemon-reload
fi

# Remove device setup script
if [ -f "/usr/local/bin/vrt-setup-devices.sh" ]; then
    rm -f /usr/local/bin/vrt-setup-devices.sh
fi

# Remove ldconfig configuration
if [ -f "/etc/ld.so.conf.d/amd-vrt.conf" ]; then
    rm -f /etc/ld.so.conf.d/amd-vrt.conf
    ldconfig
fi

exit 0
""")
    
    os.chmod(script_path, 0o755)

def build_deb_package(temp_dir, output_dir):
    """Build the Debian package from the temporary directory"""
    timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    deb_filename = f"{PACKAGE_NAME}_{VERSION}_{timestamp}_{ARCHITECTURE}.deb"
    deb_path = os.path.join(output_dir, deb_filename)
    
    run_command(f"dpkg-deb --build {temp_dir} {deb_path}")
    
    print(f"Package created: {deb_path}")
    return deb_path

def main():
    repo_root = os.path.abspath(os.getcwd())
    print(f"Repository root directory: {repo_root}")
    global VERSION
    VERSION = get_version_from_header(repo_root)
    output_dir = os.path.join(repo_root, "deploy", "output")
    os.makedirs(output_dir, exist_ok=True)
    
    temp_dir = create_temp_directory()
    
    try:
        build_and_copy_vrt(repo_root, temp_dir)
        build_and_copy_smi(repo_root, temp_dir)
        copy_pcie_driver(repo_root, temp_dir)
        copy_design_pdi(repo_root, temp_dir)

        create_debian_control_file(temp_dir)
        create_postinst_script(temp_dir)
        create_prerm_script(temp_dir)
        create_postrm_script(temp_dir)
        
        deb_path = build_deb_package(temp_dir, temp_dir)
        
        print(f"\nPackage successfully created at: {deb_path}")
        print(f"To install: sudo apt install {deb_path}")
        
    finally:
        pass

if __name__ == "__main__":
    main()