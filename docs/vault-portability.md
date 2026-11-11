# Portable archive-key recovery

This owner-only dashboard workflow makes encrypted archives recoverable after a
Windows account or machine change. It does not change the running character's
storage and is never exposed as a character/model tool.

1. Pause the application and switch it offline. Finish or stop active work.
2. Create a completed archive backup in Memory Workshop. Retain the entire folder,
   including `manifest.json`, `data/`, character files and configuration.
3. Export a portable key bundle with a strong, unique password of 12–1024 UTF-8
   bytes. Store the password separately from both backup and bundle. Losing it
   and the original Windows account can make the memories unrecoverable.
4. On the destination, select the encrypted bundle and existing backup directory.
   Verify it first. Verification shows counts, never memory contents.
5. Recover to an absolute, local, **nonexistent data directory** outside the active
   archive, installation and configured workspaces. Recovery never overwrites an
   existing directory or automatically activates the restored data.
6. Keep the source backup. Only after successful recovery, configure the application
   to use the new data directory and separately restore the desired character and
   non-secret configuration from the backup. Re-enter API credentials separately.

## Protection and format

`hibernation.portable-keys.v1` contains only an encrypted binary keyring: root and
nested channel archive master keys, plus their relative storage names. It does
not contain conversation text, API keys, QQ tokens or other credentials. Storage
names are encrypted too. Export does not migrate the archive encryption itself.

AYCrypto provides Argon2id (3 iterations, 64 MiB, parallelism 1, random 16-byte salt)
and XChaCha20-Poly1305 (random 24-byte nonce, 32-byte key, 16-byte authentication
tag). Header metadata is repeated inside the authenticated payload because the
current engine cipher interface has no associated-data argument. Unknown format
versions, algorithms and KDF parameters are rejected before derivation. Limits:
256 KiB bundle, 128 stores, 1 MiB manifest, 4096 manifest files, 32 path components,
and 10 million authenticated revisions per ledger. A large backup can therefore
require a later streaming/multi-volume format; disk capacity does not remove
these limits.

Passwords, decrypted bundles, derived keys and master-key buffers are cleared
after use. Neither passwords nor raw keys are persisted or logged. As with normal
operation, keys and decrypted records exist briefly in process memory; this is
at-rest protection. A weak password remains vulnerable to offline guessing.

## Verification and publication

Verification checks the completed Memory Workshop manifest and SHA-256 for every
listed file. Original `memory-key.dpapi` files may be absent: the portable bundle
replaces them. Each archive must have a matching key, and **every revision** must
pass the existing vault codec's authentication, including its bound source,
revision, schema, correlation and timestamp. A valid hash alone is insufficient.

The current engine SQLite opener changes journal mode. To preserve snapshot bytes,
verification opens an encrypted scratch copy next to the ledger, never the source
database. Only its exact randomly named scratch file and SQLite sidecars are
removed afterward. The backup therefore needs space for one additional encrypted
ledger and write permission for these temporary copies. No plaintext database is
created. Source archive hashes are checked again after authentication.

Recovery copies `data/` only into a random sibling staging directory. All runtime
directories are omitted, including old credentials, IPC tokens and DPAPI blobs.
Only archive master keys are rewrapped with DPAPI for the current Windows user.
The new blobs are loaded again to authenticate actual encrypted rows. All copied
archive bytes are checked once more before a final rename publishes the new data
directory. A failed copy/cancellation leaves its `.recovering-*` staging directory
for inspection; the application never recursively deletes it. An incomplete marker
prevents confusing an interrupted staging directory with a completed result.
`RECOVERY_INCOMPLETE.json` and `portable-recovery.json` are reserved root names.

Absolute/device paths, traversal, Windows reserved names, alternate data streams,
symlinks and junctions are rejected. The existing Windows owner and administrator
can still alter local files concurrently; this is not isolation from a hostile
administrator. Do not edit a backup during verification/recovery.

## HTTP management API

All endpoints use the existing authenticated local management service. Operations
require pause + offline and no active model, channel or workshop work.

- `POST /api/vault-portability/export` with `{password}`.
- `POST /api/vault-portability/verify` with `{password,bundle,backupRoot}`.
- `POST /api/vault-portability/restore` with
  `{password,bundle,backupRoot,targetRoot}`.
