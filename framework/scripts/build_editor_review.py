# Copyright Francesco Di Blasi 2026 - All Rights Reserved

"""Package staged Godot screenshots and the editor design guide for offline review."""

import argparse
import json
import re
import shutil
from html import escape
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
EDITOR_ASSETS = ROOT / "project" / "addons" / "crimson-framework" / "editor"
SPACING_HEADER = ROOT / "src" / "editor" / "ui" / "editor_spacing.hpp"
THUMBNAIL_NAME = re.compile(r"[^a-z0-9-]+")

TYPE_SPECIMENS = {
    "type_body": ("Choose a target", 13, 450),
    "type_secondary": ("Changes apply on confirmation", 11, 450),
    "type_title": ("Input Map", 16, 600),
}

FIGURE_SPECIMENS = {
    "radius_control": ("editor-components-text-input", "Field corners"),
    "radius_popup": ("editor-components-native-menu-popup-detail", "Popup frame and option rows"),
    "pill_actions": ("editor-components-dialog-form", "Rounded action buttons"),
    "graph_pins": ("editor-components-graph", "Connected and open graph pins"),
    "hover_state": ("editor-components-button-hover", "A hovered command"),
    "selected_state": ("editor-components-select-keyboard-focus-detail", "Highlighted and committed options"),
    "keyboard_state": ("editor-components-tabs-switched", "Keyboard-selected tab"),
    "field_states": ("editor-components-text-input", "Editable, read-only, disabled, and invalid fields"),
    "tree_inspector": ("10-input-map-modifier-inspector", "Binding tree and modifier inspector"),
    "menu_pattern": ("02-input-map-add-action-menu", "Input Map action menu"),
    "dialog_actions": ("editor-components-dialog-removal", "Destructive confirmation footer"),
    "graph_anatomy": ("editor-components-graph", "Title, data pins, and execution pins"),
    "tool_workbench": ("visual-script-100", "Visual Script workbench"),
    "button_anatomy": ("editor-components-button", "Button treatments"),
    "select_anatomy": ("editor-components-select-popup-detail", "Select options and trailing check"),
    "field_anatomy": ("editor-components-field-group", "Labels, fields, and help"),
    "dialog_anatomy": ("editor-components-dialog-form", "Scrim, panel, form, and footer"),
}

