// "Start here": the questions pick which steps of the checklist show, and
// ticks are kept in localStorage. Any element with data-mode, data-platform,
// data-disc, data-room or data-where shows only when the matching answer is one
// of the listed values. The URL hash carries the answers (start.html#join/deck)
// so a host can send a link that is already filled in.
(function () {
  "use strict";
  const STORE = "ctr-start";
  const GROUPS = ["mode", "platform", "disc", "room", "where"];
  const NEEDS = { join: ["room"], host: ["where"] };
  const TITLES = { join: "Joining a room", host: "Hosting a multiworld", solo: "Playing solo", race: "Just racing" };
  const FINISH = {
    join: "You are connected and racing. Good luck out there!",
    host: "Your room is live. Enjoy the multiworld!",
    solo: "Your solo seed is running. Have a great run!",
    race: "You are all set. See you on the track!",
  };
  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => [...r.querySelectorAll(s)];

  let state = { sel: {}, done: [] };
  try { state = Object.assign(state, JSON.parse(localStorage.getItem(STORE) || "{}")); } catch (e) { /* defaults */ }
  const save = () => { try { localStorage.setItem(STORE, JSON.stringify(state)); } catch (e) { /* storage off */ } };

  // Answers: the hash first, then what was saved, then a guess at the platform.
  const values = {};
  for (const g of GROUPS) values[g] = $$(`input[name=${g}]`).map((i) => i.value);
  function readHash() {
    const sel = {};
    for (const t of decodeURIComponent(location.hash.slice(1)).split("/").filter(Boolean)) {
      for (const g of GROUPS) if (values[g].includes(t)) sel[g] = t;
    }
    if (Object.keys(sel).length) state.sel = Object.assign({}, state.sel, sel);
    for (const g of GROUPS) {
      const input = state.sel[g] && $(`input[name=${g}][value="${state.sel[g]}"]`);
      if (input) input.checked = true;
    }
    return Object.keys(sel).length > 0;
  }
  if (!state.sel.platform) state.sel.platform = /Linux/.test(navigator.userAgent) && !/Android/.test(navigator.userAgent) ? "linux" : "windows";
  const hashSel = readHash();

  const shown = (el) => GROUPS.every((g) => {
    const want = el.dataset[g];
    return want === undefined || (state.sel[g] !== undefined && want.split(" ").includes(state.sel[g]));
  });
  const ready = () => {
    const m = state.sel.mode;
    return !!m && !!state.sel.platform && !!state.sel.disc && (NEEDS[m] || []).every((g) => state.sel[g]);
  };

  const run = $("#checklist");
  const steps = $$(".step", run);
  for (const s of steps) {
    const box = $("input[type=checkbox]", s);
    box.checked = state.done.includes(s.dataset.id);
    box.addEventListener("change", () => {
      const id = s.dataset.id;
      state.done = state.done.filter((x) => x !== id);
      if (box.checked) state.done.push(id);
      save();
      render();
      if (box.checked) {
        const next = $(".step.current", run);
        if (next) next.scrollIntoView({ behavior: matchMedia("(prefers-reduced-motion: reduce)").matches ? "auto" : "smooth", block: "nearest" });
      }
    });
  }

  function render() {
    for (const el of $$("[data-mode],[data-platform],[data-disc],[data-room],[data-where]")) el.hidden = !shown(el);
    const ok = ready();
    run.hidden = !ok;
    if (!ok) return;
    const visible = steps.filter((s) => !s.hidden);
    let current = null;
    for (const s of visible) {
      const done = state.done.includes(s.dataset.id);
      s.classList.toggle("checked", done);
      s.classList.remove("current");
      if (!done && !current) current = s;
    }
    if (current) current.classList.add("current");
    const n = visible.filter((s) => state.done.includes(s.dataset.id)).length;
    $("#hud-count").textContent = `${n}/${visible.length}`;
    $("#hud-lap").textContent = current ? `Step ${visible.indexOf(current) + 1} of ${visible.length}` : "All steps done";
    $("#meter").style.width = `${visible.length ? (100 * n) / visible.length : 0}%`;
    $("#run-title").textContent = TITLES[state.sel.mode];
    $("#finish-text").textContent = FINISH[state.sel.mode];
    run.classList.toggle("done", !current);
  }

  function writeHash() {
    const parts = ["mode", ...(NEEDS[state.sel.mode] || []), "platform", "disc"].map((g) => state.sel[g]).filter(Boolean);
    history.replaceState(null, "", parts.length ? `#${parts.join("/")}` : location.pathname);
  }

  $("#guide").addEventListener("change", (e) => {
    const g = e.target.name;
    if (!GROUPS.includes(g)) return;
    const wasReady = ready();
    state.sel[g] = e.target.value;
    save();
    render();
    writeHash();
    // When the last answer comes in, bring the checklist up.
    if (!wasReady && ready()) run.scrollIntoView({ behavior: "smooth", block: "start" });
  });
  $("#reset").addEventListener("click", () => {
    if (!confirm("Untick every step?")) return;
    state.done = [];
    for (const s of steps) $("input[type=checkbox]", s).checked = false;
    save();
    render();
  });

  // Glossary words: hover, focus or tap shows the definition from the list above.
  let tip = null;
  const hide = () => { if (tip) { tip.remove(); tip = null; } };
  function show(btn) {
    hide();
    const dt = document.getElementById(`t-${btn.dataset.term}`);
    if (!dt) return;
    tip = document.createElement("div");
    tip.className = "tip";
    tip.setAttribute("role", "tooltip");
    tip.innerHTML = `<b>${dt.textContent}</b>: ${dt.nextElementSibling.innerHTML}`;
    document.body.append(tip);
    const r = btn.getBoundingClientRect();
    const w = tip.offsetWidth;
    tip.style.left = `${Math.max(8, Math.min(window.scrollX + r.left, window.scrollX + document.documentElement.clientWidth - w - 8))}px`;
    tip.style.top = `${window.scrollY + r.bottom + 6}px`;
  }
  for (const b of $$(".term")) {
    b.addEventListener("mouseenter", () => show(b));
    b.addEventListener("mouseleave", hide);
    b.addEventListener("focus", () => show(b));
    b.addEventListener("blur", hide);
    b.addEventListener("click", (e) => { e.preventDefault(); tip ? hide() : show(b); });
  }
  window.addEventListener("scroll", hide, { passive: true });

  render();
  if (hashSel && ready()) run.scrollIntoView({ block: "start" });
  // A link on this page to start.html#… changes only the hash.
  window.addEventListener("hashchange", () => {
    if (!readHash()) return;
    save();
    render();
    if (ready()) run.scrollIntoView({ block: "start" });
  });
})();
