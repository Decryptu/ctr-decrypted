# CTR world CI

[The CTR world workflow](../../.github/workflows/apworld.yml) packs `ctr.apworld`, runs the world tests and fuzzes the packed world inside Archipelago.

`arms.txt` is the active arm registry: name, full generation count, push generation count, meta template and hook. `matrix.py push` selects arms with a push count; `matrix.py full` selects every arm at its full count. Pushes and pull requests use the quick set. Nightly runs and manual runs with `tier=full` use the full set.

`ctr-world-matrix.yml` is the imported upstream matrix used to compare coverage; the workflow reads `arms.txt`. Add an arm there when importing new upstream coverage. Gem Cup templates replace count, track selection and both gauntlet toggles; a meta name ending in `-gauntlet` enables the toggles, while other Gem Cup arms disable them.

A failed generation uploads its YAML and log as `fuzz-failure-<arm>`. The log records the generation seed and `PYTHONHASHSEED`; use both to reproduce it. See [development testing](../../docs/DEVELOPMENT.md#world-tests-and-fuzzing) for reproduction and release gates.
