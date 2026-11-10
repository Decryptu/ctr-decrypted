# Verifier sweep profiles

These thirteen solo YAMLs are the release gate for the client's connect-time seed verifier. They use the 0.3.0 content-plan options and require Archipelago 0.6.8 or newer.

For each profile, generate a seed with the release `ctr.apworld`, host it locally, and connect the exact release client. Record the `[AP VERIFY]` verdict from its log. Every profile must generate without unknown-option warnings and report `seed OK` with `(solo: definitive)`. A generation error, an indeterminate result or a blocked-goal verdict prevents release until investigated.

The sweep complements world fuzzing: generation proves the world can place items, while the connected client checks the server's actual slot data and placements. A warning on a beatable seed is a false positive; accepting an unbeatable seed is a false negative. Production verifier harnesses cover deliberately blocked graphs separately.

## Coverage

- `profile-community-01`: custom requirement weights, excluded locations, all podium rungs and a relic-based final challenge.
- `profile-candidate-02..06, -08`: vanilla and randomized requirements, shuffled pads, density extremes, goal variants and podium combinations.
- `profile-gap-07b`: the minimum legal Trophy weight with no Key weight.
- `profile-gap-vanilla`: original pad placement and vanilla requirements.
- `profile-gap-pinning`: zero cups and Crystal Challenges with shuffled gems. Their items remain in the content-plan pool rather than pinning to removed checks.
- `profile-gap-minimal`: minimal accessibility.
- `profile-gap-goldmode` and `profile-gap-platmode`: gold and platinum final-challenge requirements.

The profiles retain their item counts, explicit weights and exclusions where supported. Mixed pad shuffles use `pad_placement: shuffle`; an empty former shuffle list uses `vanilla`. Cup profiles select the five original cups with `cup_tracks: off`, and arena profiles select the four original arenas. `item_pool.base.relics` and `podium_rungs` make the intended counts and checks explicit.
