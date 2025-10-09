#!/usr/bin/env bash
elf=$1
hash=$(md5sum "$elf" | awk '{print $1}')
echo "ELF hash is: $hash"
echo -n "$hash" | "${OBJCOPY:=objcopy}" --update-section .elfhash=/dev/stdin "$elf"
