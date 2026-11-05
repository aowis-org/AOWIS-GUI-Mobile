# Offline operation, local persistence and synchronization

## Product requirements

**Agreed:** Work must continue in poor or absent Internet coverage. Recording
an observation or saving a local edit must not depend on a round trip to a
remote server. Losing a connection must not lose previously saved changes.

Mobile must visibly distinguish:

- **Local and saved:** durable on this device, not necessarily uploaded.
- **Queued / unsynchronized:** waiting for transfer or reconciliation.
- **Synchronized:** server has authoritatively acknowledged the applicable
  operation or objects.
- **Conflict / action needed:** the target changed or an operation was rejected;
  local data is retained until explicitly resolved.

A success toast or a matching SHA-256 is not by itself a server acknowledgement
that a draft or branch HEAD advanced. Operation identity and authoritative
status matter separately from content equality.

## Proposed local storage and editing split

An offline survey session needs durable geometry, type/field values, GPS
metadata, attachments and a pending-operation ledger. A direct network edit
needs an editable private working copy with MODEL action history.

**Decision not yet made:** whether the phone runs a full native embedded
private DVMDB, a lightweight mobile store that later imports into a personal
DVMDB, or some carefully bounded combination. Selection criteria include
Android/iOS packaging, durability under abrupt termination, working-copy
semantics, storage use and usable offline conflict handling. A device that
stores only an in-memory queue would **not** meet the product requirement.

For a direct edit of a project hosted on a shared DVMDB, the intended AOWIS
rule still applies: checkout into a personal editable repository, not a write
straight to an arbitrary shared database. An on-device repository could
become that personal source if the runtime and DVMDB integration support it.
Do not silently change the personal-server designation just because Mobile
starts without connectivity.

## Basemap operation

The app must be usable with explicitly downloaded offline map areas. **Proposed
requirements:** user-selectable extent and detail level, estimated package
size, download progress, integrity check, stored package inventory, deletion
and map display with no network. Provider licensing/attribution must permit
actual offline storage; ordinary transient HTTP raster cache contents should
not be treated as a guaranteed offline region.

AOWIS-SERVER-MAP already serves raster and terrain data, but mobile package
creation/mounting and automatic offline area management are **not** currently
an integrated capability. 2D surveying should still work when imagery is
unavailable; user-collected features must remain accessible.

## Edits, history and transfer

DVMDB already distinguishes a mutable private working copy from an immutable
local Commit and from a separately moving shared branch HEAD. Mobile should
preserve those distinctions:

1. Persist field change or meaningful model action locally.
2. When connected, transmit only what the receiver needs, with an operation
   identity and enough expected-state information to detect stale writers.
3. Confirm acceptance and recover the result after a lost acknowledgement.
4. Commit a direct working copy locally when requested; this must not
   silently publish to a shared source.
5. Fetch/publish/merge by explicit workflow as capabilities become available.

**Existing elsewhere:** DVMDB has verified full/incremental revision bundles,
private drafts, canonical MODEL event saves and conditional writes. A fully
integrated whole-project Clone/Fetch/Publish/reconcile flow is not yet
complete in the source inspected for this design. Mobile should not promise
automatic bidirectional publication before that workflow exists.

**Proposed performance direction:** viewport-scoped reads, content-addressed
objects, small delta/event transfers and sparse receipts instead of fetching
a whole `NetworkHydraulic` on each Undo/Redo or small edit. Hashes (including
SHA-256) are useful for object verification and comparing a known resulting
state, **not** a substitute for missing remote data, version checks, writer
fencing, operation IDs or conflict resolution.

## Conflict behavior

- If a remote branch moved independently, preserve local commits or draft
  work; offer reconciliation/merge rather than overwrite.
- If two devices edited the same logical asset or geometry while offline,
  request review of incompatible changes. Do not silently choose by timestamp.
- Multiple observations of the same physical object can coexist as evidence.
  Their spatial overlap is not by itself a synchronization conflict.
- Upload of photos and related data must be recoverable and idempotent, with
  a clear status when only part of the attachment batch is transferred.
- A phone interrupted during sync must be able to retry without duplicating
  accepted actions or turning the current draft into a second Commit.

## Tests and acceptance

The minimum offline test matrix should include:

| Test | Required outcome |
| --- | --- |
| Start survey with no network, collect point/line/area | All data saved and viewable locally |
| Kill app midway through a saved session; reopen | Completed saves and attachments preserved; unfinished draft recoverable |
| Stop network during upload, reconnect and retry | No duplicate records or missing evidence |
| Save locally while server is unavailable | No accidental direct write to shared source |
| Two devices change same direct-edit target | Deterministic conflict, no silent loss |
| Very large project with narrow map viewport | Bounded local work, no full-network reload per action |
| Device reports poor GPS accuracy | Quality visible; manual placement remains available |
| Clear offline map package | No deletion of survey observations or edits |

Additional engineering tests should exercise crash consistency, stale
operation IDs, lease/fencing failures, attachment integrity and power loss
at each durable-save boundary. These are **planned tests**, not test results.
