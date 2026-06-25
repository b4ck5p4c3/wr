import { useEffect, useState } from "preact/hooks";
import { api } from "./api.js";

// The server falls every unknown path back to this shell, so the router reads
// the path and pushes history without a page reload.
function useRoute() {
  const [path, setPath] = useState(location.pathname);
  useEffect(() => {
    const onPop = () => setPath(location.pathname);
    addEventListener("popstate", onPop);
    return () => removeEventListener("popstate", onPop);
  }, []);
  const navigate = (to) => {
    history.pushState(null, "", to);
    setPath(to);
  };
  return [path, navigate];
}

function Header({ navigate, me, onLogin, onLogout }) {
  return (
    <header class="bar">
      <a class="logo" href="/" onClick={link(navigate, "/")}>
        wr
      </a>
      <nav>
        <a href="/" onClick={link(navigate, "/")}>
          ring
        </a>
        <a href="/about" onClick={link(navigate, "/about")}>
          about
        </a>
        {me ? (
          <button class="secondary" onClick={onLogout}>
            logout
          </button>
        ) : (
          <button class="primary" onClick={onLogin}>
            login
          </button>
        )}
      </nav>
    </header>
  );
}

function link(navigate, to) {
  return (event) => {
    event.preventDefault();
    navigate(to);
  };
}

function LoginModal({ onClose, config }) {
  const telegramBot = config.telegram_bot;
  return (
    <div class="modal-backdrop" onClick={onClose}>
      <div class="modal" onClick={(e) => e.stopPropagation()}>
        <h2>sign in</h2>
        <p>Pick a provider to manage your sites.</p>
        {config.github ? (
          <a class="provider github" href="/auth/github">
            Continue with GitHub
          </a>
        ) : null}
        {telegramBot ? (
          <a
            class="provider telegram"
            href={
              "https://oauth.telegram.org/auth?bot_id=" +
              telegramBot +
              "&origin=" +
              encodeURIComponent(location.origin) +
              "&return_to=" +
              encodeURIComponent(location.origin + "/auth/telegram/callback")
            }
          >
            Continue with Telegram
          </a>
        ) : null}
        {config.is_dev ? (
          <select
            class="provider"
            onChange={(e) => {
              if (e.target.value) location.href = e.target.value;
            }}
          >
            <option value="">bypass login</option>
            <option value="/auth/dev?role=admin">log in as admin</option>
            <option value="/auth/dev?role=user">log in as user</option>
          </select>
        ) : null}
        <button class="close" onClick={onClose}>
          close
        </button>
      </div>
    </div>
  );
}

function SiteCard({ site }) {
  return (
    <li class="site">
      {site.favicon ? (
        <img src={site.favicon} alt="" width="16" height="16" />
      ) : null}
      <a href={site.url} target="_blank" rel="noopener noreferrer">
        {site.name}
      </a>
      <span class="slug">/{site.slug}</span>
    </li>
  );
}

function Landing({ navigate, me, reload }) {
  const [sites, setSites] = useState(null);
  const [error, setError] = useState(null);
  useEffect(() => {
    api
      .listSites()
      .then(setSites)
      .catch((e) => setError(e.message + ". Check your internet connection."));
  }, []);

  return (
    <main>
      <h1>the b4cksp4ce webring</h1>
      {error ? (
        <p class="error">{error}</p>
      ) : sites === null ? (
        <p>Loading...</p>
      ) : sites.length === 0 ? (
        <p>No sites are in the ring yet.</p>
      ) : (
        <ul class="sites">
          {sites.map((site) => (
            <SiteCard key={site.slug} site={site} />
          ))}
        </ul>
      )}

      {me && me.is_admin ? (
        <section class="panel">
          <h2>admin panel</h2>
          <p>Signed in as {me.display_name}.</p>
          <p>
            <a href="/admin" onClick={link(navigate, "/admin")}>
              open admin panel
            </a>
          </p>
        </section>
      ) : me ? (
        <section class="panel">
          <h2>your panel</h2>
          <p>Signed in as {me.display_name}.</p>
          {me.sites.length === 0 ? (
            <p>You have no sites yet.</p>
          ) : (
            <ul class="sites">
              {me.sites.map((site) => (
                <OwnedSite key={site.slug} site={site} onRenamed={reload} />
              ))}
            </ul>
          )}
          {me.pending && me.pending.length > 0 ? (
            <>
              <h3>awaiting review</h3>
              <ul class="pendings">
                {me.pending.map((action) => (
                  <li class="pending" key={action.id}>
                    <span class="kind">{action.kind}</span>
                    <span class="slug">/{action.target_slug}</span>
                    <code>{action.payload}</code>
                  </li>
                ))}
              </ul>
            </>
          ) : null}
          <AddSiteForm onAdded={reload} />
        </section>
      ) : null}
    </main>
  );
}

