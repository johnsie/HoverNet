# ADR 0002: Licence and authorship policy for track content

- Status: **Proposed** (17 October 2026), for confirmation by the project owner. Not legal advice; have a lawyer review it before the
  catalogue is opened to outside submissions.
- Deciders: project owner
- Roadmap: [roadmap-3.0.md](../roadmap-3.0.md), "Decisions to make before kickoff" and the risk "Unknown licensing of community content"

## Question

Under what terms may a track be offered by HoverNet, given that about 1,000 inherited community tracks have no recorded author or licence
and that new tracks will come from many people?

## Decision

1. **Inherited library: archival only.** The 1,012 tracks stay a separate, optional download, as they are now ([community-tracks.md](../community-tracks.md)).
   They are described as `author=unknown`, `licence=unspecified`, are never bundled into the game or its installers, and are never
   presented as covered by the game's licence. This is a risk-managed position, not a grant of rights.
2. **Takedown.** Anyone who shows they made, or own the rights to, one of those tracks can have it withdrawn: it is removed from the
   manifest (clients stop offering it) and from the pack in the next release. The project acts on a request within 14 days and keeps the
   request, not the track, on record.
3. **New tracks in the catalogue** need a named author (a name or handle the author chooses; it is published) and one of three licences,
   identified by SPDX id: `CC0-1.0`, `CC-BY-4.0` or `CC-BY-SA-4.0`. The author declares they made the track or have the right to license it.
   `HoverNetTrackPack verify --catalogue` enforces the fields; the declaration is part of the submission process, which does not exist yet.
4. **Why these licences.** They are well known, allow redistribution and hosting by the project and by server operators, need no
   negotiation, and one of them (CC0) lets an author who wants no conditions say so. Licences with non-commercial or no-derivatives terms
   are not accepted: they would stop operators and mirrors from distributing the content or the project from converting it to future formats.
5. **Authorship is data in the package, not an account.** The package manifest records author and licence; replays and leaderboards
   record player slots and, later, display names. Nothing personal beyond the chosen name is collected for authorship.
6. **Platform neutrality.** The same rules apply to the browser client, which downloads the same packages and shows the same
   attribution.

## Consequences

- Attribution must be shown where a track is chosen (track selector and results), to honour CC-BY. Not built yet.
- Share-alike applies to the track itself, not to the game or to other tracks, and not to replays of races on it.
- Widening the licence list is a reviewed change to `CatalogueRules` in `NetTarget/LinuxClient/TrackPack.cpp` and to the specification.
- Open: a submission and review workflow, a signed catalogue, and legal review of the inherited-library position.
