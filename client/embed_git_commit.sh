#!/usr/bin/env bash
elf=$1
git_commit=$(git rev-parse HEAD 2>/dev/null || echo unknown)
echo "Git commit is: $git_commit"
echo -n "$git_commit" | "${OBJCOPY:=objcopy}" --update-section .gitcommit=/dev/stdin "$elf"
