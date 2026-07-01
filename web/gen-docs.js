// The public API docs are generated from openapi.yaml so the page never drifts
// from the served routes. Each endpoint is rendered with its request body and
// its responses, the schemas are resolved from the components, and the raw spec
// is copied beside the page and linked from it.

import { readFileSync, writeFileSync, copyFileSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { parse } from "yaml";

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

const doc = parse(readFileSync(specPath, "utf8"));

function escapeHtml(text) {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function refName(ref) {
  if (typeof ref !== "string") return null;
  const match = ref.match(/#\/components\/\w+\/(\w+)/);
  return match ? match[1] : null;
}

// The name of the first $ref found in document order under a node, or null when
// the node carries none. A null kind matches any $ref, a named kind matches only
// a ref whose target lives under that component bucket.
function firstRef(node, kind) {
  if (node == null || typeof node !== "object") return null;
  if (Array.isArray(node)) {
    for (const item of node) {
      const found = firstRef(item, kind);
      if (found != null) return found;
    }
    return null;
  }
  for (const [key, value] of Object.entries(node)) {
    if (key === "$ref" && typeof value === "string") {
      if (kind == null || value.includes("/" + kind + "/")) return refName(value);
    }
    const found = firstRef(value, kind);
    if (found != null) return found;
  }
  return null;
}

// Whether a type of the given value appears anywhere under a node, so an array
// response body is recognised through its nested items.
function hasType(node, type) {
  if (node == null || typeof node !== "object") return false;
  if (Array.isArray(node)) return node.some((item) => hasType(item, type));
  if (node.type === type) return true;
  return Object.values(node).some((value) => hasType(value, type));
}

function parseSchemas() {
  const schemas = {};
  const node = doc.components?.schemas;
  if (node == null) return schemas;
  for (const [name, schema] of Object.entries(node)) {
    const fields = [];
    const props = schema.properties ?? {};
    for (const [fieldName, prop] of Object.entries(props)) {
      const type =
        typeof prop.type === "string" && prop.type
          ? prop.type
          : (firstRef(prop, null) ?? "");
      fields.push({ name: fieldName, type });
    }
    const required = Array.isArray(schema.required) ? schema.required : [];
    schemas[name] = { fields, required };
  }
  return schemas;
}

// A shared response carries its description and the schema its body resolves
// to. A status that references it renders the same body shape as an inline
// schema.
function parseResponseComponents() {
  const out = {};
  const node = doc.components?.responses;
  if (node == null) return out;
  for (const [name, resp] of Object.entries(node)) {
    const description =
      typeof resp.description === "string" ? resp.description : name;
    out[name] = { description, schema: firstRef(resp, "schemas") };
  }
  return out;
}

// A status block resolves to a label, a referenced response description, a
// referenced schema name, or the inline description.
function resolveStatus(block, responseComponents) {
  const responseRef = firstRef(block, "responses");
  if (responseRef != null) {
    const component = responseComponents[responseRef];
    return {
      label: component ? component.description : responseRef,
      schema: component ? component.schema : null,
    };
  }

  const schemaRef = firstRef(block, "schemas");
  if (schemaRef != null) {
    return {
      label: (hasType(block, "array") ? "array of " : "") + schemaRef,
      schema: schemaRef,
    };
  }

  return {
    label: typeof block.description === "string" ? block.description : "",
    schema: null,
  };
}

function parseOperation(operation, responseComponents) {
  const op = { auth: false, request: null, params: [], responses: [] };

  op.tag = Array.isArray(operation.tags) ? operation.tags[0] || "" : "";
  op.summary = typeof operation.summary === "string" ? operation.summary : "";

  if (Array.isArray(operation.security))
    op.auth = operation.security.some(
      (entry) => entry != null && "sessionCookie" in entry,
    );

  if (operation.requestBody != null)
    op.request = firstRef(operation.requestBody, null);

  if (Array.isArray(operation.parameters)) {
    for (const param of operation.parameters) {
      if (param == null) continue;
      if (typeof param.$ref === "string") {
        const name = refName(param.$ref);
        if (name != null) op.params.push(name.toLowerCase());
      } else if (typeof param.name === "string") {
        op.params.push(param.name);
      }
    }
  }

  if (operation.responses != null) {
    for (const [status, block] of Object.entries(operation.responses)) {
      op.responses.push({
        status: String(status),
        ...resolveStatus(block ?? {}, responseComponents),
      });
    }
  }

  return op;
}

function parseSpec() {
  const responseComponents = parseResponseComponents();
  const schemas = parseSchemas();

  const title = doc.info?.title ?? "api";
  const version = doc.info?.version != null ? String(doc.info.version) : "";
  const tagOrder = Array.isArray(doc.tags)
    ? doc.tags.map((tag) => tag?.name).filter((name) => typeof name === "string")
    : [];

  const methods = ["get", "post", "put", "delete", "patch"];
  const endpoints = [];
  const paths = doc.paths ?? {};
  for (const [path, pathItem] of Object.entries(paths)) {
    if (!path.startsWith("/") || pathItem == null) continue;
    for (const [method, operation] of Object.entries(pathItem)) {
      if (!methods.includes(method) || operation == null) continue;
      endpoints.push({
        method,
        path,
        ...parseOperation(operation, responseComponents),
      });
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
  if (schema == null || schema.fields.length === 0) {
    return "";
  }
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
    '<div class="block"><span class="label">JSON responses</span><ul>' +
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
    (endpoint.auth ? '<span class="auth">with session cookie</span>' : "") +
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
    <meta name="description" content="${heading}, the public webring API." />
    <meta property="og:title" content="${heading}" />
    <meta property="og:description" content="${heading}, the public webring API." />
    <meta property="og:image" content="/favicon-512x512.png" />
    <meta property="og:type" content="website" />
    <meta name="twitter:card" content="summary" />
    <meta name="twitter:title" content="${heading}" />
    <meta name="twitter:description" content="${heading}, the public webring API." />
    <meta name="twitter:image" content="/favicon-512x512.png" />
    <link rel="icon" href="/favicon.ico" sizes="any" />
    <link rel="icon" type="image/png" sizes="16x16" href="/favicon-16x16.png" />
    <link rel="icon" type="image/png" sizes="192x192" href="/favicon-192x192.png" />
    <link rel="icon" type="image/png" sizes="512x512" href="/favicon-512x512.png" />
    <link rel="apple-touch-icon" href="/apple-touch-icon.png" />
    <title>${heading}</title>
    <link rel="stylesheet" href="/assets/app.css" />
    <style>
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
        background: var(--bg-primary);
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
        color: var(--text-primary);
      }
      .summary {
        color: var(--footer-text);
      }
      .method {
        color: var(--bg-primary);
        background: var(--accent);
        padding: 0 1ch;
        font-weight: 700;
        min-width: 7ch;
        text-align: center;
      }
      .method-post,
      .method-put,
      .method-patch {
        background: var(--up);
      }
      .method-delete {
        background: var(--down);
      }
      .auth {
        margin-left: auto;
        color: var(--footer-text);
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
        color: var(--footer-text);
      }
      .req {
        color: var(--accent);
      }
      .note {
        color: var(--footer-text);
      }
    </style>
  </head>
  <body>
    <main>
      <h1>${heading}</h1>
      <p class="note">
        Generated from <a href="/openapi.yaml">openapi.yaml</a>. Return to the
        <a class="nav-link" href="/">ring</a>.
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
