"""Build the project site: the repository's own docs as pages, plus the pages
written in site/ itself (the Start here checklist and the YAML builder).

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

SITE_URL = "https://decryptu.github.io/ctr-decrypted/"

# Repository path -> page file. Every page except the home page must be in NAV.
PAGES = {
    "README.md": "index.html",
    "SETUP.md": "guide.html",
    "apworld/ctr/docs/en_Crash Team Racing.md": "world.html",
    "docs/CUSTOM_TRACKS.md": "custom-tracks.html",
    "docs/CUSTOM_RACERS.md": "custom-racers.html",
    "docs/60FPS.md": "60fps.html",
    "docs/PGXP.md": "pgxp.html",
    "docs/SCREEN-FILTERS.md": "screen-filters.html",
    "docs/EXTRAS.md": "extras.html",
    "docs/LEVEL-VISIBILITY.md": "level-visibility.html",
    "docs/CONTROLS.md": "controls.html",
    "docs/STEAM-DECK.md": "steam-deck.html",
    "docs/DEV-TOOLS.md": "dev-tools.html",
}
# Hand-written pages in site/: page file -> (title, description).
HANDMADE = {
    "start.html": ("Start here", "An interactive checklist to join, host or play an Archipelago CTR seed solo."),
    "builder.html": ("YAML builder", "Make your Crash Team Racing player file for Archipelago."),
}
# The side menu: (group, [(page file or URL, label)]).
NAV = [
    ("Play", [("start.html", "Start here"), ("guide.html", "Setup guide"), ("builder.html", "YAML builder"),
              ("world.html", "Archipelago world")]),
    ("Features", [("custom-tracks.html", "Custom tracks"), ("custom-racers.html", "Custom racers"),
                  ("60fps.html", "60 FPS"), ("pgxp.html", "PGXP anti-wobble"), ("screen-filters.html", "Screen filters"),
                  ("extras.html", "Extras"), ("level-visibility.html", "Draw distance"), ("controls.html", "Controls"),
                  ("steam-deck.html", "Linux and Steam Deck")]),
    ("Project", [("dev-tools.html", "Dev tools"), (REPO_URL, "Source code"),
                 (REPO_URL + "/issues/new/choose", "Report a problem")]),
]
DESCRIPTION = "Crash Team Racing (PS1) as a native PC game with the Archipelago multiworld randomizer built in."

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
        if url.startswith(SITE_URL):
            return f'{attr}={quote}{url[len(SITE_URL):] or "index.html"}{quote}'
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
            new = PAGES[target] + (f"#{anchor}" if anchor else "")
        else:
            kind = "tree" if os.path.isdir(os.path.join(ROOT, *target.split("/"))) else "blob"
            new = f"{REPO_URL}/{kind}/main/{target}" + (f"#{anchor}" if anchor else "")
        return f'{attr}={quote}{html.escape(new)}{quote}'
    return re.sub(r'\b(href|src)=(["\'])(.*?)\2', fix, body)


def nav_html(current, toc):
    """The side menu, with the current page's sections under it."""
    out = []
    for group, links in NAV:
        out.append(f'<div class="nav-group">{html.escape(group)}</div>')
        for href, label in links:
            ext = href.startswith("http")
            attrs = ' class="ext"' if ext else (" aria-current=page" if href == current else "")
            out.append(f'<a href="{html.escape(href)}"{attrs}>{html.escape(label)}</a>')
            if href == current and toc:
                out.append('<ul class="toc">' + "".join(
                    f'<li><a href="#{html.escape(i)}">{t}</a></li>' for i, t in toc) + "</ul>")
    return "".join(out)


def page(template, title, current, body, head="", main_class="", body_class="", toc=(), description=DESCRIPTION):
    return (template.replace("{{title}}", html.escape(title)).replace("{{nav}}", nav_html(current, toc))
            .replace("{{description}}", html.escape(description)).replace("{{head}}", head)
            .replace("{{main_class}}", main_class).replace("{{body_class}}", body_class)
            .replace("{{body}}", body))


def sections(body):
    """(id, text) of each h2, for the menu's list of sections."""
    return [(m.group(1), re.sub("<[^>]+>", "", m.group(2)))
            for m in re.finditer(r'<h2 id="([^"]+)">(.*?)</h2>', body)]


def main():
    options_json, out_dir = sys.argv[1], sys.argv[2]
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)
    with open(os.path.join(SITE, "template.html"), encoding="utf-8") as f:
        template = f.read()

    in_nav = {href for _, links in NAV for href, _ in links}
    missing = [dest for dest in list(PAGES.values()) + list(HANDMADE) if dest != "index.html" and dest not in in_nav]
    if missing:
        sys.exit(f"pages missing from NAV: {', '.join(missing)}")
    with open(os.path.join(SITE, "home.html"), encoding="utf-8") as f:
        hero = f.read()

    for src, dest in PAGES.items():
        with open(os.path.join(ROOT, *src.split("/")), encoding="utf-8") as f:
            text = f.read()
        body = rewrite_links(render_md(text), src, out_dir)
        heading = re.search(r"<h1[^>]*>(.*?)</h1>", body)
        name = re.sub("<[^>]+>", "", heading.group(1)) if heading else dest
        if dest == "index.html":
            # The hero replaces README's icon, title and first screenshot (its background).
            body = re.sub(r'^\s*<p[^>]*>\s*<img[^>]*icon-256[^>]*>\s*</p>', "", body)
            body = re.sub(r'<p[^>]*>\s*<img[^>]*readme-oxide[^>]*>\s*</p>', "", body, count=1)
            out = page(template, "CTR Decrypted: Crash Team Racing with Archipelago", dest,
                       hero + f'<article class="box doc home-doc">{body}</article>', body_class="home")
        else:
            out = page(template, f"{name} | CTR Decrypted", dest, f'<article class="box doc">{body}</article>',
                       toc=sections(body))
        with open(os.path.join(out_dir, dest), "w", encoding="utf-8") as f:
            f.write(out)

    with open(os.path.join(SITE, "start.html"), encoding="utf-8") as f:
        start = f.read()
    title, desc = HANDMADE["start.html"]
    with open(os.path.join(out_dir, "start.html"), "w", encoding="utf-8") as f:
        f.write(page(template, f"{title} | CTR Decrypted", "start.html", start,
                     '<script src="start.js" defer></script>', main_class="guide", description=desc))

    # The builder: option docs rendered here, so the page needs no Markdown library.
    with open(options_json, encoding="utf-8") as f:
        options = json.load(f)
    for group in options["groups"]:
        for opt in group["options"]:
            opt["doc"] = render_md(opt["doc"])
    with open(os.path.join(SITE, "builder.html"), encoding="utf-8") as f:
        builder = f.read()
    data = json.dumps(options, separators=(",", ":")).replace("</", "<\\/")
    title, desc = HANDMADE["builder.html"]
    with open(os.path.join(out_dir, "builder.html"), "w", encoding="utf-8") as f:
        f.write(page(template, f"{title} | CTR Decrypted", "builder.html", builder.replace("{{options}}", data),
                     '<script src="builder.js" defer></script>', main_class="wide", description=desc))

    for name in ("style.css", "site.js", "start.js", "builder.js"):
        shutil.copyfile(os.path.join(SITE, name), os.path.join(out_dir, name))
    shutil.copyfile(os.path.join(ROOT, "platform", "icon", "icon-256.png"), os.path.join(out_dir, "icon.png"))
    open(os.path.join(out_dir, ".nojekyll"), "w").close()
    print(f"{len(PAGES) + len(HANDMADE)} pages -> {out_dir}")


if __name__ == "__main__":
    main()
