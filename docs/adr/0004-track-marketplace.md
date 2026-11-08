# ADR 0004: Track Market: authors publish from the editor, every player can get the tracks

- Status: **Accepted** (8 November 2026) by the project owner's delegation ("make decisions for me based on legality, playability and usability"). Not legal advice: the terms of use need review before the market is opened to outside submissions (decision 3).
- Deciders: project owner (delegated)
- Roadmap: [roadmap-track-editor.md](../roadmap-track-editor.md), Phase 5; builds on [ADR 0002](0002-content-licence-policy.md) (licences, authorship, takedown) and [ADR 0003](0003-track-editor.md) (the editor)

## Question

The editor must make **new** tracks, not only open old ones, and a finished track must be shareable through **one shared market that every player can browse and download from**, in the desktop and browser clients alike. What does a submission have to be, who may submit, who checks what, and how does a track reach players and servers?

## What already exists (checked 8 November 2026)

| Piece | State |
| --- | --- |
| Licence and authorship policy (CC0, CC-BY, CC-BY-SA, named author, declaration, 14-day takedown) | Accepted, [ADR 0002](0002-content-licence-policy.md); the submission process it mentions does not exist |
| Track package v1 with author, licence, hashes; `HoverNetTrackPack verify --catalogue` | Built and tested ([specs/track-package.md](../specs/track-package.md)) |
| A verified download path: manifest with size and SHA-256, hash-named `.trk.gz`, checked before install, works in the browser from the web host | Built for the 1,012 inherited tracks ([community-tracks.md](../community-tracks.md)); the manifest is a file in the repository, not a live catalogue |
| Online races on downloaded tracks | Built; the server must have the track in its community folder |
| Outiva sign-in for the web client (server-side OpenID Connect) | Built, optional; not in the desktop client |
| Moderator console, reports, kicks, bans, audit log | Built for chat and players, not for tracks |
| Ed25519 signing tool | Built and cross-checked; **no production key exists**, so nothing is signed |
| Proof that a track can be driven round: `HoverNetDriver` finds a finishing run in the real simulation and writes a verifiable replay | Built; a track made by the editor core's ring template finishes in 36.7 s and its replay verifies (`scripts/test-new-track.sh`) |
| Creating a track from nothing | Started: `MakeRingTrack` in the editor core; the result passes the strict validator and lint and is admissible to the catalogue (`HoverNetNewTrack` test) |

## Decision

1. **A submission is a package plus proof it can be finished.** The editor sends the `.hntp` package (author, licence, hashes) and a **finishing replay** recorded in its own playtest. The market re-runs, on the server and with the same tools the editor used, `verify --catalogue`, the strict validator, the strict lint and `HoverNetReplay verify` of the replay against the submitted track. A track nobody has finished never reaches players (playability). The replay is kept only until the track is listed or refused.
2. **Anyone can browse and download; only a signed-in author can submit.** Downloading needs no account. Submitting needs an Outiva account, so there is someone to contact, rate-limit, suspend and answer a takedown with. The published byline is the display name the author chooses; no email or account id is shown (ADR 0002 item 5). Limits: 5 submissions a day and 50 listed tracks per account, package at most 2 MB, description at most 200 characters.
3. **The market opens in two steps, because of legality.** *Private beta:* only invited authors (an allow-list kept by the owner) can submit, so tracks can be tried end to end while the terms are reviewed. *Public:* opens to any signed-in author only after (a) the terms of use and the author declaration have had legal review, (b) the account privacy policy (`docs/account-privacy-policy-draft.md`) is final, and (c) the production signing key exists (decision 6). Until then nothing may be described as open to everyone.
4. **Moderation is automatic first, human where the risk is.**
   - Every submission passes the automatic checks of decision 1.
   - An author's **first track is reviewed by a moderator** before it is listed (descriptions, names and any picture could be unlawful or abusive); once an author has a track listed, later tracks list automatically when the checks pass.
   - Every listed track has a **Report** button in the game; reports reach the moderator console. A track is unlisted at once while a report about illegal content is looked at, and removed for good on a valid takedown request (ADR 0002: acted on within 14 days, and sooner for content that is plainly unlawful or unsafe).
   - Removed tracks stay in the catalogue as **revocations** (the hash and the date, not the track) so clients and servers delete or refuse them.
