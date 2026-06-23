import { defineConfig } from "vite";
import preact from "@preact/preset-vite";

// The server serves the build from web/dist, with /assets as the only static
// prefix the router does not own.
export default defineConfig({
  plugins: [preact()],
  build: { outDir: "dist", emptyOutDir: true },
});
