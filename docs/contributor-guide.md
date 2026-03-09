# Contributor Guide

**Role**: Contributor guide.

Web route page: `docs/contributor-guide.html`.

Primary gates:

```bash
make test
make ci
make frontend-test
make backend-conformance-test
make docs-check
make web
```

Ownership model:

- Frontend owns semantics and analysis.
- Backends own emission strategy.
- Driver owns backend discovery and CLI routing.
