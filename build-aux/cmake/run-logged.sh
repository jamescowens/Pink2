#!/bin/sh
# usage: run-logged.sh <log file> <command> [args...]
#
# Runs the command with its output (stdout and stderr) in the log file, and
# exits with its status. On a failure it prints the log's last 60 lines to
# stderr first.
#
# src/bdb48/CMakeLists.txt runs Berkeley DB's make through this rather than
# through ExternalProject's LOG_BUILD. LOG_BUILD runs the command from a
# `cmake -P` script, and CMake starts it through libuv, which on macOS closes
# every descriptor the child would inherit: the outer make's job server pipe
# among them, so BDB's make fell back to one job ("jobserver unavailable:
# using -j1"). A shell keeps them.

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <log file> <command> [args...]" >&2
    exit 2
fi
log=$1
shift
if "$@" >"$log" 2>&1; then
    exit 0
else
    status=$?
fi
echo "$0: '$*' failed with status $status. The end of $log:" >&2
tail -n 60 "$log" >&2
exit "$status"
