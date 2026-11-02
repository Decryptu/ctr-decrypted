"""Build the project site: the repository's own docs as pages, plus the YAML builder.

    python site/build.py OPTIONS_JSON OUT_DIR

OPTIONS_JSON comes from site/dump_options.py. The pages are rendered from the
Markdown files listed in PAGES, so the site never holds a second copy of a doc.
A link to another listed file becomes a link to its page, a link to anything
else in the repository goes to GitHub, and images are copied next to the pages.
Needs the `markdown` package.
"""
import html
import json
import os
import posixpath
import re
import shutil
import sys

import markdown

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE = os.path.join(ROOT, "site")
REPO_URL = "https://github.com/Decryptu/ctr-decrypted"

# Repository path -> (page file, nav title or None for pages only linked to).
PAGES = {
    "README.md": ("index.html", "Home"),
    "SETUP.md": ("guide.html", "Guide"),
    "apworld/ctr/docs/en_Crash Team Racing.md": ("world.html", "Archipelago world"),
    "docs/DEV-TOOLS.md": ("dev-tools.html", "Dev tools"),
    "docs/CUSTOM_TRACKS.md": ("custom-tracks.html", None),
    "docs/CUSTOM_RACERS.md": ("custom-racers.html", None),
    "docs/60FPS.md": ("60fps.html", None),
    "docs/PGXP.md": ("pgxp.html", None),
    "docs/SCREEN-FILTERS.md": ("screen-filters.html", None),
    "docs/EXTRAS.md": ("extras.html", None),
    "docs/LEVEL-VISIBILITY.md": ("level-visibility.html", None),
    "docs/CONTROLS.md": ("controls.html", None),
    "docs/STEAM-DECK.md": ("steam-deck.html", None),
}
BUILDER = ("builder.html", "YAML builder")
NAV = [(page, title) for page, title in PAGES.values() if title]
NAV.insert(2, BUILDER)

LIST_ITEM = re.compile(r"^\s*(?:[-*+]|\d+\.)\s")


def loosen_lists(text):
    """GitHub starts a list right after a paragraph line; Python-Markdown wants a
    blank line first. Add it, outside code fences."""
    out, fence, prev = [], False, ""
    for line in text.split("\n"):
        if line.lstrip().startswith("```"):
            fence = not fence
        elif (not fence and LIST_ITEM.match(line) and prev.strip() and not LIST_ITEM.match(prev)
              and not prev.startswith((" ", "\t", "|"))):
            out.append("")
        out.append(line)
        prev = line
    return "\n".join(out)


def render_md(text):
    return markdown.markdown(loosen_lists(text), extensions=["extra", "toc", "sane_lists"],
                             extension_configs={"toc": {"permalink": False}})


def rewrite_links(body, src, out_dir):
    """Point relative links at pages, GitHub or copied images."""
    src_dir = posixpath.dirname(src)

    def fix(m):
        attr, quote, url = m.group(1), m.group(2), m.group(3)
        if re.match(r"^[a-z]+:|^#|^//", url):
            return m.group(0)
        path, _, anchor = url.partition("#")
        target = posixpath.normpath(posixpath.join(src_dir, html.unescape(path)))
        if attr == "src":
            dest = os.path.join(out_dir, *target.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copyfile(os.path.join(ROOT, *target.split("/")), dest)
            new = target
        elif target in PAGES:
            new = PAGES[target][0] + (f"#{anchor}" if anchor else "")
        else:
            kind = "tree" if os.path.isdir(os.path.join(ROOT, *target.split("/"))) else "blob"
            new = f"{REPO_URL}/{kind}/main/{target}" + (f"#{anchor}" if anchor else "")
        return f'{attr}={quote}{html.escape(new)}{quote}'
    return re.sub(r'\b(href|src)=(["\'])(.*?)\2', fix, body)


def page(template, title, current, body, head=""):
    nav = "".join(f'<a href="{p}"{" aria-current=page" if p == current else ""}>{t}</a>' for p, t in NAV)
    return (template.replace("{{title}}", html.escape(title)).replace("{{nav}}", nav)
            .replace("{{head}}", head).replace("{{body}}", body))


def main():
    options_json, out_dir = sys.argv[1], sys.argv[2]
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)
    with open(os.path.join(SITE, "template.html"), encoding="utf-8") as f:
        template = f.read()

    for src, (dest, title) in PAGES.items():
        with open(os.path.join(ROOT, *src.split("/")), encoding="utf-8") as f:
            text = f.read()
        body = rewrite_links(render_md(text), src, out_dir)
        heading = re.search(r"<h1[^>]*>(.*?)</h1>", body)
        name = title or (re.sub("<[^>]+>", "", heading.group(1)) if heading else dest)
        with open(os.path.join(out_dir, dest), "w", encoding="utf-8") as f:
            f.write(page(template, f"{name} | CTR Decrypted", dest, f'<article class="doc">{body}</article>'))

    # The builder: option docs rendered here, so the page needs no Markdown library.
    with open(options_json, encoding="utf-8") as f:
        options = json.load(f)
    for group in options["groups"]:
        for opt in group["options"]:
            opt["doc"] = render_md(opt["doc"])
    with open(os.path.join(SITE, "builder.html"), encoding="utf-8") as f:
        builder = f.read()
    data = json.dumps(options, separators=(",", ":")).replace("</", "<\\/")
    with open(os.path.join(out_dir, BUILDER[0]), "w", encoding="utf-8") as f:
        f.write(page(template, "YAML builder | CTR Decrypted", BUILDER[0],
                     builder.replace("{{options}}", data), '<script src="builder.js" defer></script>'))

    for name in ("style.css", "builder.js"):
        shutil.copyfile(os.path.join(SITE, name), os.path.join(out_dir, name))
    shutil.copyfile(os.path.join(ROOT, "platform", "icon", "icon-256.png"), os.path.join(out_dir, "icon.png"))
    open(os.path.join(out_dir, ".nojekyll"), "w").close()
    print(f"{len(PAGES) + 1} pages -> {out_dir}")


if __name__ == "__main__":
    main()
