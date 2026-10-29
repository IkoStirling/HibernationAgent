# HibernationAgent

Independent application consuming AliyatEngine. Business persona, stages, memory
policy, output schema, workspace layout and frontend belong here. Generic fixes
belong in the owning engine module with its tests, docs and SDK updates.

Novel settings are read-only references. Simulation output never edits the novel
repository. Preserve all original records; accessibility changes are separate.
Do not add model keys to configs, logs, tests, command lines or commits. Use only
environment variable names. Default to Mock; no live paid calls without user setup.

Use UTF-8. Build/output/temp files go to configured non-system storage. Run unit
and separate-process integration checks before committing verified app changes.
Keep local runtime archives/config and generated evidence out of source control.

## Character actions and animation catalog

`docs/character-action-catalog.md` is the canonical character-action and animation
production table. In the same change, update it whenever adding, changing or
removing an action, interaction, visible plugin behavior, scene flow or animation
state/fallback. Keep stable action IDs, real tool/event entry points, actual
Asterlan material coverage, transitions, props, interruption and completion rules.
Distinguish implemented logic, independent animation, reused animation and planned
scenes; adding a PNG/state is not automatic business wiring. After changing the
Asterlan manifest or imported assets, refresh frame counts, timings and fallbacks.
Link composite actions to reusable units; retain retired IDs instead of reusing
them. Do not turn animation completion into a factual action receipt or alter
persona/stages/novel settings as a documentation side effect.
