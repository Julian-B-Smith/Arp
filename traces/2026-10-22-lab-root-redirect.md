# lab-root-redirect — the server's root opens the lab, not a directory listing

- **Queue item:** unqueued, at the human's report ("the lab links to the
  directory currently").
- **Cause:** both the `lab` preview and `python3 -m http.server` serve the
  repo root, so http://localhost:59531/ showed a bare directory listing. The
  lab lives at `/lab/arp-lab.html`.
- **Changed:** a root `index.html` that redirects to `lab/arp-lab.html`
  (Echo's pattern), with a plain link as a fallback; README and CODEMAP
  updated.
- **Evidence:** `curl /` returns the redirect; in the browser,
  http://localhost:59531/ landed on `/lab/arp-lab.html`, titled "Arp lab".
- **Verify:** `./verify fast` exit 0.
