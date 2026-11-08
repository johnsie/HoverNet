# Owner actions: what only you can do

Written 8 November 2026. Everything below is a job nobody else can do for the project: it needs your keys, your accounts or your decision. Each has a script or exact commands. Nothing here changes the game or the servers except where it says so.

**Copy this whole kit to your machine with one command** (run from the repository, then use the printed `scp` lines):

    bash scripts/owner/build-owner-kit.sh

It writes `dist/hovernet-owner-kit-YYYYMMDD.tar.gz` (the scripts, a copy of the signing tool's source, and these documents) and prints:

    scp dist/hovernet-owner-kit-YYYYMMDD.tar.gz dist/hovernet-owner-kit-YYYYMMDD.tar.gz.sha256 USER@YOUR-OWN-COMPUTER:~/
    ssh USER@YOUR-OWN-COMPUTER 'cd ~ && sha256sum -c hovernet-owner-kit-YYYYMMDD.tar.gz.sha256 && tar xzf hovernet-owner-kit-YYYYMMDD.tar.gz && cd hovernet-owner-kit-YYYYMMDD && ls'

Nothing in the kit is a prebuilt program, so it runs on any Linux machine that has `g++` (your race server's older system libraries are not a problem).

## Where to run what

| Job | Where | Why |
| --- | --- | --- |
| `check-production.sh` | your own computer (it reaches the server with `ssh john@192.168.10.181`, read-only) | it only looks |
| `make-signing-key.sh`, `sign-file.sh` | **your own trusted computer, never `192.168.10.181`** | that host runs a GitLab runner (`gitlab-runner.service` is active there, next to the race, account and web-gateway services); anyone who can run a CI job on it could read the key. The script refuses to run there |
| everything the Track Market service needs later (the service, the catalogue folder) | `192.168.10.181`, installed by a script I will give you then, with `scp` + `ssh` | it needs the public key only, never the secret one |

## 1. Check the release is live (2 minutes, read-only)

    bash check-production.sh 2.3.26

Checks the GitHub release and its eight files, that hovernet.outiva.com answers, and over ssh that the race server package is that version, the service is active and port 9600 is listening. The race server is `john@192.168.10.181` by default; set `RACE_HOST=user@host` to change it. I ran it on 8 November 2026: the release has all eight files, the site answers, the race server is `2.3.26`, active, listening. What it cannot tell you is whether the GitLab pipeline for the tag is green: look at https://gitlab.outiva.com/devteam/hovernet/-/pipelines, and open https://hovernet.outiva.com to see the new hovercraft picture and `v2.3.26` in the corner.

## 2. Make the production signing key (10 minutes, on a machine you trust, not a build server)

    bash make-signing-key.sh                      # key goes to ~/hovernet-keys/hovernet-catalogue.key

It builds the signing tool from source on that machine, makes the key, proves signing and verifying work (a changed file is refused), refuses to overwrite an existing key, and refuses to run on a CI runner. Then:

1. **Back the key file up offline now** (USB stick kept away from the computer; consider a second copy in a different place). Check the copy: `sha256sum ~/hovernet-keys/hovernet-catalogue.key` on both. If the key is lost nothing already published breaks, but nothing new can be signed until clients are updated with a new key. If it leaks, anyone can sign as the project.
2. **Give me only the public key** (the 64-character line it prints). I put it in the repository and compile it into the clients. Never send or commit the key file.
3. To sign a file later (the catalogue, a release's `SHA256SUMS`): `bash sign-file.sh FILE` writes `FILE.sig` and checks it. To do it from your laptop for a file that lives on a server:

        scp USER@SERVER:/path/to/SHA256SUMS .
        bash sign-file.sh SHA256SUMS
        scp SHA256SUMS.sig USER@SERVER:/path/to/

Until this is done, nothing may be described as signed (docs/signing.md).

## 3. Get the Track Market terms reviewed (the one thing that decides when it can open)

`docs/track-market-terms-draft.md` is a first draft of the terms of use, the author declaration and the takedown procedure, with the questions to put to a lawyer at the end. It is **not legal advice and not in force**. Send it with `docs/adr/0002-content-licence-policy.md`, `docs/adr/0004-track-marketplace.md` and `docs/account-privacy-policy-draft.md` (all in the kit's `docs/`):

    scp docs/*.md USER@LAWYER-OR-OWN-MACHINE:~/hovernet-review/

Fill in the `[OWNER: ...]` placeholders in the draft (who runs HoverNet, the contact address for rights complaints). Until it is reviewed the market can only run as a **private beta for authors you invite**; nothing may be called open to everyone.

## 4. Decide who the first invited authors are

Make a list of the Outiva account names you want to allow to publish during the private beta (a handful of trusted people is right: their first tracks are reviewed by a moderator anyway). Keep it somewhere safe; the submission service will read it as a plain list, one account name per line. Nothing to run yet.

## 5. Decide about the unpushed work

Local commits that are **not pushed anywhere** (the editor groundwork, the compiler's stable sort, the Track Market design, these scripts): see `git log origin/main..main`. They do not change the game players run except that the track compiler now sorts equal levels the same way on every platform (every shipped track compiles to the same bytes, checked). When you want them on the servers:

    git push gitlab main        # GitLab: builds and deploys the web client from main
    git push origin main        # GitHub: runs the Actions build

A release (a tag) is only needed when you want the race server or installers to change; the editor work does not need one yet.

## What I do next without you

The editor's two-dimensional interface, the preview and playtest that records the finishing lap, the submission service and the Track Market screen, in that order ([roadmap-track-editor.md](roadmap-track-editor.md)). Nothing in that list needs your action until the private beta, when I will need the invited-authors list and the public key.
