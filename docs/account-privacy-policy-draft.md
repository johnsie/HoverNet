# HoverNet accounts: privacy and deletion policy (DRAFT)

**Status: draft written 2026-10-17 for the owner's review. It has not been reviewed by a lawyer and is not legal advice.** It must be
reviewed by someone qualified in the law of the places where HoverNet is offered (at least the UK/EU GDPR and, if children may play,
the US COPPA and the UK Children's Code) before accounts are switched on. Nothing here is in force until the owner adopts it and the
bracketed items are filled in. The building blocks are in the owner's recorded decision
([owner-decisions.md](owner-decisions.md), "Accounts and sign-in"): accounts are **optional**, sign-in is by a **device key**, and the
only personal data stored is an **email address**. Everything the server does today without accounts is in
[online-services.md](online-services.md), "Privacy and retention", and stays true.

## What the rules are designed around

1. **Playing never needs an account.** A guest can host, join, watch and chat as today. An account only adds things that need to remember
   who someone is (a named place on leaderboards, a recognised display name, moderation that can follow a person rather than an address).
2. **Collect the least that works.** One email address, one device public key per signed-in device, a display name, and the dates
   needed to run the account. No real name, birth date, phone number, location, contacts or payment details.
3. **No selling, no advertising, no third-party trackers.** The web client loads nothing from third-party servers for tracking.
4. **A player can leave completely, and quickly,** without writing to anyone.

## What is stored for an account

| Data | Why | Where | How long |
| --- | --- | --- | --- |
| Email address | to confirm the account and to recover it if every signed-in device is lost; nothing else (no newsletters) | the account store on the operator's server | until the account is deleted |
| Display name | shown to other players, leaderboards and moderators | account store | until deleted or changed |
| Device public key(s), with a device label and the date last used | to sign in without a password; the matching private key never leaves the device | account store | per device, until the player removes it or the account is deleted; a key unused for 12 months is removed |
| Account created, last sign-in | security and the 12-month rule above | account store | until deleted |
| Verified results and replays attributed to the account | leaderboards | results store (replays contain no names, addresses or chat) | until deleted: the attribution is removed, the anonymous replay may remain |
| Sanctions (mute, ban) and the reports behind them | safety; see "What deletion does not remove" | moderation store | as the existing audit-log rules: 180 days after the action ends, reports 30 days after resolution |

Not stored for an account: chat text (the existing 24-hour rule applies), IP addresses beyond the existing connection-limit and
audit-log uses, passwords (there are none), device identifiers other than the public key.

## Lawful basis (for the reviewer to confirm)

Account data is processed to provide the account the player asked for (contract); the moderation records are kept for the legitimate
interest of keeping a public game safe. No consent is needed for either, but sign-up must say clearly, before the email address is
entered, what is stored and link to this policy. [Reviewer: confirm, and decide whether the email address needs a double opt-in.]

## Players' rights and how they are met

- **See what is held, and take a copy.** Settings, "Your account", "Download my data" produces one file with everything in the table
  above that is tied to the account. [Reviewer: confirm the one-month deadline is met by the self-service export.]
- **Correct it.** The display name and the email address can be changed in the same screen (a changed email is confirmed first).
- **Delete the account.** Settings, "Your account", "Delete my account", then a typed confirmation. It takes effect at once for sign-in
  and display (the name is freed and removed from leaderboards), and the data in the table is erased within **30 days**, backups included
  by the end of the backup cycle [state it: 35 days]. A signed-in device can do it; a player who has lost every device can ask for an
  emailed link sent to the address on the account.
- **Object or restrict.** By email to the contact below; answered within one month.
- **Complain.** To the data-protection authority where the player lives (in the UK, the Information Commissioner's Office).

## What deletion does not remove

- **Moderation records.** If a player was banned or muted, a record of the sanction and the report behind it is kept for the periods in
  the table (short, and only to stop a ban being evaded), then purged by the server's hourly purge. This is stated on the deletion screen.
- **Anonymous data.** Replays and counters contain no personal data, so they are not deleted for privacy reasons.
- **Other players' copies.** Chat a player sent to others, and replays others saved, are theirs.

## Children

HoverNet is a racing game that children may enjoy, so accounts are built to need as little as possible. [Owner decision needed:
the minimum age for an account. Proposal: 16 (the highest age of digital consent in the EU), with guests of any age unaffected,
because a guest gives us nothing.] Sign-up asks one question, "I am at least [16]", and does not store the answer or a birth date.
If the owner wants under-age players to have accounts, a parental-consent flow must be designed first and the reviewer must confirm
COPPA and the UK Children's Code requirements.

## Security

- Device keys are Ed25519 (the signing code and its checks already exist: [signing.md](signing.md)); the server stores only public keys,
  so a stolen database cannot sign anyone in.
- Email addresses are stored apart from gameplay data, readable only by the account service, and never written to logs or the audit log.
- Sign-in challenges are single-use and short-lived; account actions (delete, change email, add or remove a device) are rate limited.
- A breach affecting account data is reported to the authority and the players affected within the legal deadline (72 hours to the
  authority under GDPR). [Owner: name who decides and who tells players.]

## Who runs it, and where data goes

- **Controller and contact:** [name of the person or organisation running the official HoverNet service, postal address, privacy contact
  email]. Operators of other servers are their own controllers; their servers do not share accounts with the official one.
- **Processors:** [list the hosting provider and the email-sending provider; sign a data-processing agreement with each; state the country
  the account store is in and, if outside the UK/EEA, the transfer mechanism.]
- **No other recipients,** except where the law requires disclosure.

## Before accounts launch (checklist)

- [ ] A lawyer has reviewed this draft and every bracketed item is filled in.
- [ ] The minimum-age decision is made and the sign-up question matches it.
- [ ] The account store, the export, the deletion job and the 12-month key expiry are built and tested, including deleting an account
      that has results, replays and a sanction, and a restore from backup that does not bring a deleted account back.
- [ ] The privacy page and the sign-up screen link to the final text (desktop and web alike, so they cannot drift).
- [ ] Operators' guide ([operators.md](operators.md)) tells other server operators what is theirs to do.