- `GET /api/vault-portability/status` returns
  `{state,operation,operation_id,result,error}`. State is `idle`, `running`,
  `completed` or `failed`. Accepted operations return HTTP 202 and `operation_id`.

Export result: `{bundle,filename,stores,includes_memory_contents:false}`. The UI
downloads the encrypted bundle and does not render its contents. Verification
result: `{verified:true,stores,records,files,archive_bytes}`. Recovery additionally
returns `{restored:true,targetRoot,originals_unchanged:true,automatic_activation:false}`.
Job state is transient; passwords and unfinished operations do not survive restart.

Public errors: `portable_password`, `portable_bundle_invalid`,
`portable_unlock_failed` (wrong password and authentication failure are identical),
`portable_key_unavailable`, `portable_backup_invalid`, `portable_backup_mismatch`,
`portable_target_exists`, `portable_path_rejected`, `portable_busy`,
`portable_pause_offline_required`, `portable_crypto_unavailable`,
`portable_cancelled`, `portable_owner_only`, `portable_operation_failed`.

## Checks and limits of evidence

`tests/VaultPortability.cpp` uses synthetic archives and real vault encryption:
root + nested channel recovery after deleting copied DPAPI keys, actual record
decryption, unchanged database bytes, exclusion of API credentials, wrong password,
tag/nonce/KDF tampering, existing-key conflicts, invalid/protected paths, manifest
traversal, cancellation before and after staging (partial copy retained), corrupt archives and a correctly rehashed database using
the wrong master key. No production records or paid model calls are used.

These checks simulate lost original key files under the same Windows user. They
do not constitute a physical second-machine or second-account recovery test. A
separate disposable Windows account/machine drill remains advisable before
relying on the feature as the sole recovery method.


## 导师独立记忆的 v2 附件

存在 `mentors/default/state.dpapi` 或 `records/<32位小写十六进制ID>.dpapi` 时，导出使用 `hibernation.portable-keys.v2`。v1 的根存档与 QQ 密钥包继续兼容。v2 除存档密钥之外，还包含通过原 Windows 账户解封后、再次用口令加密的导师状态与记忆，因此 `includes_memory_contents` 为真；JSON 下载文件始终只有加密载荷，不输出明文正文。

仅接受固定路径和导师 v1 状态/记录结构；原生登录、auth、token、配置目录不进入附件。未知 DPAPI 路径、记录缺失、损坏、无法解密或越界都会明确失败，不能把单纯复制原 DPAPI 文件算成可迁移成功。导师明文工作区仍以普通文件备份，本功能不会把工作区文件自动转成加密记忆，也不迁移 Codex 登录凭据。

附件及存档密钥解码后的总量上限为 4 MiB，JSON 包上限为 10 MiB；超限会返回 `portable_mentor_limit`。单状态上限 1 MiB，单记录上限 128 KiB，附件最多 4096 个；普通备份自身仍受原有 4096 文件限制。较大的导师历史目前需要保留原 Windows 账户备份，不能假定已经跨账户可恢复。

每份附件绑定原备份 DPAPI 文件的 SHA256。应先暂停并离线，确认导师已停稳，在同一静止检查点生成普通备份和 v2 包；如果之后导师状态变化，不能用新的包搭配旧快照。验证会明确拒绝不匹配，保留两边原件。没有导师附件的旧 v1 包也不能验证带有导师 DPAPI 的快照。

恢复只写入全新的目标目录：正常文件复制后，用目标 Windows 账户重新 DPAPI 封装导师状态和记录，验证后发布目标目录。原备份文件和密文哈希保持不变，不替换备份中的 DPAPI 文件；`mentor_files_rewrapped` 报告实际重封装数量。恢复测试模拟了密钥丢失与重新封装，未在第二个真实 Windows 账户上进行端到端登录测试。

异常中断可能遗留 `.pending` 原子写入临时文件。它们本身仍是 DPAPI 密文，但不在状态索引中；普通备份保留原件，迁移不会把它们计作已恢复的导师记忆。发现此类遗留应先核对并修复原账户存档，不把它们作为可重放操作或已确认经历。笔记只接受当前索引内真实记录 ID 作为来源，类别限 fact、summary、open_question。
