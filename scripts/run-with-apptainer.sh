#!/bin/bash

# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
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

set -exo pipefail

# Usage: scripts/run-with-apptainer.sh <run|package> <ubuntu|rocky>
#
# Builds (if necessary) and runs one of the SLASH Apptainer containers defined
# by scripts/<run|package>-<ubuntu|rocky>.def. The current working directory
# and the Xilinx tools install (plus optionally a separate license path) are
# bound into the container at the same paths they have on the host so that
# paths generated inside the container are also valid outside of it.
#
# Apptainer runs as the invoking host user by default, so no USER_ID build
# argument or in-container user creation is required.
#
# Modes:
#   package   Run the matching distro's packaging script
#             (scripts/package-deb.sh on Ubuntu, scripts/package-rpm.sh on
#             Rocky) inside a clean container that only has the build
#             dependencies installed.
#   run       Drop into an interactive bash shell inside a container that has
#             the freshly built SLASH packages already installed.
#
# Required environment variables:
#   SLASH_XILINX_PATH   Path to the Xilinx tools install on the host
#                       (e.g. /opt/Xilinx). Vivado is sourced from
#                       $SLASH_XILINX_PATH/2025.1/Vivado/settings64.sh inside
#                       the container.
#
# Optional environment variables:
#   SLASH_XILINX_ROOT              Mount point for the Xilinx tools inside the
#                                  container. Defaults to SLASH_XILINX_PATH so
#                                  paths match host and container.
#   SLASH_LICENSE_PATH             Path to the Xilinx license file (or
#                                  directory) on the host. When set, it is
#                                  bound into the container and exported as
#                                  XILINXD_LICENSE_FILE. When unset (typical
#                                  for installs under /opt/Xilinx or
#                                  /tools/Xilinx where the license lives
#                                  inside the Xilinx tree already bound via
#                                  SLASH_XILINX_PATH), Vivado's default
#                                  license discovery is used.
#   SLASH_PKG_SKIP_ROOT_DESIGN_BUILD  If set, forwarded into the container so
#                                     that pbuild.sh skips the (expensive)
#                                     root-design build step.
#   SLASH_APPTAINER_DIR            Directory in which to store built .sif
#                                  images. Defaults to build/apptainer under
#                                  the current working directory.
#
# Examples:
#   scripts/run-with-apptainer.sh package ubuntu   # build .deb packages
#   scripts/run-with-apptainer.sh package rocky    # build .rpm packages
#   scripts/run-with-apptainer.sh run     ubuntu   # interactive shell with
#                                                  # the .debs preinstalled

if [ $# -ne 2 ]; then
    echo "Usage: <run|package> <ubuntu|rocky>" >&2
    exit 1
fi

CONTAINER=$1
DISTRO=$2

if [ -z "$SLASH_XILINX_PATH" ]; then
    echo "Please set SLASH_XILINX_PATH to the path of your Xilinx tools installation (e.g. /opt/Xilinx)" >&2
    exit 1
fi

if [ -z "$SLASH_XILINX_ROOT" ]; then
    SLASH_XILINX_ROOT=$SLASH_XILINX_PATH
fi

# Validate distro
case "$DISTRO" in
    ubuntu) PACKAGE_SCRIPT="./scripts/package-deb.sh" ; ARTIFACTS_DIR="deb" ;;
    rocky)  PACKAGE_SCRIPT="./scripts/package-rpm.sh" ; ARTIFACTS_DIR="rpm" ;;
    *) echo "Unknown Linux distro $DISTRO" >&2 ; exit 1 ;;
esac

# Validate container kind
case "$CONTAINER" in
    package|run) ;;
    *) echo "Unknown container definition $CONTAINER" >&2 ; exit 1 ;;
esac

REPO_ROOT="$(pwd)"
DEF_FILE="$REPO_ROOT/scripts/$CONTAINER-$DISTRO.def"
SIF_DIR="${SLASH_APPTAINER_DIR:-$REPO_ROOT/build/apptainer}"
SIF_FILE="$SIF_DIR/slash-$CONTAINER-$DISTRO.sif"

mkdir -p "$SIF_DIR"

# Decide whether the SIF needs to be (re)built. The image is rebuilt when it
# does not yet exist, when the .def file is newer than the image, or - for the
# run-* images, which bake the freshly built packages into the image via the
# %files section - when any package in the artifacts directory is newer than
# the image.
needs_build=0
if [ ! -f "$SIF_FILE" ]; then
    needs_build=1
elif [ "$DEF_FILE" -nt "$SIF_FILE" ]; then
    needs_build=1
elif [ "$CONTAINER" = "run" ]; then
    if [ ! -d "$REPO_ROOT/$ARTIFACTS_DIR" ]; then
        echo "ERROR: $ARTIFACTS_DIR/ not found. Run 'scripts/run-with-apptainer.sh package $DISTRO' first." >&2
        exit 1
    fi
    newest_pkg="$(find "$REPO_ROOT/$ARTIFACTS_DIR" -maxdepth 1 -type f \( -name '*.deb' -o -name '*.rpm' \) -newer "$SIF_FILE" -print -quit)"
    if [ -n "$newest_pkg" ]; then
        needs_build=1
    fi
fi

if [ "$needs_build" -eq 1 ]; then
    # Apptainer's %files section resolves source paths relative to the current
    # working directory at build time, so run the build from the repo root.
    apptainer build --force "$SIF_FILE" "$DEF_FILE"
fi

# Assemble the bind list. Apptainer auto-binds $PWD and $HOME, but we add
# explicit binds for the Xilinx tools (and optionally the license) so the
# in-container paths match the host paths exactly.
APPTAINER_ARGS=()
APPTAINER_ARGS+=(--cleanenv)
APPTAINER_ARGS+=(--bind "$PWD:$PWD")
APPTAINER_ARGS+=(--pwd "$PWD")
APPTAINER_ARGS+=(--bind "$SLASH_XILINX_ROOT:$SLASH_XILINX_ROOT")

if [ -n "$SLASH_LICENSE_PATH" ]; then
    APPTAINER_ARGS+=(--bind "$SLASH_LICENSE_PATH:$SLASH_LICENSE_PATH")
    APPTAINER_ARGS+=(--env "XILINXD_LICENSE_FILE=$SLASH_LICENSE_PATH")
fi

if [ -n "$SLASH_PKG_SKIP_ROOT_DESIGN_BUILD" ]; then
    APPTAINER_ARGS+=(--env "SLASH_PKG_SKIP_ROOT_DESIGN_BUILD=$SLASH_PKG_SKIP_ROOT_DESIGN_BUILD")
fi

# Build the in-container command. Source Vivado and extend LD_LIBRARY_PATH for
# simulation, then either drop to bash or run the packaging script.
APPTAINER_COMMAND="source $SLASH_XILINX_PATH/2025.1/Vivado/settings64.sh "
APPTAINER_COMMAND+="&& export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:$SLASH_XILINX_PATH/2025.1/Vivado/lib/lnx64.o "
if [ "$CONTAINER" = "package" ]; then
    APPTAINER_COMMAND+="&& $PACKAGE_SCRIPT"
    apptainer exec "${APPTAINER_ARGS[@]}" "$SIF_FILE" bash -c "$APPTAINER_COMMAND"
else
    APPTAINER_COMMAND+="&& exec bash"
    apptainer shell "${APPTAINER_ARGS[@]}" "$SIF_FILE" -c "$APPTAINER_COMMAND"
fi
