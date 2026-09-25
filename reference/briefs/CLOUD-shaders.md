# CLOUD-1 "shaders" — HLSL out of C++ strings + rig slot checker (brief, auditor pre-flighted 2026-09-24)

Cloud/Linux sandbox: you CANNOT build MSVC or render. Deliver a branch `cloud-shaders`; the maintainer
verifies locally before merge. Read reference/briefs/EXECUTOR-CARD.md first (rules), AGENTS.md.

## (a) HLSL out of C++ strings
Today src/shaders.h holds kDisplaySrc / kPostSrc (+ compute sources) as C++ string literals, split by
hand under the MSVC 16380-byte literal cap (tools/slot-check.ps1 checks sizes and LA_ defines).
Mechanism (binding): a `.hlsl` → header generator written as a CMake script (`cmake -P`), called from
`add_custom_command` with DEPENDS on the .hlsl files; no Python at build time, no runtime file loading
(single exe). It emits raw-string pieces auto-split on line boundaries at 16000 bytes (removes the
manual cap), normalises CRLF→LF, and the generated header is NOT committed (build2/ output).
BYTE IDENTITY: the embedded text must equal today's concatenated literals exactly (raw strings in a
CRLF file compile to \n; the #define prefixes such as kAcidSlotMacros go in front unchanged); any byte
change may change the DXBC. Self-check you CAN run in the sandbox: run the generator with cmake, and a
small Python script that parses the OLD shaders.h literals, concatenates them per source, and compares
byte for byte with the generated strings for every source; include that report.
Consumers to update: every D3DCompile site and the defines it prepends (grep src/), tools/slot-check.ps1
(literal sizes + LA_ scan → now checks the .hlsl files / generated header), tools/dxbc-cmp.py and the
scratch copy noted in EXECUTOR-CARD (grep tools/ and reference/briefs for "dxbc"). PowerShell 5.1 syntax
in .ps1 (no ?:, ??, &&, ||).
## (b) Rig slot table + checker (CHECKER ONLY, no generated unpack code)
A `src/rig_slots.h` X-macro table used by the C++ packing of the post-pass rig block b3 (rg0..rg4, the
6-bit packed fields in rg1.x/y, rg2.w), plus tools/rig-check.ps1 that parses the HLSL rgN.c uses and the
unpack helpers (BDU6 / LidU8 / LidU12) against the table and FAILS on drift (prove with a planted
drift). Leave src/acid_slots.h alone (BU/BV are changing it).
## Conflicts
Branch bu (lamp grey + split tone) and bv (colour schemes) edit shaders.h and will merge soon: START from
main, and before your final report `git fetch` + rebase onto the newest main; if BU/BV merged, re-run
your byte-identity script against the NEW shaders.h. Say which main you ended on.
## Maintainer's local verification (not yours)
clean build2.cmd from a fresh clone; touching one .hlsl triggers a rebuild; dxbc-cmp on every PSO
IDENTICAL; parity md5 10E36EBF1A74EDFE609065D757300054; preset-identity fast MATCH; slot-check + rig-check
PASS, rig-check FAILS on a planted drift; one acid render.
## Report ≤25 lines + the 3 EXECUTOR-CARD review questions.
