import { defineConfig } from "vite";
import preact from "@preact/preset-vite";

// The server serves the build from web/dist, with /assets as the only static
// prefix the router does not own.
export default defineConfig({
  plugins: [preact()],
  build: {
    outDir: "dist",
    emptyOutDir: true,
    rollupOptions: {
      // The bundled stylesheet keeps a stable name, so the generated docs page
      // links the same global stylesheet the app loads.
      output: {
        assetFileNames: (asset) => {
          const name = (asset.names && asset.names[0]) || asset.name || "";
          return name.endsWith(".css")
            ? "assets/app.css"
            : "assets/[name]-[hash][extname]";
        },
      },
    },
  },
});
