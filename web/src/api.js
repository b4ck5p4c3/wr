// The JSON API client. Every call is same-origin, so the session cookie rides
// along on its own.

async function getJson(path) {
  const response = await fetch(path, { credentials: "same-origin" });
  const data = await response.json().catch(() => ({}));
  if (!response.ok)
    throw new Error(data.message || "request failed with " + response.status);
  return data;
}

async function postJson(path, body) {
  const response = await fetch(path, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.message || "request failed");
  return data;
}

export const api = {
  listSites: () => getJson("/sites"),
  me: () => getJson("/api/me"),
  addSite: (site) => postJson("/api/sites/add", site),
  renameSite: (slug, name) => postJson("/api/sites/rename", { slug, name }),
  adminEditSite: (site) => postJson("/api/admin/site", site),
  adminPending: () => getJson("/api/admin/pending"),
  adminApprove: (id) => postJson("/api/admin/pending/approve", { id }),
  adminReject: (id) => postJson("/api/admin/pending/reject", { id }),
};