function About() {
  return (
    <main>
      <h1>about</h1>
      <p>wr is a webring by b4cksp4ce.</p>
      <p>
        <a
          href="https://t.me/b4cksp4ce_issues/762"
          target="_blank"
          rel="noopener noreferrer"
        >
          See original issue
        </a>
      </p>
      <p>
        <a href="https://0x08.in" target="_blank" rel="noopener noreferrer">
          Learn about b4cksp4ce
        </a>
      </p>
    </main>
  );
}

function AddSiteForm({ onAdded, submitLabel = "Submit for review", onSubmit }) {
  const [form, setForm] = useState({
    slug: "",
    name: "",
    url: "",
    favicon: "",
  });
  const [message, setMessage] = useState(null);
  const field = (name) => (event) =>
    setForm({ ...form, [name]: event.target.value });

  const submit = async (event) => {
    event.preventDefault();
    try {
      const result = onSubmit ? await onSubmit(form) : await api.addSite(form);
      setMessage(result.message);
      setForm({ slug: "", name: "", url: "", favicon: "" });
      if (onAdded) onAdded();
    } catch (e) {
      setMessage(e.message);
    }
  };

  return (
    <form class="card" onSubmit={submit}>
      <h3>add a site</h3>
      <input placeholder="slug" value={form.slug} onInput={field("slug")} />
      <input placeholder="name" value={form.name} onInput={field("name")} />
      <input
        placeholder="https://your.site"
        value={form.url}
        onInput={field("url")}
      />
      <input
        placeholder="favicon url (optional)"
        value={form.favicon}
        onInput={field("favicon")}
      />
      <button class="primary" type="submit">
        {submitLabel}
      </button>
      {message ? <p class="hint">{message}</p> : null}
    </form>
  );
}

function OwnedSite({ site, onRenamed }) {
  const [name, setName] = useState(site.name);
  const [message, setMessage] = useState(null);
  const rename = async () => {
    try {
      const result = await api.renameSite(site.slug, name);
      setMessage(result.message);
      if (onRenamed) onRenamed();
    } catch (e) {
      setMessage(e.message);
    }
  };
  return (
    <li class="site">
      <span class="slug">/{site.slug}</span>
      <input value={name} onInput={(e) => setName(e.target.value)} />
      <button onClick={rename}>rename</button>
      <span class={site.is_reachable ? "up" : "down"}>
        {site.is_reachable ? "up" : "down"}
      </span>
      {message ? <span class="hint">{message}</span> : null}
    </li>
  );
}

function PendingRow({ action, onResolved }) {
  const resolve = async (approve) => {
    try {
      if (approve) await api.adminApprove(action.id);
      else await api.adminReject(action.id);
      onResolved();
    } catch (e) {
      // surfaced by the reload below
    }
  };
  return (
    <li class="pending">
      <span class="kind">{action.kind}</span>
      <span class="slug">/{action.target_slug}</span>
      <code>{action.payload}</code>
      <Submitter owner={action.owner} name={action.owner_display_name} />
      <button class="primary" onClick={() => resolve(true)}>
        approve
      </button>
      <button onClick={() => resolve(false)}>reject</button>
    </li>
  );
}

// The owner identity carries the provider, so a github submitter is linked to
// the profile while a telegram submitter shows the name to search.
function Submitter({ owner, name }) {
  const label = name || owner;
  if (owner && owner.startsWith("github:")) {
    return (
      <a
        class="submitter"
        href={"https://github.com/" + label}
        target="_blank"
        rel="noopener noreferrer"
      >
        {label}
      </a>
    );
  }
  return <span class="submitter">{label}</span>;
}

