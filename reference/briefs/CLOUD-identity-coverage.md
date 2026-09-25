# CLOUD-2 "identity coverage" — brief (text-only, cloud sandbox)

Read reference/briefs/EXECUTOR-CARD.md first. You are in a Linux sandbox: you CANNOT build
the exe or render. You write PowerShell 5.1 (no `?:`, `??`, `&&`, `||`) and commit to a
branch `cloud-identity`; the maintainer verifies locally (steps below) before merge.

## Goal
tools/preset-identity.ps1 today covers 5 hardcoded acid inis + the fluid parity ini. Extend
it so every ini the project ships is covered, tiered, manifest-driven, with a diff report.

## Deliverables
1. `tools/preset-identity.manifest.psd1` (or .csv, your call, PowerShell 5.1 readable): one row
   per ini under reference/configs, reference/presets, reference/moods (enumerate them; skip
   *.diff.txt, *.py). Columns: path, look (fluid|acid|ink|overlay|test), base (for PARTIAL
   overlays such as reference/presets/Mirror - *.ini and any ini that lacks a [style]/look
   key: the full ini it must be composed onto), delay (default 60; 15 is fine for test inis),
   tier (fast|full|skip), note. Rules: `eyes-*`, `parity-*`, `ab`, `blind` inis are test inis
   (tier skip unless they are real parity variants, then full). The fast tier ≈ 15 inis: the
   fluid parity ini, one per look and per ink_mode, the shipped tray presets of each look,
   acid-rise-12, monotone-post-0924, lapd-look-candidate.
2. preset-identity.ps1: `-Tier fast|full|all` (default fast), `-Manifest <path>`, keep `-Exe
   -Baseline -Save -Presets -Delay -ScratchDir -Yield`; overlays are composed by
   concatenating base + overlay into a temp ini written WITHOUT a byte-order mark
   ([IO.File]::WriteAllText with a UTF8Encoding($false)); the fluid parity check ALWAYS runs
   first and is reported separately (unchanged); gpu-lock.ps1 handling unchanged.
3. Baseline format v2: header lines `# exe-sha256 <hash>`, `# git-head <sha>`, `# tier <t>`,
   then `md5<TAB>path`. `-Baseline` diff mode prints CHANGED / UNCHANGED / MISSING (in baseline,
   not rendered) / NEW (rendered, not in baseline) and exits 1 only on CHANGED or MISSING.
   Reading a v1 baseline (md5 path lines only) must still work.
4. A `-DryRun` switch that lists what would render (path, composed base, delay, tier) without
   touching the GPU — this is what YOU run in the sandbox (pwsh is available) as your proof.

## Proof block you ship (EXECUTOR-CARD §6 adapted)
- `-DryRun -Tier all` output pasted into the PR/commit message: every ini classified, none
  missing (cross-check with `find reference -name '*.ini' | wc -l`).
- A planted-drift test in a script `tools/tests/preset-identity-diff.ps1`: feeds a v2 baseline
  and a fake result set to the diff function and asserts CHANGED/MISSING/NEW/UNCHANGED.
- Sentence per change in the commit message; no other files touched.

## Maintainer's local verification (not yours)
-Save fast tier twice → identical; full tier once; change one key in one ini → diff flags only
it; overlay compositions render.

## Then answer the 3 review questions from EXECUTOR-CARD at the end of your final report.