5. **Authors cannot upload pictures or code.** The editor offers only the game's own backgrounds and textures; the market refuses a package whose embedded background is not one of the bundled images (compared by hash) and whose elements are not in the element catalogue ([TrackElementCatalogue.h](../../NetTarget/TrackEditor/TrackElementCatalogue.h)). Tracks are data: they never carry executable content.
6. **One live, versioned catalogue; signed once a key exists.** The market publishes a catalogue (the community manifest's columns plus author, licence, listed-at, status and a version number) at one URL on the web host, next to hash-named immutable `.trk.gz` files. Clients refresh it when the Track Market screen opens, then download and verify exactly as they do today. When the owner has generated the production key (off the build servers, backed up offline, as `docs/signing.md` requires), the catalogue is signed and clients pin the public key. Until then the catalogue is served over TLS and every file is hash-checked, and **it is not called signed**.
7. **Servers get market tracks without operator work on the official servers.** Official race servers sync the listed tracks from the catalogue (the existing sync script, made incremental and driven by the catalogue); revocations remove them. Private servers keep using `--community-dir`.
8. **One Track Market screen in the shared client**, so desktop and browser match: browse (new, popular, by author, search), see author and licence (the attribution CC-BY needs), download and play or host, report. Ratings and play counts come after the first release. Attribution is also shown in the track selector and on the results screen, which ADR 0002 already required.
9. **Publishing is one button in the editor.** Publish runs the same checks locally and says what is wrong in plain words, asks for the playtest finish if there is none, shows the author declaration and licence choice (CC-BY-4.0 offered first), signs in if needed, uploads, and then shows the state: checking, waiting for review, listed, refused (with reasons). A changed track is a new version with a new hash under the same name and author; the old version stays listed until the new one is, then is replaced. The author can unlist their own track at any time.
10. **Names:** unique per author; shown as "Name by Author"; the names of bundled tracks are reserved.
11. **Privacy:** the market keeps the account id, the time, the declaration version and the package hash with each submission, for moderation and takedown, and removes the account link on request without removing the licence record that the track was lawfully published.
12. **Hosting without containers.** The submission service is an ordinary service beside `hovernet_account.py` on the web host (systemd hardening, no Docker or other container tooling: CLAUDE.md rule 1), running the C++ tools as separate processes with timeouts and size limits.

## Consequences

- The editor's Phase 3 playtest must record a replay of a finishing lap, since Publish depends on it. The scripted checks in `scripts/test-new-track.sh` are the same gate a publish step runs.
- A track the search driver cannot finish is not necessarily undrivable (it did not finish the bundled Tidal Causeway at its default settings); the editor's own playtest finish is the normal proof, and the driver is the fallback and the test.
- New server work: submission API, review queue, catalogue writer, report handling for tracks, revocation list. New client work: the Track Market screen and catalogue refresh. None of it changes the simulation, so no rules-version bump.
- The owner has to do two things nobody else can: generate and guard the signing key, and arrange the legal review of the terms. The roadmap treats both as launch gates for the public market, not for the private beta.

## Alternatives considered

- **Open uploads with no review:** fastest, but a first-time author could publish unlawful text or imagery under the project's name; the first-track review costs one moderator action per new author.
- **Pull requests to the repository as the way to share:** needs a GitHub account and git knowledge; unusable for most players.
- **Peer-to-peer sharing between players:** rejected: players are on different networks and all traffic goes through the project's servers (existing rule, `feedback_no_p2p_networking`).
- **Letting authors upload their own pictures and sounds:** a legal and safety burden with no match to the current game; revisit with the 3.0 content format.
