# LBJ Edit: the positions the LBJ popup measures from

> **Deprecated (v0.22).** The five positions and the primary are now edited from
> the resident settings menu itself - `LBJPRI` picks the slot, `LBJLAT` and
> `LBJLON` type its latitude and longitude (see `../lbj/README.md`, v0.22), and the
> store is a 4 KiB sector of the firmware's own (`App/app/lbj_pos.c`). This app
> is kept, and still builds, only for radios that already have it: **the popup
> still reads its store as a fallback** when the menu has never set a position,
> so nothing that was entered here is lost. There is no reason to install it
> again, and the app is no longer rebuilt for new firmware versions.

An overlay app that keeps **five positions** and marks one of them as the
**primary**. The resident LBJ popup uses that primary position to work out the
distance to a decoded train (see `../lbj/README.md`), so the address does not
have to be re-entered to move between sites, and changing it does not touch what
APRS TX beacons.

```
┌─────────────────────────────────────┐
│           LBJ EDIT                  │  title
│ LAT 1                               │  the menu: a latitude and a longitude
│ LON 1*                    30 35.16  │  per slot, then PRIMARY; "*" marks the
│ LAT 2                               │  slot the popup is reading
│           01/11                     │  selected / count
│ MENU=EDIT EXIT=SAVE                 │  keys, or the verdict on the field
└─────────────────────────────────────┘
```

| Key | |
|---|---|
| UP / DOWN (UV-K1: LEFT / RIGHT) | pick an item; on `PRIMARY`, walk none / 1…5 |
| MENU | open the selected coordinate for editing; in the editor, keep the edit |
| `0`-`9` | in the editor: write the digit under the cursor and step on |
| EXIT | in the editor, undo the field; on the menu, save and quit |

## Where the five positions live

In **this app's own config area**, the bytes the resident `cfg_load`/`cfg_save`
move (96 B since API level 3). That is why the app carries no config-proxy
descriptor: its `cfg_*` calls are its own store, not another app's asset, and
the resident reads it back with `APP_ReadSlotConfig()`.

`lbjedit_pos.h` holds the layout:

| offset | |
|---|---|
| 0 | `"LBJ1"` magic |
| 4 | version (1) |
| 5 | primary slot, or `0xFF` for none |
| 6 | reserved |
| 8 | 5 slots × 13 decimal digits |

Each slot is the same 13 digits APRS Edit edits - latitude `DD MM hh` then
longitude `DDD MM hh`, hundredths of a minute, **north and east only** - so the
resident needs no conversion it does not already have. A slot that still holds
erased flash is an unfilled one: this editor draws it as dashes, and a digit
above 9 fails the resident's check, so nothing has to be flagged. 0.01' steps
are about 18.5 m, far below the 0.1 km the popup displays.

`1e-4` degrees come out of `degrees * 10000 + (MMhh * 10000) / 6000`, the
resident's conversion.

## Notes

- The store is only written when something was edited, so an app that is opened
  and left alone leaves the APRS TX profile in charge of the readout.
- A primary that points at an unfilled slot, or `--` (none), also hands the
  readout back to the APRS TX profile - that is how it is "given back" without
  clearing anything.
- Deleting the app deletes the store with it (the slot erase takes the config
  area), and the popup goes back to the APRS TX profile.
- Editing a coordinate snapshots the store: EXIT inside the editor restores it,
  MENU keeps it.
- Build: `./build.sh` (API level 3, no config-proxy capability required). About
  1.5 KiB of the 4 KiB overlay, so there is room to grow.