STYLE = """
@font-face{font-family:Inter;src:url(InterVariable.ttf) format('truetype');font-weight:100 900;font-display:swap}
:root{color-scheme:dark;background:#101011;color:#ededee;font:14px/1.55 Inter,system-ui,sans-serif}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0}
body:has(dialog[open]){overflow:hidden}
main{max-width:1512px;margin:auto;padding:32px clamp(var(--space-xl),4vw,56px) 56px}
h1,h2,h3,p,figure,dl,dd{margin:0}
h1{font-size:28px;font-weight:650;letter-spacing:-.045em;line-height:1.25}
h2{font-size:19px;font-weight:620;letter-spacing:-.025em;margin-bottom:12px}
h3{font-size:15px;font-weight:600}
a{color:#b4b3f7;text-underline-offset:3px}
p,.muted,figcaption,dd{color:#a8a8ad}
code{font:12px/1.5 ui-monospace,monospace;overflow-wrap:anywhere}
.intro{margin-top:var(--space-md);max-width:68ch}
.build{margin-top:var(--space-md);font-size:12px}
.links{display:flex;gap:var(--space-md) 20px;flex-wrap:wrap;margin-top:20px;font-size:13px}
nav{position:sticky;top:0;z-index:1;display:flex;gap:var(--space-xxl);overflow:auto;scrollbar-width:thin;scrollbar-color:#4c4d51 transparent;background:#101011;border-bottom:1px solid #29292c;margin:20px 0 32px}
nav a{color:#c3c3c7;text-decoration:none;padding:10px 0;white-space:nowrap;font-weight:510}
nav a:hover{color:#fff}
section{margin:0 0 32px;scroll-margin-top:64px}
section>p{margin-bottom:16px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(290px,100%),1fr));gap:var(--space-lg);align-items:start}
figure{min-width:0;background:#19191b;border:1px solid #2b2c2e;border-radius:10px;overflow:hidden}
button{font:inherit;color:inherit;cursor:pointer}
button:focus-visible{outline:none}
.thumbnail{display:block;width:100%;aspect-ratio:16/9;padding:var(--space-md);border:0;border-bottom:1px solid #2b2c2e;background:#131315}
.thumbnail:hover{background:#212123}
.thumbnail:focus-visible{background:#212123}
.thumbnail img{display:block;width:100%;height:100%;object-fit:contain}
figcaption{padding:var(--space-md) var(--space-lg);font-size:13px}
.guide-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(354px,100%),1fr));gap:var(--space-lg);align-items:start}
.guide-card{background:#19191b;border:1px solid #2b2c2e;border-radius:12px;padding:var(--space-xl)}
.guide-card>p{margin-top:var(--space-xs)}
.evidence{margin-top:var(--space-md);display:flex;gap:var(--space-xs) var(--space-md);flex-wrap:wrap}
.evidence button{border:0;background:none;color:#b4b3f7;padding:var(--space-xs) 0;text-align:left}
.evidence button:hover,.evidence button:focus-visible{text-decoration:underline;text-underline-offset:3px}
dl{margin-top:var(--space-lg)}
.token{display:grid;grid-template-columns:1fr auto;gap:var(--space-xxs) var(--space-lg);padding:var(--space-md) 0;border-top:1px solid #2b2c2e}
.token dt{font-weight:550;display:flex;align-items:center;gap:var(--space-md)}
.token dd{text-align:right;font-size:13px}
.token .token-note{grid-column:1/-1;text-align:left}
.swatch{width:14px;height:14px;border:1px solid #ffffff28;border-radius:4px;flex:none}
.token .token-visual{grid-column:1/-1;margin-top:var(--space-xs);text-align:left}
.token-spacing{display:grid;grid-template-columns:52px 40px minmax(40px,1fr)}
.token.token-spacing>.token-visual{grid-column:2;grid-row:1;align-self:center;margin:0}
.token-spacing>.token-measurement{grid-column:3;grid-row:1}
.spacing-mark{display:block;width:var(--measure);height:var(--space-xs);background:#a89dfa}
.text-sample{padding:var(--space-md) var(--space-lg);background:#121213;border-radius:8px;color:#e8e7ea;font-size:var(--sample-size);font-weight:var(--sample-weight)}
.text-sample[data-tone=muted]{color:#a0a0a6}
.component-example{display:flex;align-items:center;gap:var(--space-lg);flex-wrap:wrap;padding:var(--space-md) var(--space-lg);background:#121213;border-radius:8px;color:#e8e7ea}
.component-example label{display:flex;align-items:center;gap:var(--space-xs);cursor:pointer}
.component-example input{accent-color:#5e6ad2}
.component-example input[role=switch]{appearance:none;width:22px;height:14px;margin:0;background:#727374;border-radius:7px;cursor:pointer}
.component-example input[role=switch]::before{content:"";display:block;width:10px;height:10px;margin:2px;background:#fffeff;border-radius:50%}
.component-example input[role=switch]:checked{background:#5e6ad2}
.component-example input[role=switch]:checked::before{transform:translateX(8px)}
.component-example nav{position:static;overflow:visible;margin:0;background:none;width:100%}
.component-example nav button{font-size:14px;font-weight:510;color:#8a8f98;background:none;border:none;border-bottom:1px solid transparent;padding:var(--space-md) 0;min-height:42px}
.component-example nav button[aria-selected=true]{color:#f7f8f8;border-color:#f7f8f8}
.component-example nav button:hover{color:#d0d6e0}
.example-panel{font-size:13px;color:#e8e7ea}
.guide-image{display:grid;width:100%;gap:var(--space-xs);padding:var(--space-xs);text-align:left;color:#a8a8ad;background:#121213;border:1px solid #2b2c2e;border-radius:8px}
.guide-image:hover,.guide-image:focus-visible{background:#232326;color:#e8e7ea}
.guide-image img{display:block;width:100%;max-height:220px;object-fit:contain;border-radius:4px}
.guide-image span{padding:0 var(--space-xs);font-size:12px}
dialog{width:100%;max-width:none;height:100%;height:100dvh;max-height:none;margin:0;padding:0;border:0;background:#101011;color:#ededee}
dialog[open]{display:grid;grid-template-rows:auto minmax(0,1fr) auto}
.viewer-bar,.viewer-caption{padding:var(--space-lg) clamp(var(--space-xl),3vw,32px);background:#19191b}
.viewer-bar{display:flex;justify-content:space-between;gap:var(--space-lg);align-items:center;border-bottom:1px solid #2b2c2e}
.viewer-controls{display:flex;gap:var(--space-md);align-items:center}
.viewer-controls button,.close-viewer{border:1px solid #353539;background:#222224;border-radius:8px;min-height:32px;padding:4px 10px}
.viewer-controls button:hover:not(:disabled),.close-viewer:hover{background:#303033}
.viewer-controls button:focus-visible,.close-viewer:focus-visible{background:#303033}
.viewer-controls button:disabled{color:#77777c;cursor:default}
.viewer-controls output{min-width:72px;text-align:center;color:#b4b4b9;font-size:12px;font-variant-numeric:tabular-nums}
.viewer-image{display:block;width:100%;height:100%;object-fit:contain;padding:12px clamp(12px,3vw,32px)}
.viewer-caption{border-top:1px solid #2b2c2e}
.viewer-caption h2{font-size:16px;margin:0 0 2px}
.viewer-caption p{margin:0}
.viewer-description:empty{display:none}
@media(max-width:520px){.viewer-bar,.viewer-caption{padding:8px 12px}.viewer-image{padding:8px 0}}
"""

