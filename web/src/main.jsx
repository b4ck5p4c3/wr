import { render } from "preact";
import { App } from "./app.jsx";
import "./reset.css";
import "./global.css";
import "./animations.css";

try {
  if (localStorage.getItem("low-detail") === "1")
    document.documentElement.classList.add("low-detail");
} catch (_) {}

render(<App />, document.getElementById("app"));
