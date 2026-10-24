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
