# AOWIS Mobile documentation

**Status:** Product and architecture definition; implementation has not started.
These pages describe the *current intended product*, not a log of discussions,
legacy implementations, or completed development stages.

| Document | Purpose |
| --- | --- |
| [Product](product.md) | Purpose, domains, capabilities, and boundaries |
| [Field workflows](survey-workflows.md) | Direct editing, observations, positioning, and the touch UX |
| [Architecture](architecture.md) | Mobile application boundary and reuse of AOWIS components |
| [Data model](data-model.md) | Shared geometry concepts, hydraulic topology, OSM mapping, and observations |
| [Offline and sync](offline-sync.md) | Durable local work, offline basemaps, version control, conflict handling |
| [Roadmap](roadmap.md) | Implementation priorities, acceptance cases, outstanding decisions |

## Status conventions

- **Agreed:** Explicitly settled product requirements and direction.
- **Proposed:** Preferred engineering design that must still be implemented,
  validated, and, where it affects product behavior, accepted.
- **Future:** Intended capability not required for the initial survey release.
- **Existing elsewhere:** Behavior present or documented in another AOWIS
  repository; this does **not** mean it is integrated into Mobile.

The roadmap is the source for what is actually in this repository. Do not
confuse a description of an intended feature with an implemented feature.

## Documentation maintenance

Update these pages when a product choice changes, when an implementation makes
previously proposed behavior concrete, or when a limitation is discovered.
Prefer a current contract and clear future work over accumulating patch-stage
notes or redundant historical roadmaps. AOWIS is pre-first-release: there is
no need to design migration compatibility for nonexistent mobile users.

- [Rendering diagnostics](renderer-lab.md): Qt Quick/bgfx implementation, build and validation protocol.

- [Android renderer build and device validation](android-renderer-test.md) — packaging, touch and lifecycle checklist.
