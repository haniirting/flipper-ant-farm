# Ant Farm for Flipper Zero

A small idle game for Flipper Zero. Take care of an ant colony: feed it, keep the nest clean and watch it grow.

## How it works

- The queen lays eggs. An egg becomes a larva, and a larva becomes a new ant.
- The more you feed the colony and the cleaner the nest is, the happier the ants are, and the more eggs the queen lays.
- Trash appears on the arena over time, faster for bigger colonies.
- Food runs out over time. With no food the colony starts to shrink.
- Top right: ant counter and happiness counter. Top left: food bar.

## Controls

| Button | Action |
|--------|--------|
| OK | Feed the colony |
| Left | Clean up all the trash |
| Back | Exit |

## Build

1. Clone the Flipper Zero firmware: https://github.com/flipperdevices/flipperzero-firmware
2. Copy this folder to `applications_user/ant_farm/` inside the firmware repo.
3. Build the app:

   ```
   ./fbt fap_ant_farm
   ```

4. Copy the resulting `ant_farm.fap` (from `build/.../.extapps/`) to `apps/Games/` on the Flipper's SD card.

## Files

- `ant_farm.c` - game source
- `application.fam` - app manifest
