import { useEffect, useRef, useState } from "preact/hooks";
import { api } from "./api.js";

// The server falls every unknown path back to this shell, so the router reads
// the path and pushes history without a page reload.
export function useRoute() {
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

// A pulsing placeholder bar shown where a value has not loaded yet, borrowed
// from the fennec.support ghost line.
export function GhostLine({ width = "100%" }) {
  return <span class="ghost" style={{ width }} />;
}

export function Loading() {
  return (
    <div class="ghost-lines">
      <GhostLine width="55%" />
      <GhostLine width="90%" />
      <GhostLine width="70%" />
    </div>
  );
}

export function Header({ navigate, me, onLogin, onLogout }) {
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
            logout..
          </button>
        ) : (
          <button class="primary" onClick={onLogin}>
            login..
          </button>
        )}
      </nav>
    </header>
  );
}

export function link(navigate, to) {
  return (event) => {
    event.preventDefault();
    navigate(to);
  };
}

export function LoginModal({ onClose, config }) {
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
          close..
        </button>
      </div>
    </div>
  );
}

// The favicon is taken from the site itself at its well-known path.
export function faviconFor(url) {
  try {
    return new URL(url).origin + "/favicon.ico";
  } catch (_) {
    return null;
  }
}

// A created_at epoch is rendered as a coarse membership age for the card.
export function formatAge(createdAt) {
  const seconds = Math.max(0, Math.floor(Date.now() / 1000) - createdAt);
  const days = Math.floor(seconds / 86400);
  if (days >= 365) {
    const years = Math.floor(days / 365);
    return years + (years > 1 ? " years" : " year");
  }
  if (days >= 30) {
    const months = Math.floor(days / 30);
    return months + (months > 1 ? " months" : " month");
  }
  if (days >= 1) return days + (days > 1 ? " days" : " day");
  const hours = Math.floor(seconds / 3600);
  if (hours >= 1) return hours + (hours > 1 ? " hours" : " hour");
  return "less than an hour";
}

const UPTIME_COLUMN_COUNT = 48;

// The 7-day hourly history is collapsed into a fixed row of bars, green for a
// healthy hour, red for a down hour, dim for an hour with no probe.
export function UptimeGraph({ history }) {
  if (history == null || history.length === 0) return null;

  const columns = [];
  const per_column = history.length / UPTIME_COLUMN_COUNT;
  for (let c = 0; c < UPTIME_COLUMN_COUNT; c++) {
    const start = Math.floor(c * per_column);
    const end = Math.floor((c + 1) * per_column);
    let sum = 0;
    let sampled_count = 0;
    for (let k = start; k < end; k++) {
      if (history[k] >= 0) {
        sum += history[k];
        sampled_count++;
      }
    }
    columns.push(sampled_count === 0 ? -1 : sum / sampled_count);
  }

  return (
    <div class="uptime-graph" title="uptime over the last 7 days">
      <div class="uptime-bars">
        {columns.map((ratio, i) => (
          <span key={i} class={ratio < 0 ? "gap" : ratio >= 50 ? "up" : "down"}>
            |
          </span>
        ))}
      </div>
      <div class="uptime-axis">
        <span>7d</span>
        <span>now</span>
      </div>
    </div>
  );
}

// The reaction set is the open-source Microsoft Fluent Emoji. The glyphs are
// served as images from the bundle, never as unicode characters.
const REACTIONS = ["poop", "like", "eyes", "fire", "star", "skull"];

