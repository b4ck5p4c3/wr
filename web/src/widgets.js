const BASE_WIDGET_STYLE =
  ".wr{box-sizing:border-box;margin:0!important;font:14px/1.2 Terminus,monospace;color:#fbf9ff;background:linear-gradient(180deg,#241b3d,#151122);border:1px solid #7361a0;box-shadow:0 0 0 1px rgba(255,210,145,.16),inset 0 0 24px rgba(255,200,120,.14),0 8px 24px rgba(0,0,0,.32);text-shadow:0 0 8px rgba(255,206,128,.75);overflow:hidden}.wr *{box-sizing:border-box;margin:0!important;font:inherit!important}.wr a{display:flex;align-items:center;justify-content:center;padding:2px 0;color:#ffdc9c;text-decoration:none}.wr a:focus-visible{outline:2px solid #ffce80;outline-offset:-3px}";

const SMALL_WIDGET_STYLE =
  BASE_WIDGET_STYLE +
  ".v{display:grid;grid-template-rows:repeat(3,60px);width:42px;height:180px}.v a{padding:8px 0}.v a:first-child,.v a:last-child{padding:12px 0}.v a+a{border-top:1px solid rgba(255,210,145,.16)}.v a:nth-of-type(2){font-weight:700!important}.v b{transform:rotate(90deg)}";

const LONG_WIDGET_STYLE =
  BASE_WIDGET_STYLE +
  ".h{display:grid;grid-template-columns:minmax(72px,1fr) minmax(140px,2fr) minmax(72px,1fr);width:100%;max-width:560px;height:44px}.h a+a{border-left:1px solid rgba(255,210,145,.16)}.h a:nth-of-type(2){font-weight:700!important}";

function escapeHtmlAttribute(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll('"', "&quot;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function createWidgetShell(style, classes, root, contents) {
  return (
    '<style>@font-face{font-family:Terminus;src:url("' +
    escapeHtmlAttribute(root) +
    '/font/woff2/terminus.woff2")}' +
    style +
    '</style><nav class="' +
    classes +
    '" aria-label="b4cksp4ce webring">' +
    contents +
    "</nav>"
  );
}

export function createWidgetCode(origin, slug, variant) {
  const root = new URL(origin).origin;
  const encodedSlug = encodeURIComponent(slug);
  const widgetRoot = escapeHtmlAttribute(root);
  const sitePath = widgetRoot + "/" + escapeHtmlAttribute(encodedSlug);

  if (variant === "small") {
    return createWidgetShell(
      SMALL_WIDGET_STYLE,
      "wr v",
      root,
      '<a href="' +
        sitePath +
        '/previous">↑</a><a href="' +
        widgetRoot +
        '/"><b>bksp</b></a><a href="' +
        sitePath +
        '/next">↓</a>',
    );
  }

  if (variant === "long") {
    return createWidgetShell(
      LONG_WIDGET_STYLE,
      "wr h",
      root,
      '<a href="' +
        sitePath +
        '/previous">←---</a><a href="' +
        widgetRoot +
        '/">b4cksp4ce webring</a><a href="' +
        sitePath +
        '/next">---→</a>',
    );
  }

  throw new Error("Unknown widget variant");
}

export function getOwnedActiveSites(sites, account) {
  if (!Array.isArray(sites) || account == null) {
    return [];
  }

  return sites.filter(
    (site) =>
      site.owner_oauth === account.oauth && site.owner_tag === account.tag,
  );
}

export async function copyWidgetCode(clipboard, code) {
  if (clipboard == null || typeof clipboard.writeText !== "function") {
    throw new Error("Clipboard access is unavailable");
  }

  await clipboard.writeText(code);
}
