# Craft designs

The Garage lets a player change the visible shape, colours and paint of a
hovercraft. These are cosmetic changes only: the race still uses the selected
stock craft for its handling, speed, collision shape and simulation state. A
design never changes race hashes, replays or golden frames, and it does not
change the race camera.

## In the game

Open **Garage** from the main menu. Pick a preview hull, adjust length, width,
height and nose taper, choose body and cockpit colours, and paint the 32 x 32
livery. Paint tools include pencil, eraser, fill, colour picker, mirror and
undo. Save a design in one of eight slots and choose whether it is used in
races. The selected preview hull is only for the Garage picture; choose the
craft you want to drive in the normal pre-race craft selection.

Saved designs are stored in the user's configuration directory under `craft/`
as `design0.hcd` through `design7.hcd`, with the active slot in `active`.
Browser builds keep the same files in the browser's IndexedDB-backed
configuration directory. A missing or invalid file is treated as an empty slot.

When an online race client advertises the craft-design capability, it sends its
active design to the race server. The server validates and relays the design to
other capable clients by player slot. Clients without that capability continue
to receive the stock appearance. The format is bounded to 640 bytes and
validated before the server relays it.

## Account service

The optional Outiva account service also provides authenticated `GET` and `PUT`
`/account/craft` endpoints. They store up to eight base64-encoded `.hcd`
designs and an active slot in `/var/lib/hovernet-account/craft.json` by the
account's immutable OIDC subject. The service validates every design against
the same HCD1 layout and checksum as the game, and creates the file with mode
0600. A missing account service does not affect guest play.

The Garage currently saves to the local configuration directory; it does not
automatically synchronize those files with `/account/craft`.

## Format and tests

HCD1 contains a hull index for the preview, four bounded shape percentages,
body and accent RGB values and strengths, a 16-colour palette, 1024 packed
4-bit paint indices, a printable ASCII name (up to 24 bytes), and a 32-bit
FNV-1a checksum. `Authority::DecodeCraftDesign` rejects invalid sizes,
checksums, hulls, shape values and names; editor input is clamped by
`Authority::SanitiseCraftDesign`.

Focused checks:

```sh
python3 scripts/test-web-account.py
cmake --build build/linux --target HoverNetCraftDesignTest HoverNetCraftDesignNetTest HoverNetCraftLookTest HoverNetGame2Player
ctest --test-dir build/linux -R 'HoverNetCraftDesign|HoverNetCraftLook|HoverNetCraftDesignNet' --output-on-failure
```

`HoverNetCraftDesign` checks the format, `HoverNetCraftDesignNet` checks server
validation and relay behavior (including unchanged simulation hashes), and
`HoverNetCraftLook` checks model variants, colour and the Garage preview.
