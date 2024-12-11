#!/bin/bash

# Script to find the PID of the last process named "count" and run ./ptrace with it

# Find the PID(s) of the process(es) named "count"
pids=$(ps -A | grep count | awk '{print $1}')

# Check if any PIDs were found
if [ -z "$pids" ]; then
    echo "Process 'count' not found."
    exit 1
fi

# Get the last PID from the list
pid=$(echo "$pids" | tail -n 1)

echo "Using 'count' process with PID: $pid"

# Run the ./ptrace program with the PID
echo "Running ./ptrace with PID $pid"
./build/checkpoint "$pid" counter_dump.bin

# Check the exit status of ./ptrace
if [ $? -ne 0 ]; then
    echo "ptrace failed on PID $pid"
else
    echo "ptrace completed successfully on PID $pid"
fi
