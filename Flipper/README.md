# Ant Farm for Flipper Zero

A small idle game for Flipper Zero. Take care of an ant colony: feed it, keep it moist and clean, and watch it grow.

## How it works

- The top part of the screen is the surface: grass, a hole (the nest entrance), two water tanks and a trash pile in the right corner.
- The bottom part is the underground ant farm: tunnels, the queen, eggs, larvae and the colony.
- The queen lays eggs. An egg becomes a larva, and a larva becomes a new ant. Everything animates.
- The more food, the cleaner the nest and the moister the soil, the happier the ants, and the more eggs the queen lays.
- Trash appears in the corner over time (faster for bigger colonies). Food runs out and the soil dries over time.
- With no food the colony starts to shrink.
- Top left: food bar and humidity bar. Top right: ant counter and happiness counter.
- Progress is saved to the SD card automatically (`apps_data/ant_farm/save.bin`).

## Controls

| Button | Action |
|--------|--------|
| OK | Feed the colony (crumbs fall into the hole) |
| Left | Clean the trash with a dustpan and a small shovel |
| Right | Moisten the nest: press once to fill the watering can, press again to pour |
| Back | Save and exit |

There are 2 water tanks. A used tank refills by itself after a while.

## Build

Upload the files to a GitHub repository (the `.github/workflows/build.yml` workflow builds the `.fap` for you), or build locally:

1. Clone https://github.com/flipperdevices/flipperzero-firmware
2. Copy this folder to `applications_user/ant_farm/`
3. Run `./fbt fap_ant_farm`
4. Copy `ant_farm.fap` to `apps/Games/` on the Flipper's SD card.
