# Endless Orgasm Machine UI

![Screenshot](../doc/Screenshot3.png)

A Svelte app that shows the EOM's readings and settings and can forward the pleasure level to
other toys through [Buttplug.io](https://buttplug.io). It connects to the EOM over Bluetooth
(Chrome or Edge on desktop and Android) or over WebSocket.

- `npm install` once, then `npm run dev` for a local development server.
- `npm run build` builds one self-contained `dist/index.html` and copies it, gzipped, to
  `../data/ui` for *Upload Filesystem Image*.
- `npm run check` type-checks the project.

Pushing UI changes to `main` publishes the UI to GitHub Pages through
`.github/workflows/ui.yml`, once Pages is enabled with *Source: GitHub Actions*.

`buttplug/` holds a prebuilt Buttplug client and its WebAssembly server, which talks to toys
through Web Bluetooth.
