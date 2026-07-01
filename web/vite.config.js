import { defineConfig } from "vite";
import preact from "@preact/preset-vite";
import { obfuscate } from "js-confuser";

const OBFUSCATOR_OPTIONS = {
  target: "browser",
  compact: true,
  minify: true,
  hexadecimalNumbers: true,
  identifierGenerator: "mangled",
  renameVariables: true,
  renameGlobals: false,
  renameLabels: false,
  stringConcealing: true,
  stringSplitting: false,
  duplicateLiteralsRemoval: false,
  controlFlowFlattening: false,
  deadCode: false,
  dispatcher: false,
  globalConcealing: false,
  objectExtraction: false,
  variableMasking: false,
  movedDeclarations: false,
  astScrambler: false,
  calculator: false,
  pack: false,
};

function jsConfuserPlugin() {
  return {
    name: "js-confuser",
    apply: "build",
    async generateBundle(_options, bundle) {
      for (const file of Object.values(bundle)) {
        if (file.type !== "chunk" || !file.fileName.endsWith(".js")) continue;
        const result = await obfuscate(file.code, OBFUSCATOR_OPTIONS);
        file.code = typeof result === "string" ? result : result.code;
      }
    },
  };
}

export default defineConfig({
  plugins: [preact(), jsConfuserPlugin()],
  build: {
    outDir: "dist",
    emptyOutDir: true,
    rollupOptions: {
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
