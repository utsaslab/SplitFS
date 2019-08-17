#!/bin/bash

usage() { echo "Usage: $0 -p </path/to/boost> -t <tree_name> exe" 1>&2; exit 1; }

echo "Rohan here"

while getopts ":p:t:" o; do
    case "${o}" in
        p)
            path=${OPTARG}
            ;;
        t)
            tree=${OPTARG}
            ;;
        *)
            usage
            ;;
    esac
done
shift $((OPTIND-1))

#echo "path = ${path}"

if [ -z "${path}" ] || [ -z "${tree}" ]; then
    usage
fi

export LD_LIBRARY_PATH=${path}:$LD_LIBRARY_PATH
export LD_PRELOAD="libnvp.so"

echo "Quill path = ${path}"

if [ ${path:((${#path} - 1))} == "/" ]; then
	tree_path=${path}"bin/"
else
	tree_path=${path}"/bin/"
fi

export NVP_TREE_FILE=${tree_path}${tree}
echo "tree = ${NVP_TREE_FILE}"

#echo "Commands are: $@"
#echo "run" > commandsFile
#gdb -x commandsFile --args $@
$@
