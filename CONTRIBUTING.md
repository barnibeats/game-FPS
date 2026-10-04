# Contributing

## Workflow

- `main` is always releasable. Work in branches: `feat/...`, `fix/...`, `docs/...`, `chore/...`.
- Open a pull request into `main`; CI must be green.
- Commits follow [Conventional Commits](https://www.conventionalcommits.org): `feat: ...`, `fix: ...`, `docs: ...`, `refactor: ...`, `chore: ...`.
- Update `CHANGELOG.md` under `[Unreleased]` for user-visible changes.

## Releases (GitHub only)

1. Bump `VERSION` in `project(game-fps VERSION x.y.z)` in `CMakeLists.txt`.
2. Move the `[Unreleased]` notes to a new version section in `CHANGELOG.md`.
3. Commit `chore(release): vX.Y.Z`, then tag and push:
   ```
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin main --tags
   ```
4. The `release` workflow builds the binary and publishes the GitHub Release with a zip and SHA-256 checksum.

## Code guidelines

- C++17, Win32 API only, no third-party dependencies, static linking.
- Resource use is the main feature: no polling loops faster than 1 s, no allocations in the ETW callback, nothing running while the HUD is hidden.
- Must work on Windows 10 and 11: load newer APIs dynamically and keep the manifest `supportedOS` entries.