VIEWER = """
<dialog id="gallery-viewer" aria-labelledby="viewer-title" aria-describedby="viewer-description">
  <header class="viewer-bar">
    <div class="viewer-controls">
      <button type="button" id="viewer-prev" title="Left arrow" aria-label="Previous image">&#8592;</button>
      <output id="viewer-count" aria-label="Image position"></output>
      <button type="button" id="viewer-next" title="Right arrow" aria-label="Next image">&#8594;</button>
    </div>
    <button type="button" id="viewer-close" class="close-viewer" autofocus>Close</button>
  </header>
  <img class="viewer-image" id="viewer-image" alt="">
  <footer class="viewer-caption" aria-live="polite">
    <h2 id="viewer-title"></h2>
    <p class="viewer-description" id="viewer-description"></p>
  </footer>
</dialog>
<script>
const viewer = document.querySelector('#gallery-viewer');
const thumbnails = [...document.querySelectorAll('.thumbnail')];
const image = document.querySelector('#viewer-image');
const position = document.querySelector('#viewer-count');
const title = document.querySelector('#viewer-title');
const description = document.querySelector('#viewer-description');
const previous = document.querySelector('#viewer-prev');
const next = document.querySelector('#viewer-next');
let current = 0;
let opener;

function show(index) {
    if (index < 0 || index >= thumbnails.length) return;
    current = index;
    const thumbnail = thumbnails[index];
    const source = thumbnail.querySelector('img');
    image.src = source.src;
    image.alt = thumbnail.dataset.caption;
    title.textContent = thumbnail.dataset.caption;
    description.textContent = thumbnail.dataset.description;
    position.textContent = `${index + 1} / ${thumbnails.length}`;
    previous.disabled = index === 0;
    next.disabled = index + 1 === thumbnails.length;
}

thumbnails.forEach((thumbnail, index) => thumbnail.addEventListener('click', () => {
    opener = thumbnail;
    show(index);
    viewer.showModal();
}));
const imageIndices = new Map(thumbnails.map((button, index) => [button.id, index]));
document.querySelectorAll('[data-open-image]').forEach(button => button.addEventListener('click', () => {
    opener = button;
    show(imageIndices.get(button.dataset.openImage));
    viewer.showModal();
}));
document.querySelectorAll('[data-guide-tabs]').forEach(sample => {
    const tabs = [...sample.querySelectorAll('[role=tab]')];
    function activate(tab) {
        tabs.forEach(peer => {
            const current = peer === tab;
            peer.setAttribute('aria-selected', current);
            peer.tabIndex = current ? 0 : -1;
            document.getElementById(peer.getAttribute('aria-controls')).hidden = !current;
        });
        tab.focus();
    }
    tabs.forEach((tab, index) => {
        tab.addEventListener('click', () => activate(tab));
        tab.addEventListener('keydown', event => {
            if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
            event.preventDefault();
            activate(tabs[event.key === 'Home' ? 0 : event.key === 'End' ? tabs.length - 1 :
                (index + (event.key === 'ArrowLeft' ? -1 : 1) + tabs.length) % tabs.length]);
        });
    });
});
previous.addEventListener('click', () => show(current - 1));
next.addEventListener('click', () => show(current + 1));
document.querySelector('#viewer-close').addEventListener('click', () => viewer.close());
viewer.addEventListener('close', () => opener?.focus({preventScroll: true}));
viewer.addEventListener('keydown', event => {
    if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
        event.preventDefault();
        show(current + (event.key === 'ArrowLeft' ? -1 : 1));
    }
});
</script>
"""


