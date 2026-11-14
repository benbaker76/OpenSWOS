# scripts/worldgen — the world data generator

Generates a football world's squads (World 2026-27 first) from the original sources —
Transfermarkt (the open dataset, or pages saved from your own browser), EA SPORTS FC's ratings
feed, Wikipedia / Wikidata — in the SWOS 96/97 Mod's pack format.

**The user guide is [docs/WORLD_DATAGEN.md](../../docs/WORLD_DATAGEN.md)**: setup on Windows,
every command with an example, how to save the Transfermarkt pages, a worked example for one
league, troubleshooting, and what it cannot do yet. Python 3, standard library only.

```
python3 -I scripts/worldgen/tmpages.py urls --country eng --division 0   # the pages to save
python3 -I scripts/worldgen/tmpages.py check <folder> --country eng --division 0
python3 -I scripts/worldgen/gen.py --tm both --tm-pages <folder>          # generate
python3 -I scripts/worldgen/compare.py                                    # vs the Mod's packs
```
