// Every page: the side menu (a drawer under 960px), the latest release's
// version and download links, and the "on this page" highlight.
(function () {
  "use strict";
  const REPO = "Decryptu/ctr-decrypted";

  // Drawer -----------------------------------------------------------------
  const side = document.getElementById("side");
  const scrim = document.querySelector(".scrim");
  const openBtn = document.querySelector(".menu-btn");
  const closeBtn = document.querySelector(".close-btn");
  const wide = window.matchMedia("(min-width: 960px)");
  function setOpen(open) {
    if (wide.matches) open = false;
    side.classList.toggle("open", open);
    scrim.hidden = !open;
    openBtn.setAttribute("aria-expanded", String(open));
    document.body.style.overflow = open ? "hidden" : "";
    if (open) (side.querySelector("[aria-current]") || side.querySelector("a")).focus({ preventScroll: true });
    else if (document.activeElement && side.contains(document.activeElement)) openBtn.focus();
  }
  openBtn.addEventListener("click", () => setOpen(true));
  closeBtn.addEventListener("click", () => setOpen(false));
  scrim.addEventListener("click", () => setOpen(false));
  document.addEventListener("keydown", (e) => { if (e.key === "Escape" && side.classList.contains("open")) setOpen(false); });
  side.addEventListener("click", (e) => { if (e.target.closest("a") && !wide.matches) setOpen(false); });
  wide.addEventListener("change", () => setOpen(false));
  // Keep the current page in view in a long menu.
  const cur = side.querySelector("nav a[aria-current]");
  if (cur && cur.offsetTop > side.clientHeight * 0.6) side.scrollTop = cur.offsetTop - side.clientHeight / 3;

  // Latest release -----------------------------------------------------------
  // One API call per session; without it the links stay on the releases page.
  async function latest() {
    const KEY = "ctr-latest-release";
    try {
      const hit = JSON.parse(sessionStorage.getItem(KEY) || "null");
      if (hit && Date.now() - hit.at < 3600e3) return hit.rel;
    } catch (e) { /* no storage */ }
    const r = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, { headers: { Accept: "application/vnd.github+json" } });
    if (!r.ok) throw new Error(r.status);
    const j = await r.json();
    const rel = { tag: j.tag_name, url: j.html_url, assets: j.assets.map((a) => ({ name: a.name, url: a.browser_download_url, size: a.size })) };
    try { sessionStorage.setItem(KEY, JSON.stringify({ at: Date.now(), rel })); } catch (e) { /* no storage */ }
    return rel;
  }
  const PICK = {
    windows: (n) => /-windows-x86\.zip$/.test(n) && !/box-authoring/.test(n),
    linux: (n) => /-linux-x86\.tar\.gz$/.test(n) && !/box-authoring/.test(n),
    apworld: (n) => n === "ctr.apworld",
  };
  window.ctrRelease = latest().then((rel) => {
    for (const e of document.querySelectorAll("[data-latest-version]")) e.textContent = rel.tag;
    for (const a of document.querySelectorAll("[data-download]")) {
      const kind = a.dataset.download;
      const asset = PICK[kind] && rel.assets.find((x) => PICK[kind](x.name));
      if (asset) {
        a.href = asset.url;
        const size = a.querySelector("[data-size]");
        if (size) size.textContent = `${(asset.size / 1048576).toFixed(1)} MB`;
      } else if (kind === "page") a.href = rel.url;
    }
    return rel;
  }).catch(() => null);

  // On this page -------------------------------------------------------------
  const toc = [...side.querySelectorAll(".toc a")];
  if (toc.length && "IntersectionObserver" in window) {
    const byId = new Map(toc.map((a) => [decodeURIComponent(a.hash.slice(1)), a]));
    const heads = [...byId.keys()].map((id) => document.getElementById(id)).filter(Boolean);
    const seen = new Set();
    const mark = () => {
      let here = heads.find((h) => seen.has(h)) || null;
      if (!here) here = heads.filter((h) => h.getBoundingClientRect().top < 0).pop() || null;
      for (const a of toc) a.classList.toggle("here", !!here && byId.get(here.id) === a);
    };
    const io = new IntersectionObserver((entries) => {
      for (const en of entries) en.isIntersecting ? seen.add(en.target) : seen.delete(en.target);
      mark();
    }, { rootMargin: "0px 0px -60% 0px" });
    heads.forEach((h) => io.observe(h));
  }
})();