def name_for(path):
    """Normalize filenames for stable links and caption keys."""
    return THUMBNAIL_NAME.sub("-", path.stem.lower()).strip("-")


def load_groups(sources):
    """Collect screenshots in the authored category and filename order."""
    groups = {}
    names = set()
    for argument in sources:
        group, spec = argument.split("=", 1)
        label, source = spec.split(":", 1)
        folder, _, pattern = source.partition("|")
        screenshots = sorted(Path(folder).glob(pattern or "*.png"))
        if not screenshots:
            raise ValueError(f"No captures match {argument}")
        items = groups.setdefault(group, [])
        for screenshot in screenshots:
            key = f"{label}-{name_for(screenshot)}"
            if key in names or not re.fullmatch(r"[a-z0-9-]+", key):
                raise ValueError(f"Invalid or duplicate screenshot name {key}")
            with screenshot.open("rb") as png:
                if png.read(8) != b"\x89PNG\r\n\x1a\n":
                    raise ValueError(f"Invalid PNG signature {screenshot}")
            names.add(key)
            items.append((key, screenshot))
    return groups


def render_specimen(specimen, captures):
    """Render a live HTML foundation example or link an exact Godot capture."""
    if specimen in TYPE_SPECIMENS:
        caption, size, weight = TYPE_SPECIMENS[specimen]
        tone = ' data-tone="muted"' if specimen == "type_secondary" else ""
        return f'<p class="text-sample"{tone} style="--sample-size:{size}px;--sample-weight:{weight}">{caption}</p>'
    if specimen == "content_tabs":
        return (
            '<div class="component-example" data-guide-tabs><nav aria-label="Example device tabs" role="tablist">'
            '<button type="button" id="guide-keyboard" role="tab" aria-selected="true" aria-controls="guide-keys">Keyboard</button>'
            '<button type="button" id="guide-gamepad" role="tab" aria-selected="false" aria-controls="guide-pad" tabindex="-1">Gamepad</button>'
            '</nav><p class="example-panel" id="guide-keys" role="tabpanel" aria-labelledby="guide-keyboard">Physical keys and mouse buttons</p>'
            '<p class="example-panel" id="guide-pad" role="tabpanel" aria-labelledby="guide-gamepad" hidden>Controller buttons and axes</p></div>'
        )
    if specimen == "atomic_controls":
        return (
            '<div class="component-example">'
            '<label><input type="checkbox" checked> Enabled</label>'
            '<label><input type="checkbox" role="switch"> Loop</label></div>'
        )
    screenshot, label = FIGURE_SPECIMENS.get(specimen, ("", ""))
    key = captures.get(screenshot)
    if not key:
        return ""
    return (
        f'<button class="guide-image" type="button" data-open-image="{key}" aria-label="View {escape(label)}">'
        f'<img src="screenshots/{key}.png" alt="" loading="lazy"><span>{escape(label)}</span></button>'
    )


