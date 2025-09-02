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

import argparse
import glob
import subprocess
import shutil
import os

ROOT_PATH = os.path.realpath(".")
DESIGN_PDI_PATH = os.path.join(ROOT_PATH, "..")
RESOURCES_PATH = os.path.realpath("../../submodules/v80-vitis-flow/resources/")
COMPUTE_EXAMPLE_DIR = os.path.realpath("../../examples/05_perf/")
DEPLOY_PROJECT_COMPUTE = os.path.join(os.getcwd(), "deploy_project_compute")
HLS_DIR_COMPUTE = os.path.join(DEPLOY_PROJECT_COMPUTE, "hls/")
AVED_ROOT_DIR_COMPUTE = os.path.join(DEPLOY_PROJECT_COMPUTE, "build/v80-vitis-flow/build/aved-fork/hw/amd_v80_gen5x8_24.1/")
AVED_SRC_DIR_COMPUTE = os.path.join(AVED_ROOT_DIR_COMPUTE, "src/")
AVED_IPREPO_DIR_COMPUTE = os.path.join(AVED_SRC_DIR_COMPUTE, "iprepo/")
LINKER_SRC_DIR_COMPUTE = os.path.join(DEPLOY_PROJECT_COMPUTE, "build/v80-vitis-flow/")
LINKER_BUILD_DIR_COMPUTE = os.path.join(LINKER_SRC_DIR_COMPUTE, "build/")
AVED_DIR_SUBMODULE_COMPUTE = os.path.join(DEPLOY_PROJECT_COMPUTE, LINKER_SRC_DIR_COMPUTE, "submodules/aved/")
AVED_DIR_COMPUTE_BUILD = os.path.join(DEPLOY_PROJECT_COMPUTE, LINKER_SRC_DIR_COMPUTE, "build/aved-fork/")
CONFIG_FILE_PATH_COMPUTE = os.path.join(DEPLOY_PROJECT_COMPUTE, "config.cfg")

TCL_DIR = os.path.realpath("./tcl/")
CREATE_DESIGN_DIR = os.path.join(TCL_DIR, "create_design.tcl")
NOC_SOLUTION_DIR = os.path.join(TCL_DIR, "noc_solution.tcl")
EXPORT_NOC_DIR = os.path.join(TCL_DIR, "export_noc.tcl")
SEGMENTED_IMG_BIF = os.path.join(TCL_DIR, "segmented_img.bif")



def run_linker(CONFIG_PATH, KERNEL_PATHS):
    os.chdir(LINKER_BUILD_DIR_COMPUTE)
    cmd = [
        "./v80++-linker",
        "--cfg", CONFIG_PATH,
        "--platform", "hw",
        "--segmented"
    ]
    cmd.append("--kernels")
    cmd.extend(KERNEL_PATHS)

    subprocess.run(cmd, check=True)
    print("Linker run completed.")

def run_hw():
    os.chdir(AVED_ROOT_DIR_COMPUTE)
    subprocess.run(["./build_all.sh"], check=False)
    print("Hardware build completed.")

def setup_step(platform):
    if platform == "compute":
        os.chdir(COMPUTE_EXAMPLE_DIR)
        subprocess.run(["make", "setup"])
        shutil.copytree(COMPUTE_EXAMPLE_DIR, DEPLOY_PROJECT_COMPUTE, dirs_exist_ok=True)
    elif platform == "eth":
        print("Eth mode not supported yet.")
    else:
        raise ValueError("Invalid platform specified.")
    
def hls_step(platform):
    if platform == "compute":
        os.chdir(DEPLOY_PROJECT_COMPUTE)
        subprocess.run(["make", "hls"])
    elif platform == "eth":
        print("Eth mode not supported yet.")
    else:
        raise ValueError("Invalid platform specified.")
    
