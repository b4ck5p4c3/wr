// The public API docs are generated from openapi.yaml so the page never drifts
// from the served routes. Each endpoint is rendered with its request body and
// its responses, the schemas are resolved from the components, and the raw spec
// is copied beside the page and linked from it.

import { readFileSync, writeFileSync, copyFileSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const specPath = join(here, "..", "openapi.yaml");
const outPath = join(here, "public", "docs.html");
const rawCopyPath = join(here, "public", "openapi.yaml");
const fontSrcDir = join(here, "static", "font", "woff2");
const fontOutDir = join(here, "public", "font", "woff2");
const fontFiles = [
  "terminus.woff2",
  "terminus-bold.woff2",
  "terminus-italic.woff2",
  "terminus-bold-italic.woff2",
];

const lines = readFileSync(specPath, "utf8").split("\n");

const indentOf = (line) => line.match(/^ */)[0].length;
const isBlank = (line) => line.trim() === "";

function escapeHtml(text) {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function refName(value) {
  const match = value.match(/#\/components\/\w+\/(\w+)/);
  return match ? match[1] : null;
}

// The child range of a key line is the half-open span of the lines indented
// deeper than it, up to the next sibling or shallower line.
function childRange(startIndex) {
  const base = indentOf(lines[startIndex]);
  let end = startIndex + 1;
  while (
    end < lines.length &&
    (isBlank(lines[end]) || indentOf(lines[end]) > base)
  )
    end++;
  return [startIndex + 1, end];
}

function topLevelIndex(key) {
  return lines.findIndex((line) => line.startsWith(key));
}

// A mapping key at a fixed indent inside a span, matched by its leading name.
function keyIndexAt(from, to, indent, name) {
  for (let i = from; i < to; i++) {
    if (isBlank(lines[i])) continue;
    if (indentOf(lines[i]) === indent && lines[i].trim().startsWith(name + ":"))
      return i;
  }
  return -1;
}

function scalarAfter(line) {
  const value = line.slice(line.indexOf(":") + 1).trim();
  return value.replace(/^["']|["']$/g, "");
}

function flowList(line) {
  const match = line.match(/\[([^\]]*)\]/);
  if (match == null) return [];
  return match[1]
    .split(",")
    .map((part) => part.trim())
    .filter((part) => part.length > 0);
}

function parseSchemas() {
  const schemasIndex = lines.findIndex((line) => line === "  schemas:");
  const schemas = {};
  if (schemasIndex < 0) return schemas;
  const [from, to] = childRange(schemasIndex);
  for (let i = from; i < to; i++) {
    if (isBlank(lines[i]) || indentOf(lines[i]) !== 4) continue;
    const name = lines[i].trim().replace(/:$/, "");
    const [sFrom, sTo] = childRange(i);
    const fields = [];
    const propsIndex = keyIndexAt(sFrom, sTo, 6, "properties");
    if (propsIndex >= 0) {
      const [pFrom, pTo] = childRange(propsIndex);
      for (let p = pFrom; p < pTo; p++) {
        if (isBlank(lines[p]) || indentOf(lines[p]) !== 8) continue;
        const fieldName = lines[p].trim().replace(/:$/, "");
        const [fFrom, fTo] = childRange(p);
        const typeIndex = keyIndexAt(fFrom, fTo, 10, "type");
        fields.push({
          name: fieldName,
          type: typeIndex >= 0 ? scalarAfter(lines[typeIndex]) : "",
        });
      }
    }
    const requiredIndex = keyIndexAt(sFrom, sTo, 6, "required");
    const required = requiredIndex >= 0 ? flowList(lines[requiredIndex]) : [];
    schemas[name] = { fields, required };
  }
  return schemas;
}

function parseResponseComponents() {
  const out = {};
  const responsesIndex = lines.findIndex((line) => line === "  responses:");
  if (responsesIndex < 0) return out;
  const [from, to] = childRange(responsesIndex);
  for (let i = from; i < to; i++) {
    if (isBlank(lines[i]) || indentOf(lines[i]) !== 4) continue;
    const name = lines[i].trim().replace(/:$/, "");
    const [rFrom, rTo] = childRange(i);
    const descIndex = keyIndexAt(rFrom, rTo, 6, "description");
    out[name] = descIndex >= 0 ? scalarAfter(lines[descIndex]) : name;
  }
  return out;
}

// A status block resolves to a label, a referenced response description, a
// referenced schema name, or the inline description.
function resolveStatus(from, to, responseComponents) {
  let isArray = false;
  for (let i = from; i < to; i++) {
    if (lines[i].includes("type: array")) isArray = true;
    const refMatch = lines[i].match(/\$ref:\s*"([^"]+)"/);
    if (refMatch) {
      const name = refName(refMatch[1]);
      if (refMatch[1].includes("/responses/"))
        return { label: responseComponents[name] || name, schema: null };
      if (refMatch[1].includes("/schemas/"))
        return { label: (isArray ? "array of " : "") + name, schema: name };
    }
  }
  const descIndex = keyIndexAt(from, to, 10, "description");
  if (descIndex >= 0)
    return { label: scalarAfter(lines[descIndex]), schema: null };
  return { label: "", schema: null };
}

function parseOperation(from, to, responseComponents) {
  const op = { auth: false, request: null, params: [], responses: [] };

  const tagsIndex = keyIndexAt(from, to, 6, "tags");
  op.tag = tagsIndex >= 0 ? flowList(lines[tagsIndex])[0] || "" : "";

  const summaryIndex = keyIndexAt(from, to, 6, "summary");
  op.summary = summaryIndex >= 0 ? scalarAfter(lines[summaryIndex]) : "";

  const securityIndex = keyIndexAt(from, to, 6, "security");
  if (securityIndex >= 0) {
    const [secFrom, secTo] = childRange(securityIndex);
    for (let i = secFrom; i < secTo; i++)
      if (lines[i].includes("sessionCookie")) op.auth = true;
  }

  const requestIndex = keyIndexAt(from, to, 6, "requestBody");
  if (requestIndex >= 0) {
    const [reqFrom, reqTo] = childRange(requestIndex);
    for (let i = reqFrom; i < reqTo; i++) {
      const refMatch = lines[i].match(/\$ref:\s*"([^"]+)"/);
      if (refMatch) op.request = refName(refMatch[1]);
    }
  }

  const paramsIndex = keyIndexAt(from, to, 6, "parameters");
  if (paramsIndex >= 0) {
    const [pFrom, pTo] = childRange(paramsIndex);
    for (let i = pFrom; i < pTo; i++) {
      const refMatch = lines[i].match(/parameters\/(\w+)/);
      if (refMatch) op.params.push(refMatch[1].toLowerCase());
      const nameMatch = lines[i].match(/^\s*- name:\s*(\w+)/);
      if (nameMatch) op.params.push(nameMatch[1]);
    }
  }

  const responsesIndex = keyIndexAt(from, to, 6, "responses");
  if (responsesIndex >= 0) {
    const [rFrom, rTo] = childRange(responsesIndex);
    for (let i = rFrom; i < rTo; i++) {
      if (isBlank(lines[i]) || indentOf(lines[i]) !== 8) continue;
      const status = lines[i].trim().replace(/:$/, "").replace(/["']/g, "");
      const [sFrom, sTo] = childRange(i);
      op.responses.push({
        status,
        ...resolveStatus(sFrom, sTo, responseComponents),
      });
    }
  }

  return op;
}

function parseSpec() {
  let title = "api";
  let version = "";
  const tagOrder = [];

  const infoIndex = topLevelIndex("info:");
  if (infoIndex >= 0) {
    const [from, to] = childRange(infoIndex);
    const titleIndex = keyIndexAt(from, to, 2, "title");
    if (titleIndex >= 0) title = scalarAfter(lines[titleIndex]);
    const versionIndex = keyIndexAt(from, to, 2, "version");
    if (versionIndex >= 0) version = scalarAfter(lines[versionIndex]);
  }

  const tagsIndex = lines.findIndex((line) => line === "tags:");
  if (tagsIndex >= 0) {
    const [from, to] = childRange(tagsIndex);
    for (let i = from; i < to; i++) {
      const match = lines[i].match(/^\s*- name:\s*(.+?)\s*$/);
      if (match) tagOrder.push(match[1]);
    }
  }

  const responseComponents = parseResponseComponents();
  const schemas = parseSchemas();

  const endpoints = [];
  const pathsIndex = topLevelIndex("paths:");
  if (pathsIndex >= 0) {
    const [from, to] = childRange(pathsIndex);
    for (let i = from; i < to; i++) {
      const pathMatch = lines[i].match(/^  (\/\S*):\s*$/);
      if (!pathMatch) continue;
      const path = pathMatch[1];
      const [pFrom, pTo] = childRange(i);
      for (let m = pFrom; m < pTo; m++) {
        const methodMatch = lines[m].match(/^    (get|post|put|delete|patch):\s*$/);
        if (!methodMatch) continue;
        const [oFrom, oTo] = childRange(m);
        const op = parseOperation(oFrom, oTo, responseComponents);
        endpoints.push({ method: methodMatch[1], path, ...op });
      }
    }
  }

  return { title, version, tagOrder, endpoints, schemas };
}

// A schema is rendered as a list of its fields, each with its type and an
// optional required marker. The marker is shown for a request input only.
function renderFields(schema, showRequired) {
  return schema.fields
    .map((field) => {
      const isRequired = showRequired && schema.required.includes(field.name);
      return (
        "<li><code>" +
        escapeHtml(field.name) +
        "</code> " +
        escapeHtml(field.type) +
        (isRequired ? ' <span class="req">required</span>' : "") +
        "</li>"
      );
    })
    .join("");
}

function renderRequest(op, schemas) {
  if (op.request == null) return "";
  const schema = schemas[op.request];
  if (schema == null || schema.fields.length === 0) return "";
  return (
    '<div class="block"><span class="label">' +
    escapeHtml(op.method.toUpperCase()) +
    " json object body</span>" +
    "<ul>" +
    renderFields(schema, true) +
    "</ul></div>"
  );
}

function renderParams(op) {
  if (op.params.length === 0) return "";
  const items = op.params
    .map((name) => "<li><code>" + escapeHtml(name) + "</code></li>")
    .join("");
  return (
    '<div class="block"><span class="label">parameters</span><ul>' +
    items +
    "</ul></div>"
  );
}

function renderResponses(op, schemas) {
  if (op.responses.length === 0) return "";
  const rows = op.responses
    .map((response) => {
      const schema = response.schema ? schemas[response.schema] : null;
      const fields =
        schema != null && schema.fields.length > 0
          ? "<ul>" + renderFields(schema, false) + "</ul>"
          : "";
      return (
        "<li><code>" +
        escapeHtml(response.status) +
        "</code> " +
        escapeHtml(response.label) +
        fields +
        "</li>"
      );
    })
    .join("");
  return (
    '<div class="block"><span class="label">responses</span><ul>' +
    rows +
    "</ul></div>"
  );
}

function renderEndpoint(endpoint, schemas) {
  const detail =
    renderParams(endpoint) +
    renderRequest(endpoint, schemas) +
    renderResponses(endpoint, schemas);
  return (
    '<div class="endpoint">\n          <div class="head">' +
    '<span class="method method-' +
    endpoint.method +
    '">' +
    endpoint.method.toUpperCase() +
    "</span>" +
    "<code>" +
    escapeHtml(endpoint.path) +
    "</code>" +
    '<span class="summary">' +
    escapeHtml(endpoint.summary) +
    "</span>" +
    (endpoint.auth ? '<span class="auth">session</span>' : "") +
    "</div>" +
    (detail ? '<div class="detail">' + detail + "</div>" : "") +
    "</div>"
  );
}

function renderGroup(tag, endpoints, schemas) {
  const rows = endpoints
    .map((endpoint) => renderEndpoint(endpoint, schemas))
    .join("\n        ");
  return (
    "<section>\n        <h2>" +
    escapeHtml(tag) +
    "</h2>\n        " +
    rows +
    "\n      </section>"
  );
}

function render(spec) {
  const groups = [];
  const seen = new Set();
  for (const tag of spec.tagOrder) {
    const matched = spec.endpoints.filter((endpoint) => endpoint.tag === tag);
    if (matched.length === 0) continue;
    groups.push(renderGroup(tag, matched, spec.schemas));
    seen.add(tag);
  }
  const untagged = spec.endpoints.filter((endpoint) => !seen.has(endpoint.tag));
  if (untagged.length > 0)
    groups.push(renderGroup("other", untagged, spec.schemas));

  const heading = "wr API documentation";

  return `<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>${heading}</title>
    <style>
      @font-face {
        font-family: "Terminus";
        font-style: normal;
        font-weight: 400;
        src: url("/font/woff2/terminus.woff2") format("woff2");
      }
      @font-face {
        font-family: "Terminus";
        font-style: normal;
        font-weight: 700;
        src: url("/font/woff2/terminus-bold.woff2") format("woff2");
      }
      @font-face {
        font-family: "Terminus";
        font-style: italic;
        font-weight: 400;
        src: url("/font/woff2/terminus-italic.woff2") format("woff2");
      }
      @font-face {
        font-family: "Terminus";
        font-style: italic;
        font-weight: 700;
        src: url("/font/woff2/terminus-bold-italic.woff2") format("woff2");
      }
      :root {
        --bg: #241a3a;
        --panel: #1e1832;
        --text: #f7f5ff;
        --accent: #ffd089;
        --border: #5a4a7a;
        --muted: #b9a8d0;
        --post: #82e882;
      }
      * {
        box-sizing: border-box;
        font-family: "Terminus", monospace;
      }
      body {
        margin: 0;
        background: var(--bg);
        color: var(--text);
        line-height: 1.4;
      }
      main {
        max-width: 820px;
        margin: 0 auto;
        padding: 4ch 2ch 8ch;
      }
      h1 {
        color: var(--accent);
      }
      h2 {
        color: var(--accent);
        margin: 0 0 1.5ch;
        text-transform: lowercase;
      }
      a {
        color: var(--accent);
      }
      section {
        border: 1px solid var(--border);
        background: var(--panel);
        padding: 2ch;
        margin: 2ch 0;
      }
      .endpoint {
        padding: 1ch 0;
        border-top: 1px solid var(--border);
      }
      .endpoint:first-of-type {
        border-top: none;
      }
      .head {
        display: flex;
        flex-wrap: wrap;
        align-items: baseline;
        gap: 1.2ch;
      }
      code {
        color: var(--text);
      }
      .summary {
        color: var(--muted);
      }
      .method {
        color: var(--bg);
        background: var(--accent);
        padding: 0 1ch;
        font-weight: 700;
        min-width: 7ch;
        text-align: center;
      }
      .method-post,
      .method-put,
      .method-delete,
      .method-patch {
        background: var(--post);
      }
      .auth {
        margin-left: auto;
        color: var(--muted);
        border: 1px solid var(--border);
        padding: 0 0.8ch;
      }
      .detail {
        margin: 1ch 0 0 7ch;
        display: flex;
        flex-wrap: wrap;
        gap: 3ch;
      }
      .label {
        color: var(--accent);
        display: block;
        margin-bottom: 0.4ch;
      }
      .detail ul {
        margin: 0;
        padding-left: 2ch;
        color: var(--muted);
      }
      .req {
        color: var(--accent);
      }
      .note {
        color: var(--muted);
      }
    </style>
  </head>
  <body>
    <main>
      <h1>${heading}</h1>
      <p class="note">
        Generated from <a href="/openapi.yaml">openapi.yaml</a>. Return to the
        <a href="/">ring</a>.
      </p>
      ${groups.join("\n      ")}
    </main>
  </body>
</html>
`;
}

const spec = parseSpec();
writeFileSync(outPath, render(spec));
copyFileSync(specPath, rawCopyPath);
mkdirSync(fontOutDir, { recursive: true });
for (const file of fontFiles)
  copyFileSync(join(fontSrcDir, file), join(fontOutDir, file));
process.stdout.write(
  "docs.html generated from openapi.yaml, " +
    spec.endpoints.length +
    " endpoints\n",
);
