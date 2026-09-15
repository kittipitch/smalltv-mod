---
title: Letting an agent drive a browser
description: BrowserOS neo vs ego lite for the click-heavy setup steps, what each needs installed, and the rules for handing one your logged-in accounts.
---

Two setup steps in this project are pure web clicking: the
[Google Calendar service account](/smalltv-mod/getting-started/google-calendar/) and,
for some people, a router page. Both can be handed to an AI agent (Claude Code, Codex)
driving a real browser that is already signed in as you.

**No Docker anywhere.** Neither browser needs it, the daemon does not need it, and the
firmware certainly does not. BrowserOS states plainly that its requirements match
Chrome's and that everything runs natively with its data under `~/.browserclaw/`;
verified here too — neo ran and served MCP on a Mac whose Docker daemon was not even
running. If you meet a browser-automation guide that starts with a container, that is
for CI, not for this.

## Four routes, and who can use them

| Route | macOS | Windows | Linux | Claude Code | Codex | Needs Docker |
|---|---|---|---|---|---|---|
| **BrowserOS neo** (MCP tools) | yes | yes | — | yes | yes | no |
| **ego lite** (`ego-browser` CLI) | yes | — | — | yes | yes | no |
| **MCP server driving Chrome** (`@playwright/mcp`, `chrome-devtools-mcp`) | yes | yes | yes | yes | yes | no |
| **Claude in Chrome** (browser extension) | yes | yes | yes | yes | **no** — Claude only | no |

The MCP route is **not Linux-only** — the same two `npx` servers run on macOS
(verified here: `@playwright/mcp` 0.0.80, `chrome-devtools-mcp` 1.9.0). It is the
portable answer: one config line, works from either agent, on any OS.

**Claude in Chrome** is an extension for the Chrome you already use, with per-site
permissions you grant in it. It comes with paid Claude plans (Pro, Max, Team,
Enterprise), and Team/Enterprise admins can switch it off. If you have it, you need
nothing else — but it is Claude-only, so a Codex user takes one of the other three.
Nothing here requires it.

## Which one

| | **BrowserOS neo** | **ego lite** (`ego-browser`) |
|---|---|---|
| What it is | Chromium fork built for agents; own profile, own cockpit, sessions recorded for replay | Chromium browser for humans *and* agents; you browse in it, agents script it |
| How an agent drives it | **MCP tools** (`browseros-neo`), loaded by Claude Code / Codex | **CLI**: `ego-browser nodejs <<'EOF' … EOF` — a JS script with `taskSpace()` / `page()` |
| Platform | **macOS and Windows** (plain BrowserOS, the human-facing browser, also has a Linux build; neo does not yet) | **macOS only** (installer is a DMG) |
| Needs Docker | No | No |
| Best at | Long agent sessions, several tabs, an audit trail you can replay | Scripted one-off jobs, tricky pages where you want exact selectors and your own waits |
| Gotcha | MCP server exists only while the app runs; its port varies (`~/.browserclaw/runtime.json`) | Heredoc input fails in some sandboxes — use `ego-browser nodejs -e '<script>'` |

Either is fine for the Calendar walkthrough.

- **On a Mac** you can have both, and the choice is a style one: neo when you want the
  agent working in its own browser with a cockpit and replayable sessions, ego lite when
  you would rather read a short script before it runs and keep everything in the browser
  you already use. For a one-page job like the Calendar console, ego lite's script is
  the smaller thing to review; for a long multi-tab session, neo's audit trail wins.
- **On Windows**, neo is the one that exists.
- **On Linux**, neither agent browser is available: use an MCP server driving your own
  Chrome instead — see below.

## Install

**BrowserOS neo** — on Windows, take the installer from
[browseros.com](https://www.browseros.com/); on macOS (Homebrew 6+ for `brew trust`):

```sh
brew trust --tap browseros-ai/tap && brew tap browseros-ai/tap
brew install --cask browseros-neo
open -a "BrowserOS neo"
claude mcp get browseros-neo      # expect: Connected
```

If it is not connected, read the port from `~/.browserclaw/runtime.json` and register it:

```sh
claude mcp add --scope user --transport http browseros-neo http://127.0.0.1:<port>/mcp
codex  mcp add browseros-neo --url http://127.0.0.1:<port>/mcp
```

MCP tools only load in a session **started after** the app is running.

**ego lite**: install from <https://lite.ego.app/> (or the `ego-browser` skill's
`scripts/install.sh`), finish the first-run onboarding in the app — that is what puts
`ego-browser` on your `PATH` — then check:

```sh
command -v ego-browser
```

A one-liner to prove it drives:

