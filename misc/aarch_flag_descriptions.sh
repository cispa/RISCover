# https://stackoverflow.com/a/78164621
ARM64_FEATURES="$(wget -qO- https://github.com/golang/sys/raw/master/cpu/cpu.go \
    | awk '/ARM64/,/}/')"
for feature in $(cut -d"-" -f2 "$1"); do
    printf "${feature}\t"
    echo "$ARM64_FEATURES" | grep -i "Has${feature}\s" | sed 's#.* // ##' | sed 's#SIMD double precision#SIMD dot product#'
    echo
done | column -t -s $'\t'
