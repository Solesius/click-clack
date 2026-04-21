# Contributing

Thanks for your interest in `click-clack`.

## Ground rules

- Keep changes small and focused. One concern per PR.
- Preserve the append-only invariants of the WAL — never mutate or delete
  frames, only compact in place.
- Don't break existing on-disk formats without a migration.

## Development

```sh
cmake -S . -B build -DCC_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

cd frontend && npm install && npm test -- --watch=false
```

## Style

- C++: `.clang-format` governs layout; run `clang-format -i` before pushing.
- TypeScript / Angular: Prettier + ESLint (`npm run format`, `npm run lint`).
- Commit messages: imperative mood, ≤ 72-char subject, optional body.

## Security

Report security issues privately — do not file public issues for them.
