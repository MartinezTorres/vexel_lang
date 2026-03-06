#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/parse/parser.cpp"

FN="$(awk '
  /StmtPtr Parser::parse_stmt_no_semi\(\) \{/ { in_fn=1 }
  in_fn { print }
  in_fn && /^}/ { exit }
' "$TARGET")"

if [[ -z "$FN" ]]; then
  echo "missing parse_stmt_no_semi implementation" >&2
  exit 1
fi

if ! rg -q "match\(TokenType::BreakArrow\)" <<<"$FN"; then
  echo "parse_stmt_no_semi must use BreakArrow token for break control" >&2
  exit 1
fi

if ! rg -q "match\(TokenType::ContinueArrow\)" <<<"$FN"; then
  echo "parse_stmt_no_semi must use ContinueArrow token for continue control" >&2
  exit 1
fi

if rg -q "match\(TokenType::BitOr\)|match\(TokenType::Greater\)" <<<"$FN"; then
  echo "parse_stmt_no_semi must reject spaced loop-control forms like '-> |' or '-> >'" >&2
  exit 1
fi

echo "ok"
