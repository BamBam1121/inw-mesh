# INW Mesh, brand and theme

The look comes from the INW Mesh merch store (inwmerch.mowden.top): a terminal style.
Everything (this firmware's UI, the web flasher, the boot splash) should read like one
system. Match it.

## Palette
| Token | Hex | Use |
|---|---|---|
| bg        | `#060a09` | near-black background |
| panel     | `#0b120e` | cards and panels |
| line      | `#16241c` | hairline borders, grid |
| green     | `#3dffa8` | the accent: headings, prompts, glow |
| green-dim | `#1f6f4e` | secondary green |
| txt       | `#b9d4c6` | body text (cool off-white green) |
| dim       | `#5f8074` | muted labels, comments |
| amber     | `#e6b955` | warnings only |

The green glows. Give it a soft `text-shadow: 0 0 8px rgba(61,255,168,.5)`.

## Motifs
- Monospace everywhere. Uppercase headings with wide letter-spacing.
- `//` comment prefix for descriptive lines (`// we build meshcore networks`).
- Boot-log panel: a fake terminal (`root@pager:~$`) streaming `... OK` lines
  (`lora radio init 915MHz ... OK`). Reuse it on the flasher and the device splash.
- Faint grid overlay on the background.
- Status marks: a small `LIVE` dot, `SKU 00xx`, `[ LOW STOCK ]` bracket tags.

## On the device (480x222 color IPS)
- Boot splash is the boot-log motif in green on black.
- Home screen uses these tokens; the mascot and animation sit on the grid background.
- Later: swappable asset packs (Momentum-style) so the theme can be reskinned.
