// The JSON API client. Every call is same-origin, the session cookie is sent
// automatically.

// A failed fetch is tagged as a transport problem. The caller distinguishes it
// from a server that answered with an error status.
function networkError() {
  const error = new Error("Unable to reach the server");
  error.isNetworkError = true;
  return error;
}

async function request(path, options) {
  let response;
  try {
    response = await fetch(path, options);
  } catch (_) {
    throw networkError();
  }
  const data = await response.json().catch(() => ({}));
  if (!response.ok)
    throw new Error(data.message || "request failed with " + response.status);
  return data;
}

const getJson = (path) => request(path, { credentials: "same-origin" });

const sendJson = (method, path, body) =>
  request(path, {
    method,
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });

const postJson = (path, body) => sendJson("POST", path, body);
const deleteJson = (path, body) => sendJson("DELETE", path, body);

export const api = {
  config: () => getJson("/api/v1/config"),
  listSites: () => getJson("/sites"),
  me: () => getJson("/api/v1/me"),
  logout: () => postJson("/auth/logout", {}),
  addSite: (site) => postJson("/api/v1/sites/add", site),
  renameSite: (slug, name, url, description) =>
    postJson("/api/v1/sites/rename", { slug, name, url, description }),
  react: (slug, emoji) => postJson("/api/v1/sites/react", { slug, emoji }),
  recordClick: (slug) => postJson("/api/v1/sites/click", { slug }),
  listComments: (offset = 0, limit = 5) =>
    getJson(
      `/api/v1/comments?offset=${encodeURIComponent(offset)}&limit=${encodeURIComponent(limit)}`,
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
