<!-- README.md ──────────────────────────────────────────────── -->
<h1 align="center">🔍 Search Engine Simulator (OS2025 HW‑4)</h1>
<p align="center">
  <img src="https://img.shields.io/badge/language-C‑17-blue.svg" />
  <img src="https://img.shields.io/badge/threads-Pthreads-success.svg" />
  <img src="https://img.shields.io/badge/build‑type‑Unix-Makefile‑green.svg" />
  <img src="https://img.shields.io/badge/License-MIT-purple.svg" />
</p>

> **A blazing‑fast, multi‑threaded text‑indexer and contextual search CLI**  
> written in modern C (+ POSIX threads) that demos lock‑free hashing, worker–
> pool orchestration and optional stop‑word censorship.

---

## ✨ Key selling points
| Capability | TL;DR |
|------------|-------|
| **Concurrent indexing** | Every `_index_ <file>` spawns a detached worker that tokenises, normalises and inserts words into a *lock‑free* hash‑map (open addressing + linear probing) – no global mutexes, zero contention. :contentReference[oaicite:0]{index=0} |
| **Context‑aware search** | `_search_ <word>` streams matches ordered by *descending term‑frequency*, then prints rich snippets (whole sentences) for instant relevance. :contentReference[oaicite:1]{index=1} |
| **Censorship pipeline** | At start‑up you may pass a *stop‑list*; all black‑listed tokens and their sentences are skipped at both index and query time. :contentReference[oaicite:2]{index=2} |
| **Elastic hash‑map core** | Custom bucket array auto‑resizes @ 0.75 load‑factor; each slot stores a word plus a dynamically‑grown vector of `<file, occurrence‑count, contexts[]>`. :contentReference[oaicite:3]{index=3} |
| **CLI micro‑shell** | Minimal REPL exposes `_index_`, `_search_`, `_clear_`, `_stop_`. Unknown commands yield actionable hints. :contentReference[oaicite:4]{index=4} |
| **ANSI UX** | Colour‑coded prompts, progress ticks and result highlights for first‑class terminal experience (demo GIF below). |
| **Portable build** | Single‑file **Makefile**; depends only on glibc & `pthread`. Runs on Ubuntu, Arch, Alpine, WSL – anywhere POSIX is near. |

---

## 🏗️ Architecture at a glance  

```mermaid
flowchart LR
    subgraph CLI
        A[_index_ fileN] -->|enqueue| Q(Queue)
        B[_search_ term] -->|read‑only| H[(Hash‑Map)]
        C[_stop_] --> MAIN
    end
    Q -->|dequeue| WK1(Thread‑Pool) --> H
    Q --> WK2
    Q --> WKn
    MAIN[(Main thread)]

