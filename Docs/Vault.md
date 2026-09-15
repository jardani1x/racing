---
title: How this vault works
type: reference
tags:
  - index
  - meta
---

# How this vault works

`Docs/` is an Obsidian vault and a tracked repository folder at the same time.
That dual role is the reason for every rule below: a note has to survive being
read by someone who has never opened Obsidian, and the vault has to survive
being cloned onto a machine that has never seen it.

Start at [[Home]].

---

## Opening it

Obsidian → **Open folder as vault** → pick the repository's `Docs` folder.

Do not create a vault anywhere else inside the repository. A vault is just a
folder Obsidian has been pointed at, so a second one nested in the working tree
adds an untracked directory that gets committed by accident and a `.obsidian`
directory nobody is watching.

The vault's shared settings are committed (`Docs/.obsidian/app.json`,
`appearance.json`, `core-plugins.json`, `templates.json`), so everyone opens it
configured the same way. Per-machine state — window layout, graph view position,
hotkeys, installed community plugins, themes — is gitignored, because it is
about the person at the keyboard and not about the project. Changing your own
layout will therefore never show up as a diff.

Daily notes and Sync are off. Neither fits a repository-backed vault: daily
notes scatter dated files with no ticket to hang them on, and Sync would be a
second, conflicting source of truth alongside git.

---

## The rules that keep notes portable

**Plain CommonMark, always.** These files are read on GitHub, in editors, and by
agents. Nothing may depend on Obsidian to be legible.

**Wikilinks for notes inside the vault, backticked paths for everything else.**
`[[07-QualityGates]]` links a note. A reference to code or a script is written
as an inline path — `Source/RacingSim/Vehicle/` — never as a link, because those
files are outside the vault and a link to them resolves to nothing.

**Never rename a note by hand.** Rename from inside Obsidian, which rewrites
every inbound link. A hand-rename leaves dangling links that only surface when
somebody opens the graph weeks later.

**Numbered notes keep their numbers.** `00-` through `18-` encode reading order
and are cited by number in `CLAUDE.md`, in tickets, and in commit messages.
Renumbering breaks citations that live outside this vault and cannot be updated
by a link-rewriter.

**Frontmatter is three optional keys**: `title`, `type`, `tags`. `type` is one
of `index`, `reference`, `ticket`, `adr`, `report`. Keep it light — heavy
frontmatter is noise in a plain-text diff and buys nothing here.

---

## Layout

| Path | Holds |
|---|---|
| `Docs/Home.md` | Entry point. Start here. |
| `Docs/00-` … `18-` | The numbered reference set, in reading order. |
| `Docs/Tickets.md` | The ordered ticket set. The long one. |
| `Docs/Environment.md` | Machine setup, engine pin, harness facts. |
| `Docs/ADR/` | Architecture decision records, one per decision. |
| `Docs/Reports/` | Milestone reports and decision sheets. |
| `Docs/Templates/` | Note templates. Insert with the Templates core plugin. |
| `Docs/Attachments/` | Images pasted into notes. Created on first paste. |

Two files are deliberately not markdown and will show in the file explorer as
unsupported: `AssetOwnership.tsv` and `ProgressTracker.xlsx`. They are data, not
notes, and they stay where the tooling that reads them expects them.

---

## Writing a new note

1. Put it in the folder that matches what it is — an ADR in `ADR/`, a milestone
   report in `Reports/`, anything else at the vault root.
2. Start from a template in `Templates/` where one fits.
3. Link it from [[Home]], or from the note that supersedes it. An unlinked note
   is one nobody finds; the graph view exists to make those visible.
4. Commit it with the change it documents, not in a separate documentation
   commit. Documentation that lands a week after the code is documentation that
   describes something else.

---

## What does not belong here

- **Secrets, keys, tokens, or credentials.** This folder is committed and is a
  candidate for open-sourcing.
- **Licensed or reference imagery.** Anything with a licence question goes
  through [[13-AssetLicenseLedger]] first.
- **Generated output.** Test reports, build logs, and automation `index.json`
  files live under `Saved/` and `Scripts/Test/`, not in the vault.
- **A copy of anything the repository already states.** The code, the tickets,
  and `CLAUDE.md` are the truth. A note that restates them becomes a second,
  quietly wrong version of them.
