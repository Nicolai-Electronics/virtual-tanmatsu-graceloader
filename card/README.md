# The card this emulator ships with

Whatever is under `int/` and `sd/` here is copied into the emulator's virtual
card before an app starts -- `seed_tree()` in `src/main.c` -- which is how an app
finds the things a real card already had and no app repo contains. Nothing is
overwritten: a card that already has a file keeps its own, so a player's saves
and settings survive being run again.

## `int/icons`

The launcher's shared icon pack: 71 32x32 RGBA PNGs, read off the `/int/icons`
directory of a real Tanmatsu.

Three apps name those files directly rather than shipping their own --
SynthMiner's `main/ui/icons.c`, Paperclips' `main/icons.c` and Race the Synth's
`main/icons.c` all load `/int/icons/esc.png` and `f1.png` .. `f6.png` for their
key caps -- and none of them is in any app repo, so without this pack those apps
draw their key caps blank and say so once per icon in the boot log.

The rest of the pack is what the launcher itself draws with (battery, wifi,
bluetooth, storage, the app-store verbs). Apps are free to use it too, since a
card that runs them has it.
