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

async function sendJson(method, path, body) {
  let response;
  try {
    response = await fetch(path, {
      method,
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

const postJson = (path, body) => sendJson("POST", path, body);
const deleteJson = (path, body) => sendJson("DELETE", path, body);

export const api = {
  config: () => getJson("/api/v1/config"),
  listSites: () => getJson("/sites"),
  me: () => getJson("/api/v1/me"),
  logout: () => postJson("/auth/logout", {}),
  addSite: (site) => postJson("/api/v1/sites/add", site),
  renameSite: (slug, name) => postJson("/api/v1/sites/rename", { slug, name }),
  react: (slug, emoji) => postJson("/api/v1/sites/react", { slug, emoji }),
  recordClick: (slug) => postJson("/api/v1/sites/click", { slug }),
  listComments: (offset = 0, limit = 5) =>
    getJson(
      "/api/v1/comments?offset=" +
        encodeURIComponent(offset) +
        "&limit=" +
        encodeURIComponent(limit),
    ),
  postComment: (body) => postJson("/api/v1/comments/add", { body }),
  adminEditSite: (site) => postJson("/api/v1/admin/site", site),
  adminAddSite: (site) => postJson("/api/v1/admin/site/add", site),
  adminDeleteSite: (slug) => deleteJson("/api/v1/admin/site/delete", { slug }),
  adminPending: () => getJson("/api/v1/admin/pending"),
  adminApprove: (id) => postJson("/api/v1/admin/pending/approve", { id }),
  adminReject: (id) => deleteJson("/api/v1/admin/pending/reject", { id }),
  adminLogs: () => getJson("/api/v1/admin/logs"),
  adminAudit: () => getJson("/api/v1/admin/audit"),
  adminStats: () => getJson("/api/v1/admin/stats"),
  adminPendingComments: () => getJson("/api/v1/admin/comments"),
  adminApproveComment: (id) =>
    postJson("/api/v1/admin/comments/approve", { id }),
  adminDeleteComment: (id) =>
    deleteJson("/api/v1/admin/comments/delete", { id }),
  adminClearCache: () => postJson("/api/v1/admin/cache/clear", {}),
};