export function ReactionBar({ site, me, onLogin, onReacted }) {
  const counts = site.reactions || {};
  const mine = site.reacted || [];
  const react = async (emoji) => {
    if (!me) {
      if (onLogin) onLogin();
      return;
    }
    try {
      await api.react(site.slug, emoji);
      if (onReacted) onReacted();
    } catch (_) {
      // a failed toggle leaves the counts as they were
    }
  };
  return (
    <div class="reactions">
      {REACTIONS.map((emoji) => (
        <button
          key={emoji}
          class={mine.includes(emoji) ? "reaction mine" : "reaction"}
          title={emoji}
          onPointerDown={(e) => e.stopPropagation()}
          onClick={(e) => {
            e.stopPropagation();
            react(emoji);
          }}
        >
          <img
            src={"/emoji/" + emoji + ".png"}
            alt={emoji}
            width="20"
            height="20"
          />
          {counts[emoji] ? (
            <span class="reaction-count">{counts[emoji]}</span>
          ) : null}
        </button>
      ))}
    </div>
  );
}

// One card's inner content, shared by the 3D ring and the vertical phone list.
function cardBody(site, ctx) {
  const icon = faviconFor(site.url);
  return (
    <>
      <header class="tui-bar">
        {site.created_at ? (
          <span class="tui-age-top">{formatAge(site.created_at)}</span>
        ) : null}
        <span class="tui-title">/{site.slug}</span>
      </header>
      <div class="tui-body">
        {icon ? (
          <img
            class="tui-favicon"
            src={icon}
            alt=""
            width="16"
            height="16"
            onError={(e) => (e.currentTarget.style.display = "none")}
          />
        ) : null}
        <a href={site.url} target="_blank" rel="noopener noreferrer">
          {site.name}
        </a>
        {site.description ? <p class="tui-desc">{site.description}</p> : null}
        <ReactionBar
          site={site}
          me={ctx.me}
          onLogin={ctx.onLogin}
          onReacted={ctx.onReacted}
        />
      </div>
    </>
  );
}

const NARROW_QUERY = "(max-width: 640px)";
const MAX_TILT_DEGREES = 20;
const MAX_CARD_POP_PIXELS = 90;
const CARD_BOB_PIXELS = 12;
const CARD_BOB_SPEED = 0.006;
const CARD_BOB_PHASE = 1.1;

