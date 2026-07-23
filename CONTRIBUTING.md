# Contributing

[中文贡献指南](docs/community/contributing.zh-CN.md)

Thank you for helping build Document Intelligence Engine. The project is an early, measurable C++ document parsing
engine—not a finished enterprise product. Small, focused improvements with tests and evidence are preferred over
large rewrites or unmeasured backend additions.

## Where help is most useful

The current priorities are:

1. Reliable Job execution: recovery, attempts, idempotency, cancellation, and model-session reuse.
2. End-to-end evaluation on representative technical documents.
3. OCR quality, reading order, document assembly, and source-grounded RAG output.
4. Documentation, fixtures, visual diagnostics, and reproducible bug reports.

The project is not currently prioritizing additional input formats, a large collection of interchangeable models,
multi-tenant SaaS features, or Kubernetes infrastructure.

See [the roadmap](docs/roadmap.md) before proposing a large feature.

## Before opening an issue

- Search existing issues and discussions.
- Include the exact backend selection and build configuration.
- Attach logs and the smallest document that reproduces the problem when its license permits redistribution.
- Remove confidential information. Do not upload customer or employer documents without authorization.
- For accuracy changes, state which metric should improve and provide a fixture or public dataset reference.

New model backends should include a pinned immutable source, checksum, license/provenance notes, label mapping,
preprocessing/postprocessing documentation, and a measurable regression test.

## Development workflow

Build and run the core tests:

```bash
cmake --preset core-release
cmake --build --preset core-release --parallel
ctest --preset core-release
```

Build the optional platform Worker:

```bash
cmake --preset platform-release
cmake --build --preset platform-release --target document_intelligence_worker --parallel
```

Test the API and Web application:

```bash
python -m pip install -e './platform/api[test]'
pytest platform/api/tests
npm ci --prefix platform/web
npm audit --prefix platform/web
npm run build --prefix platform/web
```

Dependency setup is documented in [docs/dependencies.md](docs/dependencies.md). Commit messages follow
[docs/commit-convention.md](docs/commit-convention.md).

## Pull requests

- Keep one PR focused on one problem.
- Explain the observed cause before describing the fix.
- Prefer a general fix over special-casing one document.
- Add or update tests in proportion to the change.
- Preserve source coordinates, confidence, provenance, and backend neutrality.
- Do not commit models, private documents, generated build output, or runtime artifacts.
- Update user-facing documentation when behavior or configuration changes.

Maintainer time is limited. A review may ask for a smaller scope or defer work that is outside the current milestones.
That is a prioritization decision, not a judgment on the value of the idea.

## Data and licensing

Only contribute data that can legally be redistributed. Record the source URL, immutable revision when possible,
license, expected files, and checksums. Derived annotations must retain any attribution required by the source
dataset. By submitting a contribution, you agree that your code contribution is licensed under the repository's MIT
License.
