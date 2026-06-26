// The JSON API client. Every call is same-origin, so the session cookie rides
// along on its own.

// A failed fetch is a transport problem, so it is tagged for the caller to
// distinguish it from a server that answered with an error status.
function networkError() {
  const error = new Error("Unable to reach the server");
  error.isNetworkError = true;
  return error;
}

async function getJson(path) {
  let response;
  try {
    response = await fetch(path, { credentials: "same-origin" });
  } catch (_) {
    throw networkError();
  }
  const data = await response.json().catch(() => ({}));
  if (!response.ok)
    throw new Error(data.message || "request failed with " + response.status);
  return data;
}

async function postJson(path, body) {
  let response;
  try {
    response = await fetch(path, {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
  } catch (_) {
    throw networkError();
  }
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.message || "request failed");
  return data;
}

export const api = {
  config: () => getJson("/api/config"),
  listSites: () => getJson("/sites"),
  me: () => getJson("/api/me"),
  logout: () => postJson("/auth/logout", {}),
  addSite: (site) => postJson("/api/sites/add", site),
  renameSite: (slug, name) => postJson("/api/sites/rename", { slug, name }),
  react: (slug, emoji) => postJson("/api/sites/react", { slug, emoji }),
  adminEditSite: (site) => postJson("/api/admin/site", site),
  adminAddSite: (site) => postJson("/api/admin/site/add", site),
  adminDeleteSite: (slug) => postJson("/api/admin/site/delete", { slug }),
  adminPending: () => getJson("/api/admin/pending"),
  adminApprove: (id) => postJson("/api/admin/pending/approve", { id }),
  adminReject: (id) => postJson("/api/admin/pending/reject", { id }),
  adminLogs: () => getJson("/api/admin/logs"),
  adminAudit: () => getJson("/api/admin/audit"),
};
