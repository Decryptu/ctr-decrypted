// The YAML builder. Its options come from options.json (site/dump_options.py),
// embedded in the page; nothing here names a CTR option.
(function () {
  "use strict";
  const data = JSON.parse(document.getElementById("options-data").textContent);
  const STORE = "ctr-yaml-builder";
  const $ = (id) => document.getElementById(id);
  const el = (tag, props, ...kids) => {
    const e = Object.assign(document.createElement(tag), props || {});
    for (const k of kids) if (k != null) e.append(k);
    return e;
  };

  // YAML output -----------------------------------------------------------
  const PLAIN = /^[A-Za-z_][A-Za-z0-9_ .'()-]*$/;
  const RESERVED = /^(true|false|yes|no|on|off|null|y|n)$/i;
  function scalar(v) {
    if (typeof v === "number" || typeof v === "boolean") return String(v);
    const s = String(v);
    return PLAIN.test(s) && !RESERVED.test(s) && !/\s$/.test(s) ? s : JSON.stringify(s);
  }
  function dump(v, indent) {
    // Block YAML for preset values (plain objects, arrays, scalars).
    const pad = " ".repeat(indent);
    if (Array.isArray(v)) return v.length ? v.map((x) => `${pad}- ${scalar(x)}`).join("\n") : `${pad}[]`;
    if (v && typeof v === "object") {
      const keys = Object.keys(v);
      if (!keys.length) return `${pad}{}`;
      return keys.map((k) => (v[k] && typeof v[k] === "object")
        ? `${pad}${scalar(k)}:\n${dump(v[k], indent + 2)}` : `${pad}${scalar(k)}: ${scalar(v[k])}`).join("\n");
    }
    return pad + scalar(v);
  }
  const indentText = (text, n) => text.split("\n").map((l) => (l.trim() ? " ".repeat(n) + l : "")).join("\n");

  // One field per option --------------------------------------------------
  // Each returns {node, value() -> raw value, set(raw), yaml() -> "key: ..." lines}.
  function toggleField(o) {
    const box = el("input", { type: "checkbox" });
    return {
      node: el("label", null, box, " On"),
      value: () => box.checked,
      set: (v) => { box.checked = v === true || v === 1 || v === "true"; },
      yaml: () => `  ${o.key}: ${box.checked}`,
    };
  }
  function choiceField(o) {
    const sel = el("select");
    for (const c of o.choices.concat(["random"])) sel.append(el("option", { value: c, textContent: c }));
    return {
      node: sel,
      value: () => sel.value,
      set: (v) => { sel.value = typeof v === "number" ? o.choices[v] ?? o.default : v; },
      yaml: () => `  ${o.key}: ${scalar(sel.value)}`,
    };
  }
  function rangeField(o) {
    const num = el("input", { type: "number", min: o.min, max: o.max, step: 1 });
    const slide = el("input", { type: "range", min: o.min, max: o.max, step: 1 });
    num.addEventListener("input", () => { slide.value = num.value; });
    slide.addEventListener("input", () => { num.value = slide.value; num.dispatchEvent(new Event("input", { bubbles: true })); });
    const kids = [slide, num];
    if (o.special && Object.keys(o.special).length) {
      const named = el("select", { title: "Named values" }, el("option", { value: "", textContent: "named…" }));
      for (const [n, v] of Object.entries(o.special)) named.append(el("option", { value: v, textContent: `${n} (${v})` }));
      named.addEventListener("change", () => {
        if (named.value !== "") { num.value = slide.value = named.value; named.value = ""; num.dispatchEvent(new Event("input", { bubbles: true })); }
      });
      kids.push(named);
    }
    const clamp = () => Math.min(o.max, Math.max(o.min, Math.round(Number(num.value) || 0)));
    return {
      node: el("div", { className: "control" }, ...kids),
      value: () => clamp(),
      set: (v) => {
        if (typeof v === "string" && o.special && v in o.special) v = o.special[v];
        num.value = slide.value = typeof v === "number" ? v : o.min;
      },
      yaml: () => `  ${o.key}: ${clamp()}`,
    };
  }
  function setField(o) {
    const boxes = o.choices.map((c) => [c, el("input", { type: "checkbox", value: c })]);
    return {
      node: el("div", { className: "checks" }, ...boxes.map(([c, b]) => el("label", null, b, c))),
      value: () => boxes.filter(([, b]) => b.checked).map(([c]) => c),
      set: (v) => { const on = new Set(v || []); for (const [c, b] of boxes) b.checked = on.has(c); },
      yaml() {
        const v = this.value();
        return v.length ? `  ${o.key}:\n${v.map((c) => `    - ${scalar(c)}`).join("\n")}` : `  ${o.key}: []`;
      },
    };
  }
  function textField(o) {
    const area = el("textarea", { className: "yaml", spellcheck: false, rows: Math.min(14, Math.max(3, o.default.split("\n").length + 1)) });
    const kids = [area];
    if (o.keys && o.keys.length) kids.push(el("div", { className: "keys", textContent: "Keys: " + o.keys.join(", ") }));
    return {
      node: el("div", { className: "control" }, ...kids),
      value: () => area.value,
      set: (v) => { area.value = typeof v === "string" ? v : dump(v, 0); },
      yaml: () => area.value.trim()
        ? `  ${o.key}:\n${indentText(area.value.replace(/\s+$/, ""), 4)}`
        : `  ${o.key}: ${o.kind === "dict" ? "{}" : "[]"}`,
    };
  }
  function freeTextField(o) {
    const input = el("input", { type: "text" });
    return {
      node: input,
      value: () => input.value,
      set: (v) => { input.value = v ?? ""; },
      yaml: () => `  ${o.key}: ${scalar(input.value)}`,
    };
  }
  const MAKERS = { toggle: toggleField, choice: choiceField, textchoice: choiceField, range: rangeField,
                   set: setField, list: textField, dict: textField, text: freeTextField };

  // Page ------------------------------------------------------------------
  const fields = [];
  const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  for (const group of data.groups) {
    const box = el("details", { className: "group", open: true }, el("summary", { textContent: group.name }));
    for (const o of group.options) {
      const f = MAKERS[o.kind](o);
      f.set(o.default);
      f.def = f.value();
      f.opt = o;
      const reset = el("button", { className: "reset", textContent: "default", type: "button", title: "Back to the default" });
      reset.addEventListener("click", () => { f.set(o.default); update(); });
      f.row = el("div", { className: "option" },
        el("div", null, el("div", { className: "name", textContent: o.name + " " }, reset), el("div", { className: "key", textContent: o.key })),
        f.node.classList?.contains("control") ? f.node : el("div", { className: "control" }, f.node));
      if (o.doc) {
        const help = el("details", { className: "help" }, el("summary", { textContent: "About this option" }));
        help.insertAdjacentHTML("beforeend", o.doc);
        f.row.append(help);
      }
      fields.push(f);
      box.append(f.row);
    }
    $("groups").append(box);
  }

  const presets = data.presets || {};
  if (Object.keys(presets).length) {
    $("preset").append(el("option", { value: "", textContent: "choose…" }));
    for (const name of Object.keys(presets)) $("preset").append(el("option", { value: name, textContent: name }));
    $("preset").addEventListener("change", () => {
      const p = presets[$("preset").value];
      if (!p) return;
      for (const f of fields) f.set(f.opt.key in p ? p[f.opt.key] : f.opt.default);
      $("preset").value = "";
      update();
    });
  } else {
    $("preset").parentElement.hidden = true;
  }
  $("world-version").textContent = data.world_version;

  function yamlText() {
    const name = $("player-name").value.trim();
    const desc = $("player-desc").value.trim();
    return [
      `# Made with the CTR Decrypted YAML builder for ctr.apworld ${data.world_version}.`,
      `name: ${scalar(name || "Player{number}")}`,
      desc ? `description: ${scalar(desc)}` : null,
      `game: ${scalar(data.game)}`,
      "requires:",
      `  version: ${data.ap_version}`,
      "",
      `${scalar(data.game)}:`,
      ...fields.map((f) => f.yaml()),
      "",
    ].filter((l) => l !== null).join("\n");
  }

  function save() {
    try {
      const values = {};
      for (const f of fields) if (!same(f.value(), f.def)) values[f.opt.key] = f.value();
      localStorage.setItem(STORE, JSON.stringify({ name: $("player-name").value, desc: $("player-desc").value, values }));
    } catch (e) { /* storage off: the page still works */ }
  }
  function load() {
    try {
      const s = JSON.parse(localStorage.getItem(STORE) || "null");
      if (!s) return;
      $("player-name").value = s.name || "";
      $("player-desc").value = s.desc || "";
      for (const f of fields) if (f.opt.key in (s.values || {})) f.set(s.values[f.opt.key]);
    } catch (e) { /* ignore a bad or missing store */ }
  }

  function update() {
    for (const f of fields) f.row.classList.toggle("changed", !same(f.value(), f.def));
    $("yaml").textContent = yamlText();
    $("status").textContent = "";
    save();
  }

  $("builder").addEventListener("input", update);
  $("builder").addEventListener("change", update);
  $("reset-all").addEventListener("click", () => {
    if (!confirm("Put every option back to its default?")) return;
    for (const f of fields) f.set(f.opt.default);
    update();
  });
  $("download").addEventListener("click", () => {
    if (!$("player-name").reportValidity()) return;
    const blob = new Blob([yamlText()], { type: "text/yaml" });
    const a = el("a", { href: URL.createObjectURL(blob), download: `${$("player-name").value.trim().replace(/[^\w.-]+/g, "_")}.yaml` });
    document.body.append(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(a.href);
  });
  $("copy").addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(yamlText());
      $("status").textContent = "Copied.";
    } catch (e) {
      $("status").textContent = "Copy failed: select the text below instead.";
    }
  });

  load();
  update();
  $("builder").hidden = false;
})();
