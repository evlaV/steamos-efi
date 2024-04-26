#!/bin/bash

set -eu
shopt -s nullglob

declare -r SELF=$(readlink -f "$0")
declare -r DUMPER=$(dirname "$SELF")/objcopy-sections.sh
declare -r LIB_DIR=${1:-/usr/lib}
declare object=
declare -a section_data=()
declare -A targets=()
declare section
declare -a output=()

while read -r object
do
    #echo "# parsing $object" >&2
    while read -a section_data
    do
        case ${section_data[0]} in
            ctors*.o) continue ;;
            *.efi)    true     ;;
            *)        continue ;;
        esac

        #echo "# found: ${section_data[@]}" >&2
        for section in "${section_data[@]:1}"
        do
            targets["$section"]=1
        done
    done < <("$DUMPER" "${LIB_DIR}"/gnuefi/apps/"$object")
done < <(ls -1 "${LIB_DIR}"/gnuefi/apps 2>/dev/null || :)

for section in "${!targets[@]}"
do
    case $section in
        # we already know we want these
        data|dynamic|dynsym|rel*|rela*|sdata|text)
            true
            ;;
        # these we explicitly don't care about
        comment|debug_*|eh_frame|note.*|data.*.local)
            true
            ;;
        *)
            output+=("$section")
            ;;
    esac
done

echo "${output[@]}"
