#!/usr/bin/env bash
set -e
SRC="${1:-./build/app}"
QT_MESSAGE_PATTERN='[%{time hh:mm:ss.zzz}] %{type} %{message}' "$SRC" 2>&1 | tee /tmp/app.log
