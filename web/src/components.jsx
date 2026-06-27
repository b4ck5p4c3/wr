import { useEffect, useMemo, useRef, useState } from "preact/hooks";
import { api } from "./api.js";

// A visitor who asks the system for reduced motion gets the continuous and the
// decorative motion dropped. The CSS gates the keyframes and the transitions,
// and this reading gates the JS-driven carousel spin, the bob, the page
// crossfade, and the click sparks, which a stylesheet cannot reach.
function prefersReducedMotion() {
  return (
    typeof matchMedia !== "undefined" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches
  );
}

// A route change is wrapped in the View Transitions API, borrowed from the
// fennec.support navigation. The old page crossfades into the new one, and a
// browser without the API swaps the path with no animation. The returned promise
// yields a tick, and the Preact render commits before the new frame is captured.
function withViewTransition(applyChange) {
  if (
    prefersReducedMotion() ||
    typeof document === "undefined" ||
    document.startViewTransition == null
  ) {
    applyChange();
    return;
  }
  document.startViewTransition(() => {
    applyChange();
    return new Promise((resolve) => setTimeout(resolve, 0));
  });
}

// A typed trailing slash on a known path would otherwise miss every route and
// render NotFound, since the route match is exact.
function normalizePath(pathname) {
  return pathname !== "/" && pathname.endsWith("/")
    ? pathname.slice(0, -1)
    : pathname;
}

// The server falls every unknown path back to this shell. The router reads the
// path and pushes history without a page reload.
export function useRoute() {
  const [path, setPath] = useState(normalizePath(location.pathname));
  useEffect(() => {
    const onPop = () =>
      withViewTransition(() => setPath(normalizePath(location.pathname)));
    addEventListener("popstate", onPop);
    return () => removeEventListener("popstate", onPop);
  }, []);
  const navigate = (to) =>
    withViewTransition(() => {
      history.pushState(null, "", to);
      setPath(normalizePath(to));
    });
  return [path, navigate];
}

export function useEscape(onClose, isOpen) {
  useEffect(() => {
    if (!isOpen) return;
    const onKey = (event) => {
      if (event.key === "Escape") onClose();
    };
    addEventListener("keydown", onKey);
    return () => removeEventListener("keydown", onKey);
  }, [onClose, isOpen]);
}

const CLICK_PARTICLE_COUNT = 12;

function spawnClickParticles(originX, originY) {
  for (let i = 0; i < CLICK_PARTICLE_COUNT; i++) {
    const particle = document.createElement("span");
    particle.className = "click-particle";
    const angle =
      (Math.PI * 2 * i) / CLICK_PARTICLE_COUNT + Math.random() * 0.6;
    const distance = 16 + Math.random() * 26;
    particle.style.left = originX + "px";
    particle.style.top = originY + "px";
    particle.style.setProperty("--dx", Math.cos(angle) * distance + "px");
    particle.style.setProperty("--dy", Math.sin(angle) * distance + "px");
    document.body.appendChild(particle);
    particle.addEventListener("animationend", () => particle.remove());
  }
}

// Every enabled button across the site scatters a spark burst on click. The
// listener sits on the document, and a button added later is covered without a
// per-button handler.
export function useButtonParticles() {
  useEffect(() => {
    const onClick = (event) => {
      const target = event.target;
      const button = target.closest != null ? target.closest("button") : null;
      if (button == null || button.disabled || prefersReducedMotion()) {
        return;
      }
      spawnClickParticles(event.clientX, event.clientY);
    };
    addEventListener("click", onClick);
    return () => removeEventListener("click", onClick);
  }, []);
}

