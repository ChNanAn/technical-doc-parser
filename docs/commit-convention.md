# Commit Convention

This project uses a lightweight Conventional Commits style.

## Format

```text
<type>(optional-scope): <summary>
```

Examples:

```text
feat(pdf): render pages with PDFium
fix(cli): reject non-positive DPI values
docs: add dependency setup notes
ci: add CMake build workflow
test(pdfium): verify setup script config
chore: ignore local build artifacts
```

## Types

- `feat`: user-visible feature or new capability
- `fix`: bug fix
- `docs`: documentation only
- `ci`: GitHub Actions or other CI changes
- `test`: tests or test fixtures
- `refactor`: code restructuring without behavior changes
- `build`: build system or dependency wiring
- `chore`: maintenance that does not affect runtime behavior

## Scopes

Use a short scope when it helps clarify the affected area:

- `cli`
- `pdf`
- `image`
- `ocr`
- `layout`
- `table`
- `inference`
- `export`
- `pdfium`

Scope is optional. Prefer no scope when the change is broad.

## Summary Rules

- Use imperative mood: `add`, `fix`, `render`, `ignore`.
- Keep the first line under 72 characters when practical.
- Do not end the summary with a period.
- Keep one commit focused on one logical change.

## Body

Add a body when the reason matters more than the diff:

```text
build(pdfium): add pinned PDFium setup script

Download PDFium from a fixed release instead of committing binaries.
This keeps the repository small while making CI builds reproducible.
```

## Current Examples

Good commit messages for the current stage:

```text
build(pdfium): add pinned dependency setup
ci: install PDFium before CMake build
docs: document PDFium setup
feat(cli): add initial command-line interface
```
