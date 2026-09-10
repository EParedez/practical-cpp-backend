#!/usr/bin/env bash
# Start the local MongoDB instance used by the project (Mac/Homebrew).
set -euo pipefail

DATA_DIR="/tmp/practical-cpp-mongo"
LOG="$DATA_DIR/mongod.log"

mkdir -p "$DATA_DIR"

if pgrep -x mongod >/dev/null 2>&1; then
  echo "mongod already running."
else
  mongod --dbpath "$DATA_DIR" --port 27017 --fork --logpath "$LOG"
  echo "mongod started (log: $LOG)."
fi
