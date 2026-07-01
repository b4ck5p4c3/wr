import { render } from "preact";
import { App } from "./app.jsx";
import "./reset.css";
import "./global.css";
import "./animations.css";

if (localStorage.getItem("low-detail") === "1")
  document.documentElement.classList.add("low-detail");

render(<App />, document.getElementById("app"));