def linker_step(platform):
    if platform == "compute":
        os.chdir(LINKER_SRC_DIR_COMPUTE)
        os.makedirs("build", exist_ok=True)
        subprocess.run(["cmake", ".."], cwd="build", check=True)
        subprocess.run(["make", "-j", "4"], cwd="build", check=True)
        shutil.copytree(AVED_DIR_SUBMODULE_COMPUTE, AVED_DIR_COMPUTE_BUILD, dirs_exist_ok=True)
        shutil.copy(CREATE_DESIGN_DIR, os.path.join(AVED_SRC_DIR_COMPUTE, "create_design.tcl"))
        shutil.copy(NOC_SOLUTION_DIR, os.path.join(AVED_SRC_DIR_COMPUTE, "noc_solution.tcl"))
        shutil.copy(EXPORT_NOC_DIR, os.path.join(AVED_SRC_DIR_COMPUTE, "export_noc.tcl"))
        shutil.copy(SEGMENTED_IMG_BIF, os.path.join(AVED_ROOT_DIR_COMPUTE, "segmented_img.bif"))
        # Copying HLS to iprepo
        shutil.copytree(HLS_DIR_COMPUTE, AVED_IPREPO_DIR_COMPUTE, dirs_exist_ok=True)
        build_dirs = [d for d in glob.glob(os.path.join(HLS_DIR_COMPUTE, "build_*"))
              if os.path.isdir(d)]
        sol1_paths = [os.path.join(d, "sol1") for d in build_dirs]
        run_linker(CONFIG_FILE_PATH_COMPUTE, sol1_paths)
        shutil.copy(os.path.join(LINKER_BUILD_DIR_COMPUTE, "run_pre.tcl"), AVED_SRC_DIR_COMPUTE)
        shutil.copy(os.path.join(RESOURCES_PATH, "run_post.tcl"), AVED_SRC_DIR_COMPUTE)
    elif platform == "eth":
        print("Eth mode not supported yet.")
    else:
        raise ValueError("Invalid platform specified.")

def hw_step(platform):
    if platform == "compute":
        run_hw()
    elif platform == "eth":
        print("Eth mode not supported yet.")
    else:
        raise ValueError("Invalid platform specified.")

def generate_pdi_step(platform):
    if platform == "compute":
        os.chdir(AVED_ROOT_DIR_COMPUTE)
        # No FPT generation for now
        subprocess.run(["bootgen", "-arch", "versal", "-image", "segmented_img.bif", "-w", "-o", "design.pdi"], check=True)
        shutil.copy(os.path.join(AVED_ROOT_DIR_COMPUTE, "design.pdi"), os.path.join(ROOT_PATH, "../design.pdi"))
    elif platform == "eth":
        print("Eth mode not supported yet.")
    else:
        raise ValueError("Invalid platform specified.")

def generate_noc_solution_step(platform):
    if platform == "compute":
        os.chdir(AVED_ROOT_DIR_COMPUTE)
        # subprocess.run(["vivado", "-mode", "tcl", "-source", "src/export_noc.tcl"], check=True)
        # shutil.copy(os.path.join(AVED_ROOT_DIR_COMPUTE, "noc_sol.ncr"), os.path.join(RESOURCES_PATH, "noc_sol_compute.ncr"))
        shutil.copy(os.path.join(RESOURCES_PATH, "noc_sol.ncr"), os.path.join(RESOURCES_PATH, "noc_sol_compute.ncr"))
    elif platform == "eth":
        print("Eth mode not supported yet.")
    else:
        raise ValueError("Invalid platform specified.")

STEPS = [
    ("setup_step", setup_step),
    ("hls_step", hls_step),
    ("linker_step", linker_step),
    ("hw_step", hw_step),
    ("generate_pdi_step", generate_pdi_step),
    ("generate_noc_solution_step", generate_noc_solution_step)
]

