# Git Hooks

This repo provides a pre-commit hook that builds and tests the project.

## Enable

```bash
git config core.hooksPath .githooks
```

## Behavior

The `pre-commit` hook runs:

- `make`
- `make web`
- `make test`

It also checks that `web/playground.html` is clean after `make web` (so the playground is up to date).
