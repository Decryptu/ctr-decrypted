"""Write the CTR world's options as JSON for the site's YAML builder.

Run from the root of an Archipelago checkout that has apworld/ctr copied into
worlds/ctr (the AP_REF of .github/workflows/apworld.yml):

    python path/to/site/dump_options.py OUT.json

The builder reads only this file, so it always offers the options, defaults
and choices of the world that ships beside it.
"""
import json
import os
import sys
from inspect import cleandoc

sys.path.insert(0, os.getcwd())

import yaml  # noqa: E402  (Archipelago's own dependency)

import Options  # noqa: E402
from Utils import __version__ as ap_version  # noqa: E402
from worlds.AutoWorld import AutoWorldRegister  # noqa: E402

GAME = "Crash Team Racing"


def yaml_text(value):
    if not value:
        return ""
    if isinstance(value, (set, frozenset)):
        value = sorted(value)
    return yaml.safe_dump(value, sort_keys=False, default_flow_style=False).rstrip()


def describe(key, opt):
    d = {"key": key, "name": getattr(opt, "display_name", key), "doc": cleandoc(opt.__doc__ or "")}
    if issubclass(opt, Options.Toggle):
        d.update(kind="toggle", default=bool(opt.default))
    elif issubclass(opt, Options.Choice):
        names = [opt.name_lookup[v] for v in sorted(opt.name_lookup)]
        default = opt.default if isinstance(opt.default, str) else opt.name_lookup[opt.default]
        d.update(kind="textchoice" if issubclass(opt, Options.TextChoice) else "choice",
                 choices=names, default=default)
    elif issubclass(opt, Options.Range):
        d.update(kind="range", min=opt.range_start, max=opt.range_end, default=opt.default)
        if issubclass(opt, Options.NamedRange):
            d["special"] = dict(opt.special_range_names)
    elif issubclass(opt, Options.OptionDict):
        d.update(kind="dict", default=yaml_text(opt.default), keys=sorted(map(str, opt.valid_keys)))
    elif issubclass(opt, Options.OptionSet) and opt.valid_keys:
        d.update(kind="set", choices=sorted(map(str, opt.valid_keys)), default=sorted(map(str, opt.default)))
    elif issubclass(opt, (Options.OptionSet, Options.OptionList)):
        d.update(kind="list", default=yaml_text(opt.default), keys=sorted(map(str, opt.valid_keys)))
    elif issubclass(opt, Options.FreeText):
        d.update(kind="text", default=str(opt.default))
    elif isinstance(opt.default, (list, tuple, dict)):
        # Plando and the like: free-form YAML.
        d.update(kind="dict" if isinstance(opt.default, dict) else "list", default=yaml_text(list(opt.default)
                 if isinstance(opt.default, tuple) else opt.default), keys=[])
    else:
        raise TypeError(f"{key}: no builder field for {opt.__mro__[1].__name__}")
    return d


def main():
    world = AutoWorldRegister.world_types[GAME]
    groups = [{"name": name, "options": [describe(k, o) for k, o in opts.items()]}
              for name, opts in Options.get_option_groups(world, Options.Visibility.template).items()]
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "apworld", "ctr", "archipelago.json"), encoding="utf-8") as f:
        world_version = json.load(f)["world_version"]
    out = {"game": GAME, "ap_version": ap_version, "world_version": world_version,
           "presets": world.web.options_presets, "groups": groups}
    with open(sys.argv[1], "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    print(f"{sum(len(g['options']) for g in groups)} options in {len(groups)} groups -> {sys.argv[1]}")


if __name__ == "__main__":
    main()