def main():
    parser = argparse.ArgumentParser(description="Platform build driver with step range.")
    parser.add_argument("--platform", choices=["compute", "eth"], required=True)
    parser.add_argument("--from_step", type=str, default=STEPS[0][0], help="Step name or index to start from.")
    parser.add_argument("--to_step", type=str, help="(Optional) Step name or index to end at.")
    parser.add_argument("--list_steps", action="store_true", help="List all available steps and exit.")
    args = parser.parse_args()

    if args.list_steps:
        print("Available steps:")
        for i, (name, _) in enumerate(STEPS, 1):
            print(f"{i}. {name}")
        return

    if args.platform != "compute":
        print("Eth mode not supported yet.")
        return

    def step_index(step_id):
        if step_id.isdigit():
            idx = int(step_id) - 1
        else:
            idx = next((i for i, (name, _) in enumerate(STEPS) if name == step_id), -1)
        if idx < 0 or idx >= len(STEPS):
            raise ValueError(f"Invalid step: {step_id}")
        return idx

    from_idx = step_index(args.from_step)
    to_idx = step_index(args.to_step) if args.to_step else len(STEPS) - 1

    if from_idx > to_idx:
        raise ValueError("--from_step must come before or equal to --to_step")

    for i in range(from_idx, to_idx + 1):
        name, func = STEPS[i]
        print(f"\n--- Running Step {i + 1}: {name} ---")
        func(args.platform)

    # if args.platform == "compute":
    #     print("Running in compute mode.")
    #     os.chdir(COMPUTE_EXAMPLE_DIR)
    #     subprocess.run(["make", "setup"])
    #     shutil.copytree(COMPUTE_EXAMPLE_DIR, DEPLOY_PROJECT_COMPUTE, dirs_exist_ok=True)
    #     os.chdir(DEPLOY_PROJECT_COMPUTE)
    #     # Setup the deployment project
    #     subprocess.run(["make", "hls"])
    #     os.chdir(LINKER_SRC_DIR_COMPUTE)
    #     os.makedirs("build", exist_ok=True)
    #     subprocess.run(["cmake", ".."], cwd="build", check=True)
    #     subprocess.run(["make", "-j", "4"], cwd="build", check=True)
    #     shutil.copytree(AVED_DIR_SUBMODULE_COMPUTE, AVED_DIR_COMPUTE_BUILD, dirs_exist_ok=True)
    #     shutil.copy(CREATE_DESIGN_DIR, os.path.join(AVED_SRC_DIR_COMPUTE, "create_design.tcl"))
    #     shutil.copy(NOC_SOLUTION_DIR, os.path.join(AVED_SRC_DIR_COMPUTE, "noc_solution.tcl"))
    #     shutil.copy(EXPORT_NOC_DIR, os.path.join(AVED_SRC_DIR_COMPUTE, "export_noc.tcl"))
    #     shutil.copy(SEGMENTED_IMG_BIF, os.path.join(AVED_ROOT_DIR_COMPUTE, "segmented_img.bif"))
    #     # Copying HLS to iprepo
    #     shutil.copytree(HLS_DIR_COMPUTE, AVED_IPREPO_DIR_COMPUTE, dirs_exist_ok=True)
    #     build_dirs = [d for d in glob.glob(os.path.join(HLS_DIR_COMPUTE, "build_*"))
    #           if os.path.isdir(d)]
    #     sol1_paths = [os.path.join(d, "sol1") for d in build_dirs]
    #     run_linker(CONFIG_FILE_PATH_COMPUTE, sol1_paths)
    #     shutil.copy(os.path.join(LINKER_BUILD_DIR_COMPUTE, "run_pre.tcl"), AVED_SRC_DIR_COMPUTE)
    #     run_hw()
    #     os.chdir(AVED_ROOT_DIR_COMPUTE)
    #     # No FPT generation for now
    #     subprocess.run(["bootgen", "-arch", "versal", "-image", "segmented_img.bif", "-w", "-o", "design.pdi"], check=True)
    #     shutil.copy(os.path.join(AVED_ROOT_DIR_COMPUTE, "design.pdi"), os.path.join(ROOT_PATH, "design.pdi"))
    #     # Generate NoC solution from the vivado project
    #     subprocess.run(["vivado", "-mode", "batch", "-source", "export_noc.tcl"], check=True)
    #     shutil.copy(os.path.join(AVED_ROOT_DIR_COMPUTE, "noc_sol.ncr"), os.path.join(RESOURCES_PATH, "noc_sol_compute.ncr"))
    # elif args.platform == "eth":
    #     print("Eth mode not supported yet.")

if __name__ == "__main__":
    main()