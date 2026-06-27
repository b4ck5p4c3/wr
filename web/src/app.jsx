import { useEffect, useState } from "preact/hooks";
import { api } from "./api.js";
import {
  About,
  Admin,
  CommentsSection,
  Header,
  Landing,
  LoginModal,
  NotFound,
  useButtonParticles,
  useEscape,
  useRoute,
} from "./components.jsx";

export function App() {
  const [path, navigate] = useRoute();
  useButtonParticles();
  const [showLogin, setShowLogin] = useState(false);
  const [showLogoutConfirm, setShowLogoutConfirm] = useState(false);
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

  useEscape(() => setShowLogin(false), showLogin);
  useEscape(() => setShowLogoutConfirm(false), showLogoutConfirm);

  // A signed-in visitor cannot sign in again, so the login affordance opens the
  // logout modal instead.
  const onLogin = () => (me ? setShowLogoutConfirm(true) : setShowLogin(true));
  const onLogout = () => setShowLogoutConfirm(true);
  const doLogout = async () => {
    setShowLogoutConfirm(false);
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
  else if (path === "/admin")
    page = <Admin me={me} onLogin={onLogin} onLogout={onLogout} />;
  else if (path === "/" || path === "/panel")
    page = (
      <Landing
        navigate={navigate}
        me={me}
        reload={reloadMe}
        onLogin={onLogin}
        metricsEnabled={config.metrics_enabled}
      />
    );
  else page = <NotFound navigate={navigate} />;

  return (
    <div class="app">
      <a class="skip-link" href="#main">
        skip to content
      </a>
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
      {showLogoutConfirm ? (
        <div class="modal-backdrop" onClick={() => setShowLogoutConfirm(false)}>
          <div class="modal" onClick={(e) => e.stopPropagation()}>
            <h2>log out</h2>
            <p>End this session and return to the ring?</p>
            <button class="danger" onClick={doLogout}>
              log out..
            </button>
            <button class="close" onClick={() => setShowLogoutConfirm(false)}>
              cancel..
            </button>
          </div>
        </div>
      ) : null}
      <footer>
        <CommentsSection me={me} />
        <p>
          running on{" "}
          <a
            href="https://github.com/b4ck5p4c3/wr"
            target="_blank"
            rel="noopener noreferrer"
          >
            Shit and sticks
          </a>
        </p>
        <p>
          Copyright{" "}
          <a
            href="https://github.com/toiletbril"
            target="_blank"
            rel="noopener noreferrer"
          >
            toiletbril
          </a>
          {" and "}
          <a
            href="https://github.com/b4ck5p4c3/wr/graphs/contributors"
            target="_blank"
            rel="noopener noreferrer"
          >
            project contributors
          </a>
          , 2026
        </p>
      </footer>
    </div>
  );
}
