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
  config: () => getJson("/api/v1/config"),
  listSites: () => getJson("/sites"),
  me: () => getJson("/api/v1/me"),
  logout: () => postJson("/auth/logout", {}),
  addSite: (site) => postJson("/api/v1/sites/add", site),
  renameSite: (slug, name) => postJson("/api/v1/sites/rename", { slug, name }),
  react: (slug, emoji) => postJson("/api/v1/sites/react", { slug, emoji }),
  listComments: () => getJson("/api/v1/comments"),
  postComment: (body) => postJson("/api/v1/comments/add", { body }),
  adminEditSite: (site) => postJson("/api/v1/admin/site", site),
  adminAddSite: (site) => postJson("/api/v1/admin/site/add", site),
  adminDeleteSite: (slug) => postJson("/api/v1/admin/site/delete", { slug }),
  adminPending: () => getJson("/api/v1/admin/pending"),
  adminApprove: (id) => postJson("/api/v1/admin/pending/approve", { id }),
  adminReject: (id) => postJson("/api/v1/admin/pending/reject", { id }),
  adminLogs: () => getJson("/api/v1/admin/logs"),
  adminAudit: () => getJson("/api/v1/admin/audit"),
};
