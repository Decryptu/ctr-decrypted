"""Make the fuzzer print the seed it draws, so a failure can be generated again.

The pinned fuzz.py draws each generation's seed with random.randint and never
records it. This prints it, with the process's PYTHONHASHSEED, into the output
the fuzzer saves next to a failing YAML. Run on the fuzz.py the workflow
downloads; fails when that file no longer has the line it patches.

    python log-seed.py archipelago/fuzz.py
"""
import sys

DRAW = '            "seed": random.randint(0, 1000000000),\n'
SETTINGS = "    settings = get_settings()\n"

path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    text = f.read()
if text.count(DRAW) != 1 or text.count(SETTINGS) != 1:
    sys.exit(f"{path}: the seed draw moved; update log-seed.py for this fuzzer")
text = text.replace(SETTINGS, SETTINGS + "    fuzz_seed = random.randint(0, 1000000000)\n"
                    "    print(f\"fuzz seed {fuzz_seed}, PYTHONHASHSEED {os.environ.get('PYTHONHASHSEED')}\")\n")
text = text.replace(DRAW, '            "seed": fuzz_seed,\n')
with open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(text)
