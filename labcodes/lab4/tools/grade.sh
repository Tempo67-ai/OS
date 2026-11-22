#!/bin/sh
# Local reconstructed self-checks, not an official course grader.
set -eu
cd "$(dirname "$0")/.."
exec python3 tools/grade.py