// The ring is a carousel of terminal windows seated on a 3D cylinder. The ring
// drifts on its own and a pointer drag grabs it and spins it, with the throw
// velocity carried on release. A vertical drag tilts the ring and the grabbed
// card pops out along its facing, and both ease back on release. Each card also
// bobs up and down on a per-card phase to open vertical space between the cards.
// The transforms are mutated through refs, so a frame never costs a render. Each
// card is double sided, and a card facing away shows its back. On a phone width
// the cylinder is replaced by a plain vertical list.
export function Carousel({ sites, me, onLogin, onReacted }) {
  const ctx = { me, onLogin, onReacted };
  const [isNarrow, setIsNarrow] = useState(
    typeof matchMedia !== "undefined" && matchMedia(NARROW_QUERY).matches,
  );
  useEffect(() => {
    if (typeof matchMedia === "undefined") return;
    const query = matchMedia(NARROW_QUERY);
    const onChange = () => setIsNarrow(query.matches);
    query.addEventListener("change", onChange);
    return () => query.removeEventListener("change", onChange);
  }, []);

  const ringRef = useRef(null);
  const cardRefs = useRef([]);
  const motionRef = useRef({
    rotation: 0,
    velocity: 0,
    isDragging: false,
    lastX: 0,
    downY: 0,
    tilt: 0,
    tiltTarget: 0,
    grabbedIndex: -1,
    cardPop: 0,
    cardPopTarget: 0,
    time: 0,
  });

  const count = sites.length;
  const anglePerCard = 360 / count;
  const radius = Math.round(150 / Math.tan(Math.PI / Math.max(count, 2))) + 40;
  // The drift slows as the count grows to keep the cards readable.
  const idleSpeed = Math.min(0.18, 0.36 / count);
  // A pixel of horizontal drag turns the ring by the arc it subtends at the
  // card radius, so the front card surface tracks the cursor.
  const degreePerDragPixel = 180 / (Math.PI * radius);

  useEffect(() => {
    if (isNarrow) return;
    let frame;
    const tick = () => {
      const motion = motionRef.current;
      if (!motion.isDragging) {
        motion.rotation += motion.velocity || idleSpeed;
        motion.velocity *= 0.94;
        if (Math.abs(motion.velocity) < 0.05) motion.velocity = 0;
      }
      // The tilt and the card pop ease toward their target, so a release slips
      // the ring and the grabbed card back into place.
      motion.tilt += (motion.tiltTarget - motion.tilt) * 0.12;
      motion.cardPop += (motion.cardPopTarget - motion.cardPop) * 0.12;
      motion.time += 1;

      if (
        !motion.isDragging &&
        motion.grabbedIndex >= 0 &&
        Math.abs(motion.cardPop) < 0.5
      ) {
        motion.cardPop = 0;
        motion.grabbedIndex = -1;
      }

      if (ringRef.current != null)
        ringRef.current.style.transform =
          "translateZ(" +
          -radius +
          "px) rotateX(" +
          motion.tilt +
          "deg) rotateY(" +
          motion.rotation +
          "deg)";

      const cards = cardRefs.current;
      for (let i = 0; i < cards.length; i++) {
        const card = cards[i];
        if (card == null) continue;

        const bob =
          Math.sin(motion.time * CARD_BOB_SPEED + i * CARD_BOB_PHASE) *
          CARD_BOB_PIXELS;
        const pop = i === motion.grabbedIndex ? motion.cardPop : 0;
        card.style.transform =
          "rotateY(" +
          i * anglePerCard +
          "deg) translateZ(" +
          (radius + pop) +
          "px) translateY(" +
          bob +
          "px)";
      }

      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [radius, anglePerCard, isNarrow]);

  const onPointerDown = (event) => {
    const motion = motionRef.current;
    motion.isDragging = true;
    motion.velocity = 0;
    motion.lastX = event.clientX;
    motion.downY = event.clientY;

    const grabbed =
      event.target.closest != null ? event.target.closest(".tui-window") : null;
    motion.grabbedIndex =
      grabbed != null ? cardRefs.current.indexOf(grabbed) : -1;
    motion.cardPopTarget = motion.grabbedIndex >= 0 ? MAX_CARD_POP_PIXELS : 0;

    if (event.currentTarget.setPointerCapture != null)
      event.currentTarget.setPointerCapture(event.pointerId);
  };
  const onPointerMove = (event) => {
    const motion = motionRef.current;
    if (!motion.isDragging) return;
    const delta = (event.clientX - motion.lastX) * degreePerDragPixel;
    motion.lastX = event.clientX;
    motion.rotation += delta;
    motion.velocity = delta;

    const offsetY = event.clientY - motion.downY;
    motion.tiltTarget = Math.max(
      -MAX_TILT_DEGREES,
      Math.min(MAX_TILT_DEGREES, -offsetY * 0.2),
    );
  };
  const onPointerUp = () => {
    const motion = motionRef.current;
    motion.isDragging = false;
    motion.tiltTarget = 0;
    motion.cardPopTarget = 0;
  };

  if (isNarrow) {
    return (
      <ul class="ring-vertical">
        {sites.map((site) => (
          <li class="tui-card" key={site.slug}>
            {cardBody(site, ctx)}
          </li>
        ))}
      </ul>
    );
  }

  return (
    <div
      class="ring-stage"
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerLeave={onPointerUp}
    >
      <div class="ring" ref={ringRef}>
        {sites.map((site, i) => (
          <article
            class="tui-window"
            key={site.slug}
            ref={(el) => (cardRefs.current[i] = el)}
            style={{
              transform:
                "rotateY(" +
                i * anglePerCard +
                "deg) translateZ(" +
                radius +
                "px)",
            }}
          >
            <div class="tui-face">{cardBody(site, ctx)}</div>
            <div class="tui-face tui-back">{cardBody(site, ctx)}</div>
          </article>
        ))}
        {sites.map((_, i) => (
          <div
            class="ring-link"
            key={"link-" + i}
            style={{
              transform:
                "rotateY(" +
                (i + 0.5) * anglePerCard +
                "deg) translateZ(" +
                radius +
                "px)",
            }}
          >
            <span class="coin" />
            <span class="coin" />
            <span class="coin" />
          </div>
        ))}
      </div>
    </div>
  );
}

export function Landing({ navigate, me, reload, onLogin }) {
  const [sites, setSites] = useState(null);
  const [error, setError] = useState(null);
  const [showAddSite, setShowAddSite] = useState(false);
  const loadSites = () =>
    api
      .listSites()
      .then(setSites)
      .catch((e) =>
        setError(
          e.isNetworkError
            ? e.message + ". Check your internet connection."
            : e.message,
        ),
      );
  useEffect(() => {
    loadSites();
  }, []);

  return (
    <main>
      <h1>b4cksp4ce webring</h1>
      {error ? (
        <p class="error">{error}</p>
      ) : sites === null ? (
        <Loading />
      ) : sites.length === 0 ? (
        <p>No sites are in the ring yet.</p>
      ) : (
        <Carousel
          sites={sites}
          me={me}
          onLogin={onLogin}
          onReacted={loadSites}
        />
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
          <h2>login status</h2>
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
          <button class="primary" onClick={() => setShowAddSite(true)}>
            add a site..
          </button>
          {showAddSite ? (
            <div class="modal-backdrop" onClick={() => setShowAddSite(false)}>
              <div class="modal" onClick={(e) => e.stopPropagation()}>
                <AddSiteForm
                  onAdded={() => {
                    reload();
                    setShowAddSite(false);
                  }}
                />
                <button class="close" onClick={() => setShowAddSite(false)}>
                  close..
                </button>
              </div>
            </div>
          ) : null}
        </section>
      ) : null}
    </main>
  );
}

export function About() {
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

export const DESCRIPTION_LIMIT = 280;

// A plain textarea paints one color, so the overflow is drawn by a backdrop
// layer under a transparent textarea and the remaining count sits in the corner.
export function DescriptionField({ value, onInput, placeholder }) {
  const backdropRef = useRef(null);
  const within = value.slice(0, DESCRIPTION_LIMIT);
  const over = value.slice(DESCRIPTION_LIMIT);
  const remaining_count = DESCRIPTION_LIMIT - value.length;

  const syncScroll = (event) => {
    if (backdropRef.current != null)
      backdropRef.current.scrollTop = event.target.scrollTop;
  };

  return (
    <div class="description-field">
      <div class="description-backdrop" ref={backdropRef} aria-hidden="true">
        {within}
        <span class="over">{over}</span>
      </div>
      <textarea
        placeholder={placeholder}
        value={value}
        onInput={onInput}
        onScroll={syncScroll}
      />
      <span
        class={
          remaining_count < 0 ? "description-count over" : "description-count"
        }
      >
        {remaining_count}
      </span>
    </div>
  );
}

export function AddSiteForm({
  onAdded,
  submitLabel = "Submit for review",
  onSubmit,
}) {
  const [form, setForm] = useState({
    slug: "",
    name: "",
    url: "",
    description: "",
  });
  const [message, setMessage] = useState(null);
  const field = (name) => (event) =>
    setForm({ ...form, [name]: event.target.value });

  const submit = async (event) => {
    event.preventDefault();
    try {
      const result = onSubmit ? await onSubmit(form) : await api.addSite(form);
      setMessage(result.message);
      setForm({ slug: "", name: "", url: "", description: "" });
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
      <DescriptionField
        placeholder="description (optional)"
        value={form.description}
        onInput={field("description")}
      />
      <button class="primary" type="submit">
        {submitLabel}..
      </button>
      {message ? <p class="hint">{message}</p> : null}
    </form>
  );
}

export function OwnedSite({ site, onRenamed }) {
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
      <div class="row-actions">
        <button onClick={rename}>rename..</button>
      </div>
      <UptimeGraph history={site.uptime} />
      <span class={site.is_reachable ? "up" : "down"}>
        {site.is_reachable ? "up" : "down"}
      </span>
      {message ? <span class="hint">{message}</span> : null}
    </li>
  );
}

export function PendingRow({ action, onResolved }) {
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
      <div class="row-actions">
        <button class="primary" onClick={() => resolve(true)}>
          approve..
        </button>
        <button onClick={() => resolve(false)}>reject..</button>
      </div>
    </li>
  );
}

// The owner identity carries the provider, so a github submitter is linked to
// the profile while a telegram submitter shows the name to search.
export function Submitter({ owner, name }) {
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

export function AdminSite({ site, onSaved, onDeleted }) {
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
      <DescriptionField
        placeholder="description"
        value={form.description || ""}
        onInput={field("description")}
      />
      <div class="row-actions">
        <button onClick={save}>save..</button>
        <button class="danger" onClick={remove}>
          delete..
        </button>
      </div>
      <UptimeGraph history={site.uptime} />
      <span class={site.is_reachable ? "up" : "down"}>
        {site.is_reachable ? "up" : "down"}
      </span>
      {message ? <span class="hint error">{message}</span> : null}
    </li>
  );
}

export function Admin({ onLogin }) {
  const [me, setMe] = useState(undefined);
  const [pending, setPending] = useState([]);
  const [showAllSites, setShowAllSites] = useState(false);
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
        <Loading />
      </main>
    );
  if (me === null || !me.is_admin)
    return (
      <main>
        <h1>admin</h1>
        <p>You need an admin account.</p>
        <button class="primary" onClick={onLogin}>
          login..
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
      <button class="primary" onClick={() => setShowAllSites(true)}>
        all sites.. ({me.sites.length})
      </button>
      {showAllSites ? (
        <div class="modal-backdrop" onClick={() => setShowAllSites(false)}>
          <div class="modal modal-wide" onClick={(e) => e.stopPropagation()}>
            <h2>all sites</h2>
            <ul class="sites modal-scroll">
              {me.sites.map((site) => (
                <AdminSite
                  key={site.slug}
                  site={site}
                  onSaved={reload}
                  onDeleted={reload}
                />
              ))}
            </ul>
            <button class="close" onClick={() => setShowAllSites(false)}>
              close..
            </button>
          </div>
        </div>
      ) : null}
      <AddSiteForm
        submitLabel="Add site directly"
        onSubmit={(form) => api.adminAddSite(form)}
        onAdded={reload}
      />
      <h2>server logs</h2>
      <LogStream />
    </main>
  );
}

// The admin view polls the server log tail and keeps the pane pinned to the
// newest line, so the running trace reads like a live console.
export function LogStream() {
  const [lines, setLines] = useState(null);
  const [error, setError] = useState(null);
  const viewRef = useRef(null);

  useEffect(() => {
    let stopped = false;
    const poll = () =>
      api
        .adminLogs()
        .then((next) => {
          if (stopped) return;
          setLines(next);
          setError(null);
        })
        .catch((e) => {
          if (!stopped) setError(e.message);
        });
    poll();
    const timer = setInterval(poll, 2000);
    return () => {
      stopped = true;
      clearInterval(timer);
    };
  }, []);

  useEffect(() => {
    if (viewRef.current)
      viewRef.current.scrollTop = viewRef.current.scrollHeight;
  }, [lines]);

  if (error) return <p class="error">{error}</p>;
  if (lines === null) return <Loading />;
  if (lines.length === 0) return <p>No log lines yet.</p>;

  return (
    <pre class="log-view" ref={viewRef}>
      {lines.join("\n")}
    </pre>
  );
}

export function NotFound({ navigate }) {
  return (
    <main class="notfound">
      <p class="status">404</p>
      <h1>lost in the ring</h1>
      <p>This page hopped off the webring, or it never joined.</p>
      <p>
        <a href="/" onClick={link(navigate, "/")}>
          back to the ring
        </a>
      </p>
    </main>
  );
}
