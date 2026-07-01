import { defineConfig } from "vite";
import preact from "@preact/preset-vite";
import obfuscator from "vite-plugin-javascript-obfuscator";

// The server serves the build from web/dist, with /assets as the only static
// prefix the router does not own.
export default defineConfig({
  plugins: [
    preact(),
    obfuscator({
      apply: "build",
      include: ["**/*.js", "**/*.jsx"],
      options: {
        seed: 0,
        compact: true,
        identifierNamesGenerator: "mangled",
        simplify: true,
        stringArray: true,
        stringArrayThreshold: 0.8,
        stringArrayEncoding: ["base64"],
        stringArrayRotate: true,
        stringArrayShuffle: true,
        splitStrings: true,
        splitStringsChunkLength: 10,
        transformObjectKeys: false,
        controlFlowFlattening: false,
        deadCodeInjection: false,
        debugProtection: false,
        selfDefending: false,
        renameGlobals: false,
      },
    }),
  ],
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