def render_guide(document, spacing, captures):
    """Render the shared native design guide as annotated foundation cards."""
    parts = ['<section id="design-system">', f'<h2>{escape(document["title"])}</h2>']
    if document.get("intro"):
        parts.append(f'<p>{escape(document["intro"])}</p>')
    parts.append('<div class="guide-grid">')
    for section in document["sections"]:
        parts.append(f'<article class="guide-card"><h3>{escape(section["title"])}</h3>')
        if section.get("description"):
            parts.append(f'<p>{escape(section["description"])}</p>')
        parts.append('<dl>')
        for item in section["items"]:
            color = item.get("swatch", "")
            swatch = (f'<span class="swatch" style="background:{color}" aria-hidden="true"></span>'
                      if re.fullmatch(r"#[\da-fA-F]{6}(?:[\da-fA-F]{2})?", color) else "")
            token = item.get("token")
            value = f'{spacing[token]} px' if token else item.get("value", color)
            label = escape(item["label"])
            if token:
                label = f'<code>{label}</code>'
            note = escape(item.get("description", ""))
            details = f'<dd class="token-note">{note}</dd>' if note else ""
            measurement = f'<dd class="token-measurement">{escape(str(value))}</dd>' if value or not note else ""
            specimen = render_specimen(item.get("specimen"), captures)
            if token:
                specimen = f'<span class="spacing-mark" style="--measure:var(--space-{escape(token)})" aria-hidden="true"></span>'
            preview = f'<dd class="token-visual">{specimen}</dd>' if specimen else ""
            row_class = "token token-spacing" if token else "token"
            parts.append(f'<div class="{row_class}"><dt>{swatch}{label}</dt>'
                         f'{measurement}{details}{preview}</div>')
        parts.append('</dl></article>')
    parts.append('</div></section>')
    return "\n".join(parts)


def copy_references(reference_states, output, entries):
    """Resolve Linear measurement records to the corresponding packaged screenshot."""
    reference_index = json.loads(reference_states.read_text(encoding="utf-8-sig"))
    names = {image.name: key for key, image in entries}
    for specimen in reference_index["states"].values():
        specimen["png"] = "screenshots/" + names[specimen["png"]] + ".png"
    target = output / "linear-reference-states.json"
    target.write_text(json.dumps(reference_index, indent=2, ensure_ascii=False), encoding="utf-8")
    return target


def reference_captions(reference_index):
    """Use the recorded specimens for Linear reference captions."""
    captions = {}
    for label, specimen in reference_index["states"].items():
        names = captions.setdefault(Path(specimen["png"]).stem, [])
        names.append(label.replace("_", " "))
    return {key: "Linear reference for " + ", ".join(labels[:4]) +
            (f" and {len(labels) - 4} other measured states." if len(labels) > 4 else ".")
            for key, labels in captions.items()}


def render_comparisons(sections, screenshot_names):
    """Link each comparison to its full-size evidence in the same viewer."""
    parts = ['<section id="comparisons"><h2>Linear comparisons</h2><div class="guide-grid">']
    for comparison in sections:
        parts.append(f'<article class="guide-card"><h3>{escape(comparison["title"])}</h3>'
                     f'<p>{escape(comparison["summary"])}</p><div class="evidence">')
        for key, label in comparison["images"].items():
            if key not in screenshot_names:
                raise ValueError(f"Comparison references missing screenshot {key}")
            parts.append(f'<button type="button" data-open-image="{key}">{escape(label)}</button>')
        parts.append('</div></article>')
    parts.append('</div></section>')
    return "\n".join(parts)


