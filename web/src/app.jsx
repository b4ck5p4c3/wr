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

function Header({ navigate, onLogin }) {
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
        <a href="/panel" onClick={link(navigate, "/panel")}>
          panel
        </a>
        <button class="primary" onClick={onLogin}>
          login
        </button>
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

function LoginModal({ onClose }) {
  const telegramBot = window.WR_TELEGRAM_BOT;
  return (
    <div class="modal-backdrop" onClick={onClose}>
      <div class="modal" onClick={(e) => e.stopPropagation()}>
        <h2>Sign in</h2>
        <p>Pick a provider to manage your sites.</p>
        <a class="provider github" href="/auth/github">
          Continue with GitHub
        </a>
        {telegramBot ? (
          <a class="provider telegram" href={"https://oauth.telegram.org/auth?bot_id=" + telegramBot}>
            Continue with Telegram
          </a>
        ) : (
          <p class="hint">Telegram login is set up at deploy time.</p>
        )}
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
      {site.favicon ? <img src={site.favicon} alt="" width="16" height="16" /> : null}
      <a href={site.url} target="_blank" rel="noopener noreferrer">
        {site.name}
      </a>
      <span class="slug">/{site.slug}</span>
    </li>
  );
}

function Landing() {
  const [sites, setSites] = useState(null);
  const [error, setError] = useState(null);
  useEffect(() => {
    api.listSites().then(setSites).catch((e) => setError(e.message));
  }, []);

  return (
    <main>
      <h1>The ring</h1>
      <p>A small webring of member sites. Hop between them, or join with a panel.</p>
      {error ? <p class="error">{error}</p> : null}
      {sites === null ? (
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
    </main>
  );
}

function About() {
  return (
    <main>
      <h1>About</h1>
      <p>wr is a webring backend by b4cksp4ce, a hacker space community.</p>
      <ul>
        <li>
          <a href="https://t.me/b4cksp4ce_issues/762" target="_blank" rel="noopener noreferrer">
            The original issue
          </a>
        </li>
        <li>
          <a href="https://b4cksp4ce.net" target="_blank" rel="noopener noreferrer">
            b4cksp4ce
          </a>
        </li>
      </ul>
    </main>
  );
}

function AddSiteForm({ onAdded }) {
  const [form, setForm] = useState({ slug: "", name: "", url: "", favicon: "" });
  const [message, setMessage] = useState(null);
  const field = (name) => (event) => setForm({ ...form, [name]: event.target.value });

  const submit = async (event) => {
    event.preventDefault();
    try {
      const result = await api.addSite(form);
      setMessage(result.message);
      setForm({ slug: "", name: "", url: "", favicon: "" });
      if (onAdded) onAdded();
    } catch (e) {
      setMessage(e.message);
    }
  };

  return (
    <form class="card" onSubmit={submit}>
      <h3>Add a site</h3>
      <input placeholder="slug" value={form.slug} onInput={field("slug")} />
      <input placeholder="name" value={form.name} onInput={field("name")} />
      <input placeholder="https://your.site" value={form.url} onInput={field("url")} />
      <input placeholder="favicon url (optional)" value={form.favicon} onInput={field("favicon")} />
      <button class="primary" type="submit">
        Submit for review
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
      <span class={site.is_reachable ? "up" : "down"}>{site.is_reachable ? "up" : "down"}</span>
      {message ? <span class="hint">{message}</span> : null}
    </li>
  );
}

function Panel({ navigate, onLogin }) {
  const [me, setMe] = useState(undefined);
  const reload = () => api.me().then(setMe).catch(() => setMe(null));
  useEffect(() => {
    reload();
  }, []);

  if (me === undefined) return <main><p>Loading...</p></main>;
  if (me === null)
    return (
      <main>
        <h1>Your panel</h1>
        <p>You are not signed in.</p>
        <button class="primary" onClick={onLogin}>
          login
        </button>
      </main>
    );
  if (me.is_admin) {
    navigate("/admin");
    return null;
  }

  return (
    <main>
      <h1>Your sites</h1>
      <p>Signed in as {me.display_name}.</p>
      <ul class="sites">
        {me.sites.map((site) => (
          <OwnedSite key={site.slug} site={site} onRenamed={reload} />
        ))}
      </ul>
      <AddSiteForm onAdded={reload} />
    </main>
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
      <button class="primary" onClick={() => resolve(true)}>approve</button>
      <button onClick={() => resolve(false)}>reject</button>
    </li>
  );
}

function AdminSite({ site, onSaved }) {
  const [form, setForm] = useState({ ...site });
  const field = (name) => (event) => setForm({ ...form, [name]: event.target.value });
  const save = async () => {
    try {
      await api.adminEditSite(form);
      onSaved();
    } catch (e) {
      // surfaced by the reload
    }
  };
  return (
    <li class="site">
      <span class="slug">/{site.slug}</span>
      <input value={form.name} onInput={field("name")} />
      <input value={form.url} onInput={field("url")} />
      <button onClick={save}>save</button>
      <span class={site.is_reachable ? "up" : "down"}>{site.is_reachable ? "up" : "down"}</span>
    </li>
  );
}

function Admin({ onLogin }) {
  const [me, setMe] = useState(undefined);
  const [pending, setPending] = useState([]);
  const reload = () => {
    api.me().then(setMe).catch(() => setMe(null));
    api.adminPending().then(setPending).catch(() => setPending([]));
  };
  useEffect(() => {
    reload();
  }, []);

  if (me === undefined) return <main><p>Loading...</p></main>;
  if (me === null || !me.is_admin)
    return (
      <main>
        <h1>Admin</h1>
        <p>You need an admin account.</p>
        <button class="primary" onClick={onLogin}>login</button>
      </main>
    );

  return (
    <main>
      <h1>Admin</h1>
      <h2>Pending actions</h2>
      {pending.length === 0 ? (
        <p>Nothing is waiting for review.</p>
      ) : (
        <ul class="pendings">
          {pending.map((action) => (
            <PendingRow key={action.id} action={action} onResolved={reload} />
          ))}
        </ul>
      )}
      <h2>All sites</h2>
      <ul class="sites">
        {me.sites.map((site) => (
          <AdminSite key={site.slug} site={site} onSaved={reload} />
        ))}
      </ul>
    </main>
  );
}

export function App() {
  const [path, navigate] = useRoute();
  const [showLogin, setShowLogin] = useState(false);
  const onLogin = () => setShowLogin(true);

  let page;
  if (path === "/about") page = <About />;
  else if (path === "/panel") page = <Panel navigate={navigate} onLogin={onLogin} />;
  else if (path === "/admin") page = <Admin onLogin={onLogin} />;
  else page = <Landing />;

  return (
    <div class="app">
      <Header navigate={navigate} onLogin={onLogin} />
      {page}
      {showLogin ? <LoginModal onClose={() => setShowLogin(false)} /> : null}
      <footer>wr webring</footer>
    </div>
  );
}