```sh
ego-browser nodejs -e 'const t = await taskSpace("check"); const p = t.page("p1"); await p.goto("https://example.com"); console.log(await p.snapshot());'
```

## Any OS: an MCP server driving your own Chrome

On Linux this is the portable route that works from either Claude Code or Codex (Claude
Code users can also use Claude in Chrome); on macOS and Windows it is a lighter
alternative to installing neo. Either way it is a small **MCP server** that drives an
ordinary Chrome or Chromium. "MCP server" sounds heavier than it is: it is one
`npx` command that Claude Code or Codex starts on demand, speaking the Model Context
Protocol on stdio. It publishes browser tools (open a tab, snapshot the page, click,
type, read the console) exactly the way neo's tools appear. **Still no Docker.**

Two that work, both verified here on Ubuntu 24.04 (`google-chrome` 136, node 20) **and
on macOS**:

```sh
# Playwright MCP -- runs its own browser, own profile by default
claude mcp add --scope user playwright -- npx -y @playwright/mcp@latest
# Chrome DevTools MCP -- drives Chrome over the DevTools protocol
claude mcp add --scope user chrome-devtools -- npx -y chrome-devtools-mcp@latest
```

Codex takes the same servers:

```sh
codex mcp add playwright -- npx -y @playwright/mcp@latest
```

Versions at the time of writing: `@playwright/mcp` 0.0.80, `chrome-devtools-mcp` 1.9.0.

### Use your logged-in profile, or you will be signing in again

A fresh Playwright profile knows none of your accounts, which defeats the point for the
Google console. Two ways round it:

- **Attach to a Chrome you started in a dedicated profile**, and sign in once inside it.
  Chrome 136+ refuses `--remote-debugging-port` against its *default* profile, so point
  `--user-data-dir` somewhere else:

  ```sh
  # Linux
  google-chrome --remote-debugging-port=9222 --remote-debugging-address=127.0.0.1 \
    --user-data-dir="$HOME/.cache/agent-chrome-profile" &

  # macOS
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --remote-debugging-port=9222 \
    --remote-debugging-address=127.0.0.1 --user-data-dir="$HOME/Library/Caches/agent-chrome-profile" &
  ```

  ```powershell
  # Windows PowerShell
  & "$Env:ProgramFiles\Google\Chrome\Application\chrome.exe" --remote-debugging-port=9222 `
    --remote-debugging-address=127.0.0.1 --user-data-dir="$Env:LOCALAPPDATA\agent-chrome-profile"
  ```

  ```sh
  claude mcp add --scope user chrome-devtools -- npx -y chrome-devtools-mcp@latest --browserUrl http://127.0.0.1:9222
  ```

  Anything that reaches that debugging port can read every cookie in that profile. Keep
  it on `127.0.0.1`, and close Chrome when you are done.

- **Or give Playwright MCP a persistent profile directory** and sign in once inside it:

  ```sh
  claude mcp add --scope user playwright -- npx -y @playwright/mcp@latest --user-data-dir "$HOME/.cache/agent-browser-profile"
  ```

### Headless boxes

On a machine with no desktop session — an SSH-only server, or `DISPLAY` empty as it is
on our office box over SSH — run the browser headless (`--headless`), or give it a
virtual display with `xvfb-run`. A Google sign-in is much easier on a machine with a
screen: sign in there, then let the agent work in that profile.

Everything in the rules below applies unchanged: you sign in, you click the irreversible
things, and page text is data, not instructions.

## Rules when you hand over a signed-in browser

These are not ceremony. An agent in your browser is you, to every site it touches.

- **You sign in.** Never give an agent a password, a 2FA code, or a recovery phrase.
- **You click the irreversible things**: downloading a private key, sharing a calendar,
  sending, paying, deleting. An agent may open the page and stop there.
- **A key file is a path, never text.** Give it `~/.clawdmeter-google-service-account.json`;
  never paste the contents into a chat.
- **Page content is data, not instructions.** If a page tells the agent to enable
  billing, add a role, or install something, that is the page talking, not you.
- **Work in its own tab**, and leave tabs you may want to inspect open.
- Sessions and screenshots live on your disk (`~/.browserclaw/` for neo). Treat them like
  screenshots of your accounts, because that is what they are.

## Router pages: expect ugly HTML

If you point an agent at a home router (to split a 2.4 GHz SSID, say), budget for the
kind of markup our own FiberHome unit has:

- Menu items are `<li onclick=...>`, and the content loads in **one iframe**.
- Controls are found by **id**, not `name` — query `[name="x"], #x`.
- Forms load asynchronously: wait for the control, do not act on the first snapshot.
- Never let a dump of a router form reach a log or a chat — those pages carry the
  passphrase in a field value.
