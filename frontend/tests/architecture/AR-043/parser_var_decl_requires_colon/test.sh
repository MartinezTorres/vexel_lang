#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/parse/parser.cpp"

LOOKAHEAD_FN="$(awk '
  /bool Parser::looks_like_var_decl_with_linkage\(bool allow_double_bang_local\) const \{/ { in_fn=1 }
  in_fn { print }
  in_fn && /^}/ { exit }
' "$TARGET")"

if [[ -z "$LOOKAHEAD_FN" ]]; then
  echo "missing looks_like_var_decl_with_linkage implementation" >&2
  exit 1
fi

if ! rg -q "const bool typed = next == TokenType::Colon;" <<<"$LOOKAHEAD_FN"; then
  echo "var-decl lookahead must only accept ':' for typed declarations" >&2
  exit 1
fi

if rg -q "TokenType::Hash|TokenType::LeftBracket" <<<"$LOOKAHEAD_FN"; then
  echo "var-decl lookahead must not accept legacy prefix-array/hash typed forms" >&2
  exit 1
fi

PARSE_DECL_FN="$(awk '
  /StmtPtr Parser::parse_var_decl\(bool allow_double_bang_local\) \{/ { in_fn=1 }
  in_fn { print }
  in_fn && /^}/ { exit }
' "$TARGET")"

if [[ -z "$PARSE_DECL_FN" ]]; then
  echo "missing parse_var_decl implementation" >&2
  exit 1
fi

if ! rg -q "if \(match\(TokenType::Colon\)\)" <<<"$PARSE_DECL_FN"; then
  echo "parse_var_decl must parse explicit ':' type annotations" >&2
  exit 1
fi

if rg -q "match\(TokenType::Hash\)" <<<"$PARSE_DECL_FN"; then
  echo "parse_var_decl must not accept legacy hash-typed declaration syntax" >&2
  exit 1
fi

echo "ok"