def render_gallery(args):
    """Write the offline page, exact screenshot sources, and a single portable ZIP."""
    groups = load_groups(args.sources)
    captures = {name_for(source): key for items in groups.values() for key, source in items}
    guide = json.loads(args.design_style.read_text(encoding="utf-8-sig"))
    captions = json.loads(args.captions.read_text(encoding="utf-8-sig")) if args.captions else {}
    comparisons = json.loads(args.comparisons.read_text(encoding="utf-8-sig")) if args.comparisons else []
    measured = {}
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    font = output / "InterVariable.ttf"
    font_license = output / "OFL-Inter.txt"
    shutil.copyfile(EDITOR_ASSETS / "fonts" / font.name, font)
    shutil.copyfile(EDITOR_ASSETS / "fonts" / font_license.name, font_license)
    extras = [font, font_license]
    links = []
    if args.notes:
        notes = output / "review-notes.md"
        shutil.copyfile(args.notes, notes)
        extras.append(notes)
        links.append('<a href="review-notes.md">Verification notes</a>')
    if args.reference_states:
        measurements = copy_references(args.reference_states, output, groups["Linear reference captures"])
        extras.append(measurements)
        measured = reference_captions(json.loads(measurements.read_text(encoding="utf-8")))
        links.append('<a href="linear-reference-states.json">Linear measurements</a>')

    spacing = dict(re.findall(r"inline constexpr int (\w+) = (\d+);", SPACING_HEADER.read_text(encoding="utf-8")))
    tokens = ":root{" + "".join(f"--space-{name}:{value}px;" for name, value in spacing.items()) + "}"
    parts = ['<!doctype html><html lang="en"><head><meta charset="utf-8">',
             '<meta name="viewport" content="width=device-width,initial-scale=1">',
             '<title>Crimson Editor Design Review</title><style>' + tokens + STYLE + '</style></head><body><main>',
             '<header><h1>Crimson Editor Design Review</h1>',
             '<p class="intro">Open a screenshot to inspect it in the gallery. Move between images with the arrow keys. Press Escape to return to the same place.</p>']
    if args.build:
        parts.append(f'<p class="build">Staged Godot debug DLL <code>{escape(args.build)}</code></p>')
    if links:
        parts.append('<div class="links">' + "".join(links) + '</div>')
    parts.append('</header><nav aria-label="Review sections">')
    parts.append('<a href="#design-system">Design Style</a>')
    if comparisons:
        parts.append('<a href="#comparisons">Linear comparisons</a>')
    for index, (group, items) in enumerate(groups.items()):
        parts.append(f'<a href="#section-{index}">{escape(group)} <span class="muted">{len(items)}</span></a>')
    parts.append('</nav>')
    parts.append(render_guide(guide, spacing, captures))
    if comparisons:
        parts.append(render_comparisons(comparisons, {key for group in groups.values() for key, _ in group}))
    pngs = []
    for index, (group, items) in enumerate(groups.items()):
        parts.append(f'<section id="section-{index}"><h2>{escape(group)}</h2><div class="grid">')
        for key, source in items:
            default_title = re.sub(r"^\d+[a-z]?[-_]+", "", source.stem, count=1)
            default_title = re.sub(r"[-_]+", " ", default_title).strip().capitalize()
            custom = captions.get(key, {})
            title = escape(custom.get("title", default_title))
            description = escape(custom.get("description", measured.get(key, f"{group} in stock Godot 4.7.")))
            target = output / "screenshots" / f"{key}.png"
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(source, target)
            relative = target.relative_to(output).as_posix()
            pngs.append(target)
            parts.append(f'<figure><button class="thumbnail" id="{key}" type="button" data-caption="{title}" '
                         f'data-description="{description}" aria-label="View {title}">'
                         f'<img src="{relative}" alt="" loading="lazy"></button>'
                         f'<figcaption>{title}</figcaption></figure>')
        parts.append('</div></section>')
    parts.append('</main>' + VIEWER + '</body></html>')
    index_path = output / "index.html"
    index_path.write_text("\n".join(parts), encoding="utf-8")
    archive_path = output / "crimson-editor-design-review.zip"
    with ZipFile(archive_path, "w", ZIP_DEFLATED) as archive:
        for path in [index_path, *pngs, *extras]:
            archive.write(path, path.relative_to(output))
    with ZipFile(archive_path) as archive:
        if archive.testzip() is not None:
            raise ValueError("Gallery ZIP verification failed")
    print(len(pngs), "PNG captures", output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("sources", nargs="+", help="group=name:path[|pattern]")
    parser.add_argument("--design-style", type=Path, default=EDITOR_ASSETS / "design_style.json")
    parser.add_argument("--captions", type=Path, help="Optional objects keyed by packaged image name")
    parser.add_argument("--comparisons", type=Path, help="Optional comparison cards linking to the screenshot viewer")
    parser.add_argument("--notes", type=Path)
    parser.add_argument("--reference-states", type=Path)
    parser.add_argument("--build", default="", help="SHA-256 of the staged native DLL")
    render_gallery(parser.parse_args())
