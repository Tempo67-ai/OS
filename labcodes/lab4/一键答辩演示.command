#!/bin/sh
# macOS: double-click; Linux/WSL: bash this-file --auto
cd "$(dirname "$0")" || exit 1
python3 tools/defense_demo.py "$@"
status=$?
if [ -t 0 ]; then
    printf '\n演示已结束（退出码 %s）。按回车关闭窗口……' "$status"
    read -r answer
fi
exit "$status"
