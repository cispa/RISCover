#!/usr/bin/env bash
FRAMEWORK_ROOT=$(realpath "$(dirname "${BASH_SOURCE[0]}")")
export FRAMEWORK_ROOT
echo "FRAMEWORK_ROOT=$FRAMEWORK_ROOT"

if [ -z "$ARCH" ]; then
    printf "\033[0;31mARCH env var is empty. Make sure to set it, e.g.: export ARCH=riscv64\033[0m\n"
    return 1
fi
echo "ARCH=$ARCH"

export PATH="$FRAMEWORK_ROOT"/bin:"$FRAMEWORK_ROOT"/client:"$PATH"

# TODO: this is probably not optimal, but the mkPythonEditablePackage is also hacky
export PYTHONPATH="$FRAMEWORK_ROOT${PYTHONPATH:+:${PYTHONPATH}}"