function AdminSite({ site, onSaved, onDeleted }) {
  const [form, setForm] = useState({ ...site });
  const [message, setMessage] = useState(null);
  const field = (name) => (event) =>
    setForm({ ...form, [name]: event.target.value });
  const save = async () => {
    try {
      await api.adminEditSite(form);
      setMessage(null);
      onSaved();
    } catch (e) {
      setMessage(e.message);
    }
  };
  const remove = async () => {
    if (!confirm("Remove /" + site.slug + " from the ring?")) return;
    try {
      await api.adminDeleteSite(site.slug);
      onDeleted();
    } catch (e) {
      setMessage(e.message);
    }
  };
  return (
    <li class="site">
      <span class="slug">/{site.slug}</span>
      <input value={form.name} onInput={field("name")} />
      <input value={form.url} onInput={field("url")} />
      <button onClick={save}>save</button>
      <button class="danger" onClick={remove}>
        delete
      </button>
      <span class={site.is_reachable ? "up" : "down"}>
        {site.is_reachable ? "up" : "down"}
      </span>
      {message ? <span class="hint error">{message}</span> : null}
    </li>
  );
}

function Admin({ onLogin }) {
  const [me, setMe] = useState(undefined);
  const [pending, setPending] = useState([]);
  const reload = () => {
    api
      .me()
      .then(setMe)
      .catch(() => setMe(null));
    api
      .adminPending()
      .then(setPending)
      .catch(() => setPending([]));
  };
  useEffect(() => {
    reload();
  }, []);

  if (me === undefined)
    return (
      <main>
        <p>Loading...</p>
      </main>
    );
  if (me === null || !me.is_admin)
    return (
      <main>
        <h1>admin</h1>
        <p>You need an admin account.</p>
        <button class="primary" onClick={onLogin}>
          login
        </button>
      </main>
    );

  return (
    <main>
      <h1>admin</h1>
      <p>Signed in as {me.display_name}.</p>
      <h2>pending actions</h2>
      {pending.length === 0 ? (
        <p>Nothing is waiting for review.</p>
      ) : (
        <ul class="pendings">
          {pending.map((action) => (
            <PendingRow key={action.id} action={action} onResolved={reload} />
          ))}
        </ul>
      )}
      <h2>all sites</h2>
      <ul class="sites">
        {me.sites.map((site) => (
          <AdminSite
            key={site.slug}
            site={site}
            onSaved={reload}
            onDeleted={reload}
          />
        ))}
      </ul>
      <AddSiteForm
        submitLabel="Add site directly"
        onSubmit={(form) => api.adminAddSite(form)}
        onAdded={reload}
      />
    </main>
  );
}

export function App() {
  const [path, navigate] = useRoute();
  const [showLogin, setShowLogin] = useState(false);
  const [me, setMe] = useState(undefined);
  const [config, setConfig] = useState({});

  // Fetch the current account on mount and after a change so the header and the
  // home panel stay in sync.
  const reloadMe = () =>
    api
      .me()
      .then(setMe)
      .catch(() => setMe(null));
  useEffect(() => {
    reloadMe();
    api
      .config()
      .then(setConfig)
      .catch(() => {});
  }, []);

  const onLogin = () => setShowLogin(true);
  const onLogout = async () => {
    try {
      await api.logout();
    } catch (_) {
      // best-effort; redirect regardless
    }
    setMe(null);
    navigate("/");
  };

  let page;
  if (path === "/about") page = <About />;
  else if (path === "/admin") page = <Admin onLogin={onLogin} />;
  else page = <Landing navigate={navigate} me={me} reload={reloadMe} />;

  return (
    <div class="app">
      <Header
        navigate={navigate}
        me={me}
        onLogin={onLogin}
        onLogout={onLogout}
      />
      {page}
      {showLogin ? (
        <LoginModal config={config} onClose={() => setShowLogin(false)} />
      ) : null}
      <footer>
        <p>
          running on{" "}
          <a
            href="https://github.com/b4ck5p4c3/wr"
            target="_blank"
            rel="noopener noreferrer"
          >
            Shit and sticks
          </a>
          {", Copyright "}
          <a href="https://0x08.in" target="_blank" rel="noopener noreferrer">
            B4cksp4ce
          </a>
          , 2026
        </p>
      </footer>
    </div>
  );
}
