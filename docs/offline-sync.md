# Offline data and synchronization contract

**Product requirement, not a current implementation:** Mobile must let a
surveyor work with no network and retain local records, edits and media across
app restarts. No on-device survey store or sync protocol exists in this source
tree yet.

## State the app must communicate

| State | Meaning |
| --- | --- |
| Locally saved | Durable on device, not necessarily transferred |
| Pending sync | Local change/attachment awaits transfer or acknowledgement |
| Synchronized | Authoritative remote has acknowledged the specific operation/state |
| Conflict | Remote state differs or a write was refused; local work is retained |

A hash match can verify content, but does not replace acknowledgement of a
particular operation, revision or working-copy advance.

## Observation store (proposed)

The local survey store needs durable sessions, UUIDv7 observation identity,
geometry, classification, capture provenance, photos/attachments and a retry
ledger. Save should be atomic enough that a process kill never leaves an
apparently saved observation with a missing required payload. Attachment
integrity, interrupted transfers, duplicate upload attempts and device storage
limits need explicit tests.

The exact embedded engine is undecided: embedding DVMDB on Android is one
candidate, but an alternative evidence store is valid if it respects the
synchronization and provenance contract. Do not claim a Mobile DVMDB has
already been ported or integrated.

## Direct editing and DVMDB (agreed direction)

A user designates a personal working DVMDB (embedded in Standalone or REST,
according to platform). Projects on that server can be edited directly;
projects on other servers are **checked out** before editing. Branch, HEAD,
immutable Revision, Commit, undo/redo and publication semantics are DVMDB
responsibilities. Editing invalid hydraulic simulations is allowed; broken
structural references are not.

Mobile must not invent a separate branch-history format or upload a full
hydraulic network on every small edit/undo step. Sparse receipts/deltas,
operation IDs, precondition checks, idempotent retries and clear server
acknowledgements are preferred. How exactly this is implemented depends on
DVMDB's tested protocol when Mobile integration begins.

## Offline map data

The map should download a defined geographic region from a licensed source,
record package provenance/version and report available storage. Missing tiles
should not stop a survey from saving geometry. AOWIS-SERVER-MAP's existing
HTTP imagery/terrain cache is not automatically an offline region manager.
Bulk offline downloads from public OSM tile infrastructure must not be assumed
permitted; use an authorized provider or hosted packages.

## Conflict and recovery requirements

- Preserve the entire local change set when disconnected or on HTTP errors.
- Retry safely without duplicating observations, attachments or commits.
- Expose remote divergence and allow deliberate resolution; never silently
  overwrite another device's edits.
- Verify crash/restart recovery, partial attachments, repeated upload timeouts,
  out-of-order responses and eventual synchronization.
- Keep the observed field measurement and the accepted authoritative feature
  distinct and linked.

These capabilities are not blockers for proving the map renderer, but they are
release blockers for any claim of reliable offline surveying or Direct Edit.
