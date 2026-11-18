# Connected investigations acceptance record

Date: 2026-10-20. Implementation preserves the pre-existing uncommitted worktree.
The initial tracked patch and untracked source archive are retained under
`output/qa/investigations-20261020/baseline/`.

## Implemented behavior

- One bounded, conflict-checked profile registry with stable asset and relationship identities; explicit registration/linking, adoption, archive and import conflict previews.
- Native Qt connections with case/current-registry views, search, bounded expansion, linked map/dossier/finding/evidence navigation, a 100-node / 200-edge limit and truncation notices. Graphs fit after the tab's visible layout settles; manual zoom remains under analyst control.
- Case completion metadata and guarded closure; findings with evidence roles, alternatives, analyst review and retained prior decisions. Revised reviewed content returns to Unreviewed; newly typed rationale remains a draft.
- Explicit observation, monitoring-entry and SAR-candidate promotion. SAR drafts retain and fingerprint the original report, candidate, site, quality and source identities; stale source records are rejected.
- Finding task origins, local owner labels and prerequisite enforcement, including cycles, deletion of referenced prerequisites and reopening completed prerequisites.
- Portable immutable case context, missing-file visibility and explicit registry adoption/import. Swift models preserve the extension and registry records.
- Native A4 PDF and JSON manifest, explicit finding selection, optional map capture, bounded frozen previews, missing/conflicting evidence, outstanding work, recorded fingerprints and derivation references. Publication uses a new folder containing both completed files.

## Linux validation

Evidence directory: `output/qa/investigations-20261020/`.

- Desktop CTest suite: 24 targets; final result is in `ctest.log`.
- Investigation core: 15 scenarios (17 QtTest results including setup/cleanup); `core-tests.log`.
- Investigation UI: 6 scenarios (8 QtTest results including setup/cleanup), including stale drafts and frozen previews; `ui-tests.log`.
- Native Vulkan / NVIDIA GTX 1070 workflow: 176 checks; `native/validation.json`. The packaged validation reruns the generated workflow outside the checkout.
- Bounded graph first layout/paint: 104 ms for 100 displayed nodes from 301 assets / 300 relationships, with truncation, on this test host; `ui/graph-timing.json`. Core tests separately enforce the 200-edge cap on a dense graph.
- Briefing QA covers long multi-page text, conflicting citations, unavailable rasters, portrait previews, explicit map captures, changed product fingerprints and deletion after freezing. All 15 pages across the final six-page long-text, four-page preview and five-page packaged briefings were rendered with Poppler and visually inspected. Manifest references and PDF hashes pass; no text extends outside an A4 page. See `pdf-final-long/`, `pdf-final-previews/`, `pdf-final-packaged/` and `briefing-audit.json`.
- Final isolated Linux package/runtime/public-source checks **passed**: `package/package-check.json`, with 176 generated GPU checks and 32 live public-source checks, using Vulkan on the GTX 1070. The checker copied and installed the app outside the checkout, checked checksums/resources/runtime, exercised a path containing spaces, rejected installer overwrite and verified that uninstall preserves user-added files and the external profile. Detailed workflow reports are `package/generated/validation.json` and `package/live/validation.json`.

The validated package and matching source snapshot are under
`output/packages/linux-investigations-20261020-complete/`. The source bundle is
`helios-source.tar.zst`; its SHA-256 is recorded in the sibling `SHA256SUMS` file.
The compiled build ID matches the packaged executable.

Generated civilian airport/road records demonstrate the connected workflow. They
are not evidence of changes at a real facility. The milestone does not assert new
satellite-analysis accuracy or operational effectiveness.

## Swift and Mac gate

Swift 6.4.1 parses/formats the source, and the package manifest is admitted. The
actual new investigation models additionally pass a Linux compatibility harness
using Glibc in place of Darwin and minimal pre-existing model stubs. It decodes the
Qt fixtures, round-trips decisions/snapshots, enforces task dependencies and checks
registry conflict/archive/deletion behavior. Logs are `swift-format.log`,
`swift-package-manifest.json` and `swift-compatibility.log`. The toolchain signature
was verified before use; it is development tooling and is not an app dependency.

**Deferred by the user for later:** a fresh macOS build/run of the shared Qt application,
Metal workflow checks, Swift XCTest execution and round-tripping an edited case
between the actual Mac and Linux clients. The compatibility harness is not a
substitute. Existing macOS CI jobs include the new CTest targets and Swift tests;
the new fixture tests are in `InvestigationTests.swift`. Follow the
[Mac validation handoff](../MAC-INVESTIGATIONS-CHECKS.md) using the source snapshot
above; the new UI belongs to the shared Qt client.

Owner labels are local labels, conclusions are human-authored, and retained
history is neither authenticated collaboration nor a tamper-proof audit.
