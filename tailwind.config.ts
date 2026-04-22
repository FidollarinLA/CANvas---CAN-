import type { Config } from "tailwindcss";

export default {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        canvas: {
          bg: "#f4f6f8",
          panel: "#ffffff",
          ink: "#111827",
          mute: "#6b7280",
          accent: "#0e5bd8",
          line: "#d1d5db",
        },
      },
      boxShadow: {
        soft: "0 12px 40px rgba(17, 24, 39, 0.08)",
      },
      keyframes: {
        riseIn: {
          "0%": { opacity: "0", transform: "translateY(10px)" },
          "100%": { opacity: "1", transform: "translateY(0)" },
        },
      },
      animation: {
        riseIn: "riseIn 0.4s ease-out both",
      },
      fontFamily: {
        body: ["Space Grotesk", "Avenir Next", "Segoe UI", "sans-serif"],
        mono: ["IBM Plex Mono", "Menlo", "monospace"],
      },
    },
  },
  plugins: [],
} satisfies Config;