// A poll runs once on mount and then repeats on the interval. A late reply that
// lands after unmount is dropped before it sets state on a gone view.
function usePolling(fetcher, onData, onError, intervalMs) {
  useEffect(() => {
    let isStopped = false;
    const poll = () =>
      fetcher()
        .then((next) => {
          if (!isStopped) onData(next);
        })
        .catch((error) => {
          if (!isStopped) onError(error);
        });
    poll();
    const timer = setInterval(poll, intervalMs);
    return () => {
      isStopped = true;
      clearInterval(timer);
    };
  }, []);
}

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
        <a class="nav-link" href="/" onClick={link(navigate, "/")}>
          ring
        </a>
        <a class="nav-link" href="/about" onClick={link(navigate, "/about")}>
          about
        </a>
        <a class="nav-link" href="/docs">
          docs
        </a>
        {me ? (
          <button class="logout" onClick={onLogout}>
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

export function Modal({ label, wide, onClose, children }) {
  return (
    <div class="modal-backdrop" onClick={onClose}>
      <div
        class={wide ? "modal modal-wide" : "modal"}
        role="dialog"
        aria-modal="true"
        aria-label={label}
        onClick={(e) => e.stopPropagation()}
      >
        {children}
      </div>
    </div>
  );
}

function SearchModal({
  title,
  query,
  onQuery,
  searchLabel,
  searchPlaceholder,
  isEmpty,
  emptyText,
  onClose,
  children,
}) {
  return (
    <Modal label={title} wide onClose={onClose}>
      <h2>{title}</h2>
      <input
        class="site-search"
        aria-label={searchLabel}
        placeholder={searchPlaceholder}
        value={query}
        onInput={(e) => onQuery(e.target.value)}
      />
      {isEmpty ? <p>{emptyText}</p> : children}
      <button class="close" onClick={onClose}>
        close..
      </button>
    </Modal>
  );
}

export function LoginModal({ onClose, config }) {
  const telegramBot = config.telegram_bot;
  return (
    <Modal label="sign in" onClose={onClose}>
      <h2>sign in</h2>
      <p>Pick a provider to manage your sites.</p>
      {config.github ? (
        <a class="provider github" href="/auth/github">
          Continue with GitHub
          <span class="provider-preferred">preferred</span>
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
          aria-label="bypass login"
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
    </Modal>
  );
}

export function faviconFor(url) {
  try {
    return new URL(url).origin + "/favicon.ico";
  } catch (_) {
    return null;
  }
}

// The apple touch icon is a larger square icon the site serves at a well-known
// path, so it is tried first, and the favicon.ico is the fallback. The icons
// are read straight from the site, never through a third party.
export function faviconCandidates(url) {
  let origin = "";
  try {
    origin = new URL(url).origin;
  } catch (_) {
    return [];
  }
  return [
    origin + "/apple-touch-icon.png",
    origin + "/apple-touch-icon-precomposed.png",
    faviconFor(url),
  ];
}

function faviconHost(url) {
  try {
    return new URL(url).host;
  } catch (_) {
    return "";
  }
}

function readFaviconCache(host) {
  try {
    return host ? localStorage.getItem("favicon:" + host) : null;
  } catch (_) {
    return null;
  }
}

function writeFaviconCache(host, src) {
  try {
    if (host && src) {
      localStorage.setItem("favicon:" + host, src);
    }
  } catch (_) {
    // a private window or a full store leaves the icon uncached
  }
}

// The keys are collected before removal, since removing an entry shifts the
// remaining indices under localStorage.key.
function clearFaviconCache() {
  try {
    const keys = [];
    for (let index = 0; index < localStorage.length; index++) {
      const key = localStorage.key(index);
      if (key && key.startsWith("favicon:")) {
        keys.push(key);
      }
    }
    keys.forEach((key) => localStorage.removeItem(key));
    return keys.length;
  } catch (_) {
    return 0;
  }
}

function Favicon({ url }) {
  const host = useMemo(() => faviconHost(url), [url]);
  const candidates = useMemo(() => faviconCandidates(url), [url]);
  const [src, setSrc] = useState(
    () => readFaviconCache(host) || candidates[0] || null,
  );
  if (!src) return null;
  return (
    <img
      src={src}
      alt=""
      width="38"
      height="38"
      onLoad={() => writeFaviconCache(host, src)}
      onError={() => {
        const index = candidates.indexOf(src);
        const next =
          index >= 0 ? candidates[index + 1] || null : candidates[0] || null;
        setSrc(next === src ? null : next);
      }}
    />
  );
}

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

function formatTimestamp(epochSeconds) {
  const date = new Date(epochSeconds * 1000);
  const pad = (value) => String(value).padStart(2, "0");
  return (
    pad(date.getDate()) +
    "-" +
    pad(date.getMonth() + 1) +
    " " +
    pad(date.getHours()) +
    ":" +
    pad(date.getMinutes())
  );
}

const UPTIME_COLUMN_COUNT = 48;

export function UptimeGraph({ history }) {
  const columns = useMemo(() => {
    if (history == null || history.length === 0) return null;

    const built = [];
    const perColumn = history.length / UPTIME_COLUMN_COUNT;
    for (let column = 0; column < UPTIME_COLUMN_COUNT; column++) {
      const start = Math.floor(column * perColumn);
      const end = Math.floor((column + 1) * perColumn);
      let sum = 0;
      let sampledCount = 0;
      for (let bucket = start; bucket < end; bucket++) {
        if (history[bucket] >= 0) {
          sum += history[bucket];
          sampledCount++;
        }
      }
      built.push(sampledCount === 0 ? -1 : sum / sampledCount);
    }

    return built;
  }, [history]);

  if (columns == null) return null;

  return (
    <div
      class="uptime-graph"
      role="img"
      aria-label="uptime over the last 7 days"
      title="uptime over the last 7 days"
    >
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
  let hasReactions = false;
  for (const key in counts) {
    if (counts[key] > 0) {
      hasReactions = true;
      break;
    }
  }
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
    <div class={hasReactions ? "reactions has-reactions" : "reactions"}>
      {REACTIONS.map((emoji) => (
        <button
          key={emoji}
          class={
            "reaction" +
            (mine.includes(emoji) ? " mine" : "") +
            (counts[emoji] ? "" : " empty")
          }
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

function ownerProfileUrl(oauth, tag) {
  if (!tag) return null;
  if (oauth === "github") return "https://github.com/" + tag;
  if (oauth === "telegram") return "https://t.me/" + tag;
  return null;
}

function displayUrl(url) {
  try {
    const parsed = new URL(url);
    const path = parsed.pathname === "/" ? "" : parsed.pathname;
    return (parsed.host + path).replace(/^www\./, "");
  } catch (_) {
    return url;
  }
}

// One card's inner content, shared by the 3D ring and the vertical phone list.
// The card reads as a tweet, the site name is the display name, the owner tag is
// the handle, and the description is the body.
function cardBody(site, ctx) {
  const handle = site.owner_tag ? "@" + site.owner_tag : null;
  const ownerUrl = ownerProfileUrl(site.owner_oauth, site.owner_tag);
  const handleClass =
    "tweet-handle" + (site.owner_oauth ? " owner-" + site.owner_oauth : "");
  const recordClick = () => {
    if (ctx.metricsEnabled) api.recordClick(site.slug).catch(() => {});
  };
  return (
    <div class="tweet">
      <header class="tweet-head">
        <a
          class="tweet-avatar"
          href={site.url}
          target="_blank"
          rel="noopener noreferrer"
          onPointerDown={(e) => e.stopPropagation()}
          onClick={recordClick}
        >
          <Favicon url={site.url} />
        </a>
        <span class="tweet-id">
          <a
            class="tweet-name"
            href={site.url}
            target="_blank"
            rel="noopener noreferrer"
            onPointerDown={(e) => e.stopPropagation()}
            onClick={recordClick}
          >
            {site.name}
          </a>
          <span class="tweet-sub">
            {handle ? (
              ownerUrl ? (
                <a
                  class={handleClass}
                  href={ownerUrl}
                  target="_blank"
                  rel="noopener noreferrer"
                  onPointerDown={(e) => e.stopPropagation()}
                >
                  {handle}
                </a>
              ) : (
                <span class={handleClass}>{handle}</span>
              )
            ) : null}
            {site.created_at ? (
              <span class="tweet-age">
                {formatAge(site.created_at) + " ago"}
              </span>
            ) : null}
          </span>
        </span>
      </header>
      {site.description ? <p class="tweet-text">{site.description}</p> : null}
      <a
        class="tweet-link"
        href={site.url}
        target="_blank"
        rel="noopener noreferrer"
        onPointerDown={(e) => e.stopPropagation()}
        onClick={recordClick}
      >
        {"visit " + displayUrl(site.url)}
      </a>
      <ReactionBar
        site={site}
        me={ctx.me}
        onLogin={ctx.onLogin}
        onReacted={ctx.onReacted}
      />
    </div>
  );
}

const NARROW_QUERY = "(max-width: 640px)";
const MAX_TILT_DEGREES = 20;
const MAX_CARD_POP_PIXELS = 90;
const CARD_BOB_PIXELS = 12;
const CARD_BOB_SPEED = 0.006;
const CARD_BOB_PHASE = 1.1;
const HOVER_SPIN_SCALE = 0.1;
const TILT_GAIN = 0.2;
const WHEEL_GAIN = 0.35;

// The ring is a carousel of terminal windows seated on a 3D cylinder. The ring
// drifts on its own and a pointer drag grabs it and spins it, with the throw
// velocity carried on release. A vertical drag tilts the ring and the grabbed
// card pops out along its facing, and both ease back on release. Each card also
// bobs up and down on a per-card phase to open vertical space between the cards.
// The transforms are mutated through refs, so a frame never costs a render. Each
// card is double sided, and a card facing away shows its back. On a phone width
// the cylinder is replaced by a plain vertical list.
export function Carousel({ sites, me, onLogin, onReacted, metricsEnabled }) {
  const ctx = { me, onLogin, onReacted, metricsEnabled };
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
    isHovering: false,
  });

  const count = sites.length;
  // The drift slows as the count grows to keep the cards readable. A pixel of
  // horizontal drag turns the ring by the arc it subtends at the card radius,
  // the front card surface tracks the cursor.
  const { anglePerCard, radius, idleSpeed, degreePerDragPixel } =
    useMemo(() => {
      const anglePerCard = 360 / count;
      const radius =
        Math.round(171 / Math.tan(Math.PI / Math.max(count, 2))) + 40;
      const idleSpeed = Math.min(0.18, 0.36 / count);
      const degreePerDragPixel = 180 / (Math.PI * radius);
      return { anglePerCard, radius, idleSpeed, degreePerDragPixel };
    }, [count]);

  useEffect(() => {
    if (isNarrow) return;
    const reduced = prefersReducedMotion();
    let frame;
    const tick = () => {
      const motion = motionRef.current;
      if (!motion.isDragging) {
        const spinScale = motion.isHovering ? HOVER_SPIN_SCALE : 1;
        motion.rotation +=
          (motion.velocity || (reduced ? 0 : idleSpeed)) * spinScale;
        motion.velocity *= 0.94;
        if (Math.abs(motion.velocity) < 0.05) motion.velocity = 0;
      }
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

      if (ringRef.current != null) {
        ringRef.current.style.transform =
          "translateZ(" +
          -radius +
          "px) rotateX(" +
          motion.tilt +
          "deg) rotateY(" +
          motion.rotation +
          "deg)";
      }

      const cards = cardRefs.current;
      const grabbedIndex = motion.grabbedIndex;
      const cardPop = motion.cardPop;
      for (let i = 0; i < cards.length; i++) {
        const card = cards[i];
        if (card == null) continue;

        const bob = reduced
          ? 0
          : Math.sin(motion.time * CARD_BOB_SPEED + i * CARD_BOB_PHASE) *
            CARD_BOB_PIXELS;
        const pop = i === grabbedIndex ? cardPop : 0;
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

    const target = event.target;
    const grabbed =
      target.closest != null ? target.closest(".tui-window") : null;
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
      Math.min(MAX_TILT_DEGREES, -offsetY * TILT_GAIN),
    );
  };
  const onPointerUp = () => {
    const motion = motionRef.current;
    motion.isDragging = false;
    motion.tiltTarget = 0;
    motion.cardPopTarget = 0;
  };
  // A horizontal wheel or trackpad swipe spins the ring and carries the throw.
  // A vertical scroll is left to the page. The turn is inverted to match the
  // card travel, and the gain is damped to a calm arc.
  const onWheel = (event) => {
    if (event.deltaX === 0) return;
    event.preventDefault();
    const motion = motionRef.current;
    const turn = -event.deltaX * degreePerDragPixel * WHEEL_GAIN;
    motion.rotation += turn;
    motion.velocity = turn;
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
      onWheel={onWheel}
    >
      <div class="ring" ref={ringRef}>
        {sites.map((site, i) => (
          <article
            class="tui-window"
            key={site.slug}
            ref={(el) => (cardRefs.current[i] = el)}
            onPointerEnter={() => (motionRef.current.isHovering = true)}
            onPointerLeave={() => (motionRef.current.isHovering = false)}
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

export function Landing({ navigate, me, reload, onLogin, metricsEnabled }) {
  const [sites, setSites] = useState(null);
  const [error, setError] = useState(null);
  const [showAddSite, setShowAddSite] = useState(false);
  useEscape(() => setShowAddSite(false), showAddSite);
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
    <main id="main">
      <h1>b4cksp4ce webring</h1>
      <div class="ring-area">
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
            metricsEnabled={metricsEnabled}
          />
        )}
      </div>

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
          {me.pending.length > 0 ? (
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
            <Modal label="add a site" onClose={() => setShowAddSite(false)}>
              <AddSiteForm
                onAdded={() => {
                  reload();
                  setShowAddSite(false);
                }}
              />
              <button class="close" onClick={() => setShowAddSite(false)}>
                close..
              </button>
            </Modal>
          ) : null}
        </section>
      ) : null}
    </main>
  );
}

export function About() {
  return (
    <main id="main">
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
      <p>
        <a
          href="https://fennec.support"
          target="_blank"
          rel="noopener noreferrer"
        >
          contact the fennec
        </a>{" "}
        to be added to the webring.
      </p>
    </main>
  );
}

export const DESCRIPTION_LIMIT = 280;

// A textarea paints one color. The overflow is painted by a backdrop layer
// under a transparent textarea, and the remaining count sits in the corner.
export function DescriptionField({ value, onInput, placeholder }) {
  const backdropRef = useRef(null);
  const within = value.slice(0, DESCRIPTION_LIMIT);
  const over = value.slice(DESCRIPTION_LIMIT);
  const remainingCount = DESCRIPTION_LIMIT - value.length;

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
        aria-label={placeholder}
        placeholder={placeholder}
        value={value}
        onInput={onInput}
        onScroll={syncScroll}
      />
      <span
        class={
          remainingCount < 0 ? "description-count over" : "description-count"
        }
      >
        {remainingCount}
      </span>
    </div>
  );
}

// The per-field input setter for a form state object. The functional updater
// reads the latest state, so a rapid edit across fields never drops a value.
function fieldSetter(setForm) {
  return (name) => (event) => {
    const value = event.target.value;
    setForm((prev) => ({ ...prev, [name]: value }));
  };
}

function UptimeRow({ site }) {
  return (
    <div class="uptime-row">
      <UptimeGraph history={site.uptime} />
      <span class={site.is_reachable ? "up" : "down"}>
        {site.is_reachable ? "currently up" : "currently down"}
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
  const field = fieldSetter(setForm);

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
      <input
        aria-label="site slug"
        placeholder="slug"
        value={form.slug}
        onInput={field("slug")}
      />
      <input
        aria-label="site name"
        placeholder="name"
        value={form.name}
        onInput={field("name")}
      />
      <input
        aria-label="site url"
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
      <input
        aria-label="site name"
        value={name}
        onInput={(e) => setName(e.target.value)}
      />
      <div class="row-actions">
        <button onClick={rename}>rename..</button>
      </div>
      <UptimeRow site={site} />
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
    } catch (_) {
      // a failed resolve leaves the action in the queue
    }
  };
  return (
    <li class="pending">
      <span class="kind">{action.kind}</span>
      <span class="slug">/{action.target_slug}</span>
      <code>{action.payload}</code>
      <Submitter
        owner={action.owner}
        oauth={action.owner_oauth}
        name={action.owner_display_name}
        tag={action.owner_tag}
      />
      <div class="row-actions">
        <button class="primary" onClick={() => resolve(true)}>
          approve..
        </button>
        <button onClick={() => resolve(false)}>reject..</button>
      </div>
    </li>
  );
}

function ProfileLink({ url, className, linkClassName, children }) {
  if (url) {
    return (
      <a
        class={linkClassName || className}
        href={url}
        target="_blank"
        rel="noopener noreferrer"
      >
        {children}
      </a>
    );
  }
  return <span class={className}>{children}</span>;
}

export function Submitter({ owner, oauth, name, tag }) {
  const label = name || tag || owner;
  const url = ownerProfileUrl(oauth, tag);
  return (
    <ProfileLink url={url} className="submitter">
      {label}
    </ProfileLink>
  );
}

function CommentAuthor({ comment }) {
  const url = ownerProfileUrl(comment.author_oauth, comment.author_tag);
  return (
    <ProfileLink url={url} className="comment-author">
      {comment.author_name}
    </ProfileLink>
  );
}

export function AdminSite({ site, onSaved, onDeleted }) {
  const [form, setForm] = useState({ ...site });
  const [message, setMessage] = useState(null);
  const field = fieldSetter(setForm);
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
      <span class="owned-by">
        owned by{" "}
        <Submitter
          owner={site.owner}
          name={site.owner_display_name}
          tag={site.owner_tag}
        />
      </span>
      <input aria-label="site name" value={form.name} onInput={field("name")} />
      <input aria-label="site url" value={form.url} onInput={field("url")} />
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
      <UptimeRow site={site} />
      {message ? <span class="hint error">{message}</span> : null}
    </li>
  );
}

function matchByFields(items, query, getFields) {
  const needle = query.trim().toLowerCase();
  if (needle === "") return items;
  return items.filter((item) =>
    getFields(item).some((field) =>
      (field || "").toLowerCase().includes(needle),
    ),
  );
}

const matchSites = (sites, query) =>
  matchByFields(sites, query, (site) => [site.slug, site.name]);

const matchPending = (actions, query) =>
  matchByFields(actions, query, (action) => [
    action.owner,
    action.owner_display_name,
    action.kind,
    action.target_slug,
    action.payload,
  ]);

const matchComments = (comments, query) =>
  matchByFields(comments, query, (comment) => [
    comment.author_name,
    comment.body,
  ]);

export function Admin({ me: appMe, onLogin, onLogout }) {
  const [me, setMe] = useState(undefined);
  const [pending, setPending] = useState([]);
  const [pendingComments, setPendingComments] = useState([]);
  const [showAllSites, setShowAllSites] = useState(false);
  const [siteQuery, setSiteQuery] = useState("");
  const [showActions, setShowActions] = useState(false);
  const [actionQuery, setActionQuery] = useState("");
  const [showComments, setShowComments] = useState(false);
  const [commentQuery, setCommentQuery] = useState("");
  const [busyCommentId, setBusyCommentId] = useState(null);
  const [isCacheBusy, setIsCacheBusy] = useState(false);
  const [cacheNotice, setCacheNotice] = useState(null);
  const [cacheError, setCacheError] = useState(null);
  useEscape(() => setShowAllSites(false), showAllSites);
  useEscape(() => setShowActions(false), showActions);
  useEscape(() => setShowComments(false), showComments);
  const reload = () => {
    api
      .me()
      .then(setMe)
      .catch(() => setMe(null));
    api
      .adminPending()
      .then(setPending)
      .catch(() => setPending([]));
    api
      .adminPendingComments()
      .then(setPendingComments)
      .catch(() => setPendingComments([]));
  };
  useEffect(() => {
    reload();
  }, []);

  const resolveComment = async (id, isApprove) => {
    setBusyCommentId(id);
    try {
      await (isApprove
        ? api.adminApproveComment(id)
        : api.adminDeleteComment(id));
      reload();
    } catch (_) {
      // a failed resolve leaves the queue unchanged
    } finally {
      setBusyCommentId(null);
    }
  };

  const clearCache = async () => {
    setIsCacheBusy(true);
    setCacheNotice(null);
    setCacheError(null);
    try {
      await api.adminClearCache();
      const removedCount = clearFaviconCache();
      setCacheNotice("Cache cleared, " + removedCount + " icons dropped.");
    } catch (e) {
      setCacheError(e.message);
    } finally {
      setIsCacheBusy(false);
    }
  };

  // The panel falls back to the account the app already holds, so a signed-in
  // visitor without the admin role is offered a logout even if the panel's own
  // lookup has not resolved or has failed.
  const account = me ?? appMe;

  if (account === undefined)
    return (
      <main>
        <Loading />
      </main>
    );
  if (account === null)
    return (
      <main>
        <h1>admin</h1>
        <p>You need an admin account.</p>
        <button class="primary" onClick={onLogin}>
          login..
        </button>
      </main>
    );
  if (!account.is_admin)
    return (
      <main>
        <h1>admin</h1>
        <p>Signed in as {account.display_name}, who is not an admin.</p>
        <button class="logout" onClick={onLogout}>
          logout..
        </button>
      </main>
    );

  return (
    <main id="main">
      <h1>admin</h1>
      <p>Signed in as {account.display_name}.</p>
      <button class="primary" onClick={() => setShowActions(true)}>
        pending actions.. ({pending.length})
      </button>
      {showActions ? (
        <SearchModal
          title="pending actions"
          query={actionQuery}
          onQuery={setActionQuery}
          searchLabel="search pending actions"
          searchPlaceholder="search by user or content"
          isEmpty={pending.length === 0}
          emptyText="Nothing is waiting for review."
          onClose={() => setShowActions(false)}
        >
          <ul class="pendings modal-scroll">
            {matchPending(pending, actionQuery).map((action) => (
              <PendingRow key={action.id} action={action} onResolved={reload} />
            ))}
          </ul>
        </SearchModal>
      ) : null}
      <button class="primary" onClick={() => setShowComments(true)}>
        pending comments.. ({pendingComments.length})
      </button>
      {showComments ? (
        <SearchModal
          title="pending comments"
          query={commentQuery}
          onQuery={setCommentQuery}
          searchLabel="search pending comments"
          searchPlaceholder="search by user or content"
          isEmpty={pendingComments.length === 0}
          emptyText="No comments are waiting for review."
          onClose={() => setShowComments(false)}
        >
          <ul class="pending-comments modal-scroll">
            {matchComments(pendingComments, commentQuery).map((comment) => (
              <li class="pending-comment" key={comment.id}>
                <div class="comment-head">
                  <CommentAuthor comment={comment} />
                  <span class="comment-time">
                    {formatTimestamp(comment.created_at)}
                  </span>
                </div>
                <span class="comment-body">{comment.body}</span>
                <div class="pending-comment-actions">
                  <button
                    class="primary"
                    disabled={busyCommentId === comment.id}
                    onClick={() => resolveComment(comment.id, true)}
                  >
                    approve..
                  </button>
                  <button
                    class="danger"
                    disabled={busyCommentId === comment.id}
                    onClick={() => resolveComment(comment.id, false)}
                  >
                    delete..
                  </button>
                </div>
              </li>
            ))}
          </ul>
        </SearchModal>
      ) : null}
      <button class="primary" onClick={() => setShowAllSites(true)}>
        all sites.. ({account.sites.length})
      </button>
      {showAllSites ? (
        <SearchModal
          title="all sites"
          query={siteQuery}
          onQuery={setSiteQuery}
          searchLabel="search all sites"
          searchPlaceholder="search by name or slug"
          isEmpty={false}
          onClose={() => setShowAllSites(false)}
        >
          <ul class="sites modal-scroll">
            {matchSites(account.sites, siteQuery).map((site) => (
              <AdminSite
                key={site.slug}
                site={site}
                onSaved={reload}
                onDeleted={reload}
              />
            ))}
          </ul>
        </SearchModal>
      ) : null}
      <AddSiteForm
        submitLabel="Add site directly"
        onSubmit={(form) => api.adminAddSite(form)}
        onAdded={reload}
      />
      <h2>audit log</h2>
      <AuditLog />
      <h2>traffic stats</h2>
      <Stats />
      <h2>cache</h2>
      <button class="primary" disabled={isCacheBusy} onClick={clearCache}>
        clean cache..
      </button>
      {cacheNotice ? <p class="hint">{cacheNotice}</p> : null}
      {cacheError ? <p class="error">{cacheError}</p> : null}
      <h2>server logs</h2>
      <LogStream />
    </main>
  );
}

export function Stats() {
  const [stats, setStats] = useState(null);
  const [error, setError] = useState(null);

  useEffect(() => {
    api
      .adminStats()
      .then(setStats)
      .catch((e) => setError(e.message));
  }, []);

  if (error) return <p class="error">{error}</p>;
  if (stats === null) return <Loading />;
  if (!stats.enabled)
    return (
      <p class="hint">
        Metrics are disabled. Launch with --enable-metrics to record them.
      </p>
    );

  return (
    <div class="stats">
      <p class="stats-totals">
        {stats.total_clicks} clicks, {stats.total_hops} hops across the ring.
      </p>
      {stats.sites.length === 0 ? (
        <p class="hint">No traffic has been recorded yet.</p>
      ) : (
        <table class="stats-table">
          <thead>
            <tr>
              <th>site</th>
              <th>clicks</th>
              <th>hops</th>
            </tr>
          </thead>
          <tbody>
            {stats.sites.map((row) => (
              <tr key={row.slug}>
                <td>/{row.slug}</td>
                <td>{row.click_count}</td>
                <td>{row.hop_count}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </div>
  );
}

export function AuditLog() {
  const [entries, setEntries] = useState(null);
  const [error, setError] = useState(null);

  usePolling(
    api.adminAudit,
    (next) => {
      setEntries(next);
      setError(null);
    },
    (e) => setError(e.message),
    5000,
  );

  if (error) return <p class="error">{error}</p>;
  if (entries === null) return <Loading />;
  if (entries.length === 0) return <p>No admin actions yet.</p>;

  return (
    <ul class="audit-log">
      {entries.map((entry) => {
        const actorUrl = ownerProfileUrl(entry.actor_oauth, entry.actor_tag);
        return (
          <li class="audit-entry" key={entry.id}>
            <span class="audit-time">{formatTimestamp(entry.created_at)}</span>
            <span class="audit-action">{entry.action}</span>
            <span class="audit-target">{entry.target}</span>
            <ProfileLink
              url={actorUrl}
              className="audit-actor"
              linkClassName={"audit-actor owner-" + entry.actor_oauth}
            >
              {entry.actor}
            </ProfileLink>
            {entry.actor_ip ? (
              <span class="audit-ip">{entry.actor_ip}</span>
            ) : null}
            {entry.detail ? (
              <span class="audit-detail">{entry.detail}</span>
            ) : null}
          </li>
        );
      })}
    </ul>
  );
}

export function LogStream() {
  const [lines, setLines] = useState(null);
  const [error, setError] = useState(null);
  const viewRef = useRef(null);
  const didInitialScrollRef = useRef(false);

  usePolling(
    api.adminLogs,
    (next) => {
      setLines(next);
      setError(null);
    },
    (e) => setError(e.message),
    2000,
  );

  // The newest line is shown when the panel first fills, and a later poll
  // leaves the scroll position alone so a reader is not pulled off the line
  // they are reading.
  useEffect(() => {
    if (
      viewRef.current != null &&
      lines != null &&
      !didInitialScrollRef.current
    ) {
      viewRef.current.scrollTop = viewRef.current.scrollHeight;
      didInitialScrollRef.current = true;
    }
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

// A comment body is split so an @slug that names a site in the ring links to
// that site, and an @handle that names the owner of a site in the ring links to
// that person's provider profile. An unknown mention stays plain text.
function renderMentions(body, slugs, people) {
  return body.split(/(@[A-Za-z0-9_-]+)/g).map((part) => {
    if (part[0] !== "@") return part;
    const handle = part.slice(1);
    if (slugs.has(handle)) {
      return (
        <a class="mention" href={"/" + handle}>
          {part}
        </a>
      );
    }
    const person = Object.prototype.hasOwnProperty.call(people, handle)
      ? people[handle]
      : null;
    if (person) {
      return (
        <a
          class={"mention owner-" + person.provider}
          href={person.url}
          target="_blank"
          rel="noopener noreferrer"
        >
          {part}
        </a>
      );
    }
    return part;
  });
}

const COMMENT_PAGE_SIZE = 5;
const COMMENT_LEAVE_MS = 300;

// The footer comments. An owner of a site in the ring posts a short note, and an
// @slug mention links to that site. The slug set is read from the public
// listing so a mention is linked only when it names a real site. A new comment
// is held for admin approval, and the list is paged through a load-more button.
export function CommentsSection({ me }) {
  const [comments, setComments] = useState(null);
  const [slugs, setSlugs] = useState(() => new Set());
  const [people, setPeople] = useState({});
  const [draft, setDraft] = useState("");
  const [error, setError] = useState(null);
  const [notice, setNotice] = useState(null);
  const [hasMore, setHasMore] = useState(false);
  const [removingId, setRemovingId] = useState(null);
  const [leavingId, setLeavingId] = useState(null);
  const loadGenRef = useRef(0);
  const leaveTimerRef = useRef(0);

  // A post or a delete fires a fresh page-zero load while a load-more may still
  // be in flight. The stale response is dropped so the rows are never doubled.
  const loadPage = (offset) => {
    const generation = ++loadGenRef.current;
    return api
      .listComments(offset, COMMENT_PAGE_SIZE)
      .then((page) => {
        if (generation !== loadGenRef.current) return;
        setComments((prev) =>
          offset === 0 ? page : (prev ?? []).concat(page),
        );
        setHasMore(page.length === COMMENT_PAGE_SIZE);
      })
      .catch((e) => {
        if (generation !== loadGenRef.current) return;
        setError(e.message);
      });
  };

  useEffect(() => () => clearTimeout(leaveTimerRef.current), []);

  useEffect(() => {
    loadPage(0);
    api
      .listSites()
      .then((sites) => {
        setSlugs(new Set(sites.map((site) => site.slug)));
        const handles = {};
        for (const site of sites) {
          const oauth = site.owner_oauth;
          const url = ownerProfileUrl(oauth, site.owner_tag);
          if (site.owner_tag && oauth && url) {
            handles[site.owner_tag] = { url, provider: oauth };
          }
        }
        setPeople(handles);
      })
      .catch(() => {});
  }, []);

  const canComment = !!(me && me.sites && me.sites.length > 0);
  const isAdmin = !!(me && me.is_admin);
  const post = async () => {
    if (draft.trim() === "") return;
    try {
      await api.postComment(draft.trim());
      setDraft("");
      setError(null);
      if (isAdmin) {
        setNotice("Your comment is posted.");
        loadPage(0);
      } else {
        setNotice("Your comment was sent and is waiting for approval.");
      }
    } catch (e) {
      setError(e.message);
      setNotice(null);
    }
  };

  // A delete removes the row from the server too. The live list length stays
  // aligned with the server offset, and the next page starts after it.
  const loadMore = () => loadPage(comments ? comments.length : 0);

  const removeComment = async (id) => {
    setRemovingId(id);
    try {
      await api.adminDeleteComment(id);
      setError(null);
      // A load-more in flight is superseded here, since the delete shifts the
      // server offset out from under its request.
      loadGenRef.current++;
      setLeavingId(id);
      leaveTimerRef.current = setTimeout(() => {
        setComments((prev) =>
          (prev ?? []).filter((comment) => comment.id !== id),
        );
        setLeavingId(null);
      }, COMMENT_LEAVE_MS);
    } catch (e) {
      setError(e.message);
    } finally {
      setRemovingId(null);
    }
  };

  // A draft keystroke re-renders the section, so the rows are memoized to keep
  // a comment body from being re-split on every character.
  const commentRows = useMemo(
    () =>
      (comments ?? []).map((comment) => (
        <li
          class={"comment" + (leavingId === comment.id ? " leaving" : "")}
          key={comment.id}
        >
          <div class="comment-head">
            <CommentAuthor comment={comment} />
            <span class="comment-meta">
              <span class="comment-time">
                {formatTimestamp(comment.created_at)}
              </span>
              {isAdmin ? (
                <button
                  class="danger comment-remove"
                  disabled={
                    removingId === comment.id || leavingId === comment.id
                  }
                  onClick={() => removeComment(comment.id)}
                >
                  delete..
                </button>
              ) : null}
            </span>
          </div>
          <span class="comment-body">
            {renderMentions(comment.body, slugs, people)}
          </span>
        </li>
      )),
    [comments, leavingId, isAdmin, removingId, slugs, people],
  );

  return (
    <section class="comments">
      <h2>comments</h2>
      {canComment ? (
        <div class="comment-form">
          <textarea
            aria-label="leave a comment"
            value={draft}
            maxLength={500}
            placeholder="leave a note, @tag a site or a person in the ring"
            onInput={(e) => {
              setDraft(e.target.value);
              setNotice(null);
            }}
          />
          <button class="primary" onClick={post}>
            post..
          </button>
        </div>
      ) : (
        <p class="hint">
          {me
            ? "Only owners of a site in the ring may comment."
            : "Sign in as a site owner to comment."}
        </p>
      )}
      {notice && !error ? <p class="notice">{notice}</p> : null}
      {error ? <p class="error">{error}</p> : null}
      {comments === null ? (
        error ? null : (
          <Loading />
        )
      ) : comments.length === 0 ? (
        <p class="comment-empty">No comments yet.</p>
      ) : (
        <ul class="comment-list">{commentRows}</ul>
      )}
      {hasMore ? (
        <button class="primary comment-more" onClick={loadMore}>
          load more..
        </button>
      ) : null}
    </section>
  );
}

export function NotFound({ navigate }) {
  return (
    <main class="notfound" id="main">
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
