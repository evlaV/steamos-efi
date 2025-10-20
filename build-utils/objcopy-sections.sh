#!/bin/bash

# Copyright © 2024-2025 Collabora Ltd.
# Copyright © 2024-2025 Valve Corporation

set -eu
shopt -s extglob

declare -r TEMPLATE=${1:-/usr/lib/libefi.a}
declare label data extra more
declare filename
declare -i in_sections=0
declare -A sections=()

while read label data extra more
do
    case $label in
        Sections:)
            #echo "S [${label}]" "[${data}]" "[${extra}]"
            in_sections=1
            sections=()
            ;;
        *:|SYMBOL)
            in_sections=0
            if [ "${data}-${extra}" = file-format ]
            then
                #echo "X [${label}]" "[${data}]" "[${extra}]"
                filename=${label%:}
            fi

            if [ ${#sections[@]} -gt 0 ]
            then
                echo "$filename" "${!sections[@]}"
                sections=()
            fi
            ;;
    esac

    if [ $in_sections -eq 1  ]
    then
        case $label in
            +([0-9]))
                #echo "s <${label}>" "<${data}>" "<${extra}>"
                case $data in
                    .?*)
                        sections["${data#.}"]=1
                        ;;
                esac
                ;;
        esac
    fi
done < <(objdump -x "${TEMPLATE}")
