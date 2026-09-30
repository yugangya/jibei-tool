import vue from "@vitejs/plugin-vue";
import { defineConfig } from "vite";
import { mars3dPlugin } from "vite-plugin-mars3d";

export default defineConfig({
  plugins: [vue(), mars3dPlugin()],
  clearScreen: false,
  server: {
    host: "0.0.0.0",
    port: 5173,
    strictPort: false,
  },
  esbuild: {
    target: "es2022",
  },
  build: {
    target: "es2022",
    sourcemap: false,
  },
});
