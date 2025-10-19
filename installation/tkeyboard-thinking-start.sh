#!/bin/bash
# T-Keyboard Thinking Start Hook
# Runs before Claude executes tools

# Send thinking state
curl -s -X POST http://localhost:8081/state/update \
  -H 'Content-Type: application/json' \
  -d '{"event":"thinking_start"}' > /dev/null 2>&1

# Update buttons to show STOP
curl -s -X POST http://localhost:8081/update \
  -H 'Content-Type: application/json' \
  -d '{"buttons":["STOP","","",""],"images":["stop.rgb","","",""]}' > /dev/null 2>&1
