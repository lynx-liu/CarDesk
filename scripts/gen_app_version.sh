#!/usr/bin/env bash
# 读取 VERSION，写入 src/app_version.h，并将 patch 位 +1 供下次编译使用。
set -euo pipefail

VERSION_FILE="${1:?VERSION file path required}"
HEADER_FILE="${2:?header file path required}"

if [[ ! -f "$VERSION_FILE" ]]; then
    printf '2.0.1\n' > "$VERSION_FILE"
fi

raw="$(tr -d '[:space:]\r' < "$VERSION_FILE" | sed 's/^[Vv]//')"
major=2
minor=0
patch=1
IFS='.' read -r major minor patch _ <<< "${raw}...."
major="${major:-2}"
minor="${minor:-0}"
patch="${patch:-1}"

display="V${major}.${minor}.${patch}"
next_patch=$((10#${patch} + 1))

printf '%s.%s.%s\n' "$major" "$minor" "$next_patch" > "$VERSION_FILE"

mkdir -p "$(dirname "$HEADER_FILE")"
cat > "$HEADER_FILE" <<EOF
#ifndef APP_VERSION_H
#define APP_VERSION_H
#define APP_VERSION "${display}"
#endif
EOF

printf '%s\n' "$display"
