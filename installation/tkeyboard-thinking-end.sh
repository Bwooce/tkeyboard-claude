#!/bin/bash
# T-Keyboard Thinking End Hook
# Runs after Claude finishes executing tools

# Send idle state
curl -s -X POST http://localhost:8081/state/update \
  -H 'Content-Type: application/json' \
  -d '{"event":"thinking_end"}' > /dev/null 2>&1

# Update buttons to default options
curl -s -X POST http://localhost:8081/update \
  -H 'Content-Type: application/json' \
  -d '{"buttons":["Yes","No","Proceed","Help"],"actions":["Yes","No","Proceed","Help"],"images":["yes.rgb","no.rgb","proceed.rgb","help.rgb"]}' > /dev/null 2>&1
