# Executor briefs

Briefs handed to Opus executors (one at a time). `NEXT-*` = queued, `DONE-*` = landed (kept for the record).
The droplet-particle brief was sent as SendMessage text to the running executor (2026-09-17 evening);
its substance: replace BOTH procedural swarm layers with simulated droplet particles that are
negative/positive metaballs in the same oil field (grid-accelerated in the shader), CPU-simulated:
advect with fluid + parent blob, confined to oil (kind 0) / ink (kind 1), nucleate at a rate, attract,
coalesce area-conserving, dissolve slowly, born/dying by radius ramp (no popping), fade the old
swarms out when droplets > 0, fix the hairline-arc seam near nearly-merged blobs; presets ~1500
droplets; A/B in build2/shots/droplets/. Resume that executor after a session reset via SendMessage.
