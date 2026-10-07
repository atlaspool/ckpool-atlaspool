# AtlasPool Modifications to CKPool — APIO CKPool 2026-10

**Branch:** `apio-ckpool-2026-10`
**Clean base:** upstream ckpool `e9b66549a636489d7e4eeb92995c25fe9a25ab35` (ckolivas
master HEAD, 2026-09-24), tagged `apio-ckpool-oct2026`. Includes the upstream in-tree
Cap'n Proto IPC mining shim (`ipc/mining/`).
**Upstream source:** https://bitbucket.org/ckolivas/ckpool/
**Maintained by:** AtlasPool
**License:** GPLv3 (unchanged from upstream)

---

## Production validation — four testnet4 blocks mined with this exact build

This build was validated end-to-end against a live **Bitcoin Core v32** node over the
Cap'n Proto IPC mining interface: it successfully found, submitted, and got **four blocks
accepted and confirmed on testnet4**. This is the direct proof that modification #5 (the
`submitSolution @10` IPC stub regen) works — before that fix, every solved block was
rejected at the IPC layer and never reached the network.

| Block height | Hash |
|---|---|
| 154810 | [`0000000000000000bfe518f5716bfff334520f24680de626668bf76b06481b08`](https://mempool.space/testnet4/block/0000000000000000bfe518f5716bfff334520f24680de626668bf76b06481b08) |
| 154820 | [`00000000000000008fb3711edb4f8455f53cead9b6436e611f0a20a5dcab7c9b`](https://mempool.space/testnet4/block/00000000000000008fb3711edb4f8455f53cead9b6436e611f0a20a5dcab7c9b) |
| 154842 | [`0000000000000000722564cadd2bbb32f4270ba5a69f5321fa78a466f7484d31`](https://mempool.space/testnet4/block/0000000000000000722564cadd2bbb32f4270ba5a69f5321fa78a466f7484d31) |
| 154864 | [`00000000000000014790fbda94997cd109182deaf8da73657ef276de364713a4`](https://mempool.space/testnet4/block/00000000000000014790fbda94997cd109182deaf8da73657ef276de364713a4) |

---

## Purpose

This branch is a **fresh start** from the latest upstream ckpool, into which a
**deliberately small, hand-picked** set of AtlasPool modifications has been folded. It
is NOT a continuation of the old `main` branch (which was based on an October 2025
upstream snapshot and carried 8 modifications plus an embedded HTTP API server). The
old `main` branch is preserved for history; its `ATLASPOOL_MODIFICATIONS.md` documents
that older set.

This document captures **only the four changes** carried on this branch. Anything in
the old `main` doc that is not listed here is **intentionally NOT present** on this
branch.

### Deliberately NOT carried over from old `main`
- HTTP API server (port 8080) + its buffer-overflow fix + case-insensitive wallet
  lookup — **the on-node :8080 API is no longer used; that role is handled by a
  separate binary.** The 472-line `api_server.c`/`.h` are absent from this branch.
- Rental-rig `login` method handler.
- `mining.extranonce.subscribe` dummy-success handler.

---

## Modifications on this branch (5)

| # | Modification | Files |
|---|---|---|
| 1 | High Difficulty Port Minimum (`highdiffmin`) | `src/ckpool.h`, `src/ckpool.c`, `src/stratifier.c` |
| 2 | BTC Signature Customization (drop hardcoded `ckpool` coinbase prefix) | `src/stratifier.c` |
| 3 | User Agent Tracking (incl. Jan 2026 update-on-every-connection fix) | `src/stratifier.c` |
| 4 | Default Donation Address | `src/ckpool.c` |
| 5 | IPC mining stubs regenerated for Core v32 (`submitSolution @10`) | `ipc/mining/mining.capnp` (+ regenerated `*.capnp.c++`/`.h`), `ipc/mining/mining_ipc.cpp` |

Items 1-4 are marked with an `ATLASPOOL:` comment (`git grep ATLASPOOL`). Item 5 is an
IPC-compatibility fix (schema regen), described below.

### 5. IPC mining stubs regenerated for Core v32 (`submitSolution @10`)

**Problem (caught on testnet4):** ckpool's bundled Cap'n Proto mining schema targeted the
old `BlockTemplate.submitSolution @7 -> (result: Bool)`. Bitcoin Core **v32** deprecated that
method (renamed it `submitSolutionOld7`) and replaced it with
`submitSolution @10 -> (reason: Text, debug: Text, result: Bool)`. Against a v32 node every
block submission therefore failed at the IPC layer with
`Old submitSolution (@7) not supported. Please update your client!` — so a found block was
**never submitted to the network**. This is NOT one of the AtlasPool feature mods; it is a
compatibility fix required to run ckpool's IPC mining against Core v32.

**Fix:**
- `ipc/mining/mining.capnp`: adopt v32's `BlockTemplate.submitSolution @10` (returning
  reason/debug/result) and keep the deprecated `submitSolutionOld7 @7`; also add v32's
  `Mining.submitBlock @7`, `getTransactionsByTxID @8`, `getTransactionsByWitnessID @9`. The
  schema file id (`@0xc77d03df6a41b505`) and all other types are unchanged — this is exactly
  Core v32's `src/ipc/capnp/mining.capnp` body under ckpool's local-copy header.
- Regenerated `mining.capnp.{c++,h}` and `common.capnp.{c++,h}` with Cap'n Proto 1.1.0:
  ```
  cd ipc/mining
  capnpc -o c++ -I /usr/local/include -I . mining.capnp common.capnp
  ```
  (The `-I` paths are required: `/usr/local/include` resolves `import "/capnp/c++.capnp"`,
  `.` resolves `import "/mp/proxy.capnp"` — the client-only proxy shim vendored in `mp/`.)
- `ipc/mining/mining_ipc.cpp`: `mining_ipc_submit_solution` now also reads the node's
  `reason`/`debug` from the `@10` response and logs them to stderr (captured by journald) on
  a rejection, so a rejected submission is diagnosable. The pure-C API in `mining_ipc.h` and
  the caller in `stratifier.c` are unchanged (no ABI change).

**Note:** upstream ckpool (including current master) still ships `submitSolution @7`, so this
branch is **ahead of upstream** for Core v32 support. Regenerate again if a future Core bumps
the Mining interface; the authoritative schema is always `<core>/src/ipc/capnp/mining.capnp`.

> **Note on line numbers:** this branch's base is ~11 months newer than old `main`, so
> every change was re-located against current `e9b66549` source rather than applied by
> old line number. Current ckpool uses **yyjson** (not jansson) for JSON, so the JSON
> emission code differs from the old `main` doc's `JSON_CPACK` snippets.

---

### 1. High Difficulty Port Minimum (`highdiffmin`)

**Purpose:** High-difficulty ports (port > 4000) start clients at `highdiff`
(1,000,000), but vardiff can lower a client's difficulty over time, defeating the
purpose of a dedicated high-hashrate port. `highdiffmin` is a configurable floor that
vardiff cannot go below on high-diff ports.

**Logic ported to match the old `main` branch exactly** (operator-confirmed that
`main` handles this correctly), adapted only to this base's API (`yyjson`/`ckpool.`
instead of `json_get_int64`/`ckp->`).

**`src/ckpool.h`** — new field on `struct ckpool_instance`, next to `highdiff`:
```c
int64_t highdiffmin; // ATLASPOOL: Minimum difficulty for high diff ports (default 1000000)
```

**`src/ckpool.c`** — parse from config (`parse_config`), default (`main`), and report
(`report_config`):
```c
/* parse */
yyjson_obj_get_int64(&ckpool.highdiffmin, json_conf, "highdiffmin");
/* default (matches main branch) */
if (!ckpool.highdiffmin)
    ckpool.highdiffmin = 1000000;
/* report_config / -help-debug dump (one line beyond main, for visibility) */
printf("highdiffmin = %"PRId64"\n", ckpool.highdiffmin);
```

**`src/stratifier.c`** — enforcement in the vardiff recalculation (`add_submit`),
placed **after** the pool-mindiff / user-mindiff `MAX` clamps and **before** the
maxdiff / network_diff `MIN` clamps — the exact placement used on `main`, so the floor
can never raise `optimal` above network difficulty:
```c
/* ATLASPOOL: enforce minimum difficulty for high diff ports ... */
if (ckpool.server_highdiff && ckpool.server_highdiff[client->server]) {
    optimal = MAX(optimal, ckpool.highdiffmin);
}
```

**Config (`ckpool.conf`, optional):**
```json
{
  "highdiff": 1000000,
  "highdiffmin": 1000000
}
```
- Default `highdiffmin` = 1,000,000 (matches `main`). High-diff ports (>4000) cannot
  vardiff below it. Normal ports (≤4000) are unaffected.

**Difference from `main`:** the only intentional addition beyond `main` is the
`highdiffmin` line in the `report_config()` config dump (`main` does not print it).
The runtime behaviour is identical to `main`.

---

### 2. BTC Signature Customization

**Purpose:** Upstream hardcodes a `ckpool` tag at the start of the coinbase signature,
so a configured `btcsig` of `/AtlasPool/IAD/` lands on-chain as `ckpool/AtlasPool/IAD/`.
AtlasPool wants the coinbase signature to be **exactly** the configured `btcsig`.

**`src/stratifier.c`** (`generate_coinbase`):
```c
// BEFORE (upstream):
wb->coinb2bin = ckzalloc(512);
memcpy(wb->coinb2bin, "\x0a\x63\x6b\x70\x6f\x6f\x6c", 7);  // push of "ckpool"
wb->coinb2len = 7;

// AFTER (ATLASPOOL):
wb->coinb2bin = ckzalloc(512);
wb->coinb2len = 0;   // no hardcoded prefix; sig = configured btcsig only
```

**Why it is safe:** the coinbase script length byte `coinb1bin[41]` is computed from
the final running `len` (which includes `coinb2len`) and set as `coinb1bin[41] = len - 1`
**after** coinb2 is built, so dropping the 7 bytes self-corrects the length. If
`btcsig` is unset, the coinbase simply carries no vanity tag — the mandatory BIP34
block height and the extranonce are always present, so the coinbase scriptSig remains
valid (well above the 2-byte minimum). AtlasPool always sets `btcsig` in production, so
the live result is `height + extranonce + btcsig`.

---

### 3. User Agent Tracking (incl. Jan 2026 fix)

**Purpose:** Record miner software (user-agent) per worker for analytics/support, and
keep it current when miners upgrade firmware.

Upstream already tracks `useragent` on the per-connection `stratum_instance` and
includes it in the share log and client status JSON. It does **not** propagate it to
the aggregated `worker_instance` or expose it in worker stats — this modification adds
that.

**`src/stratifier.c`:**

a) New field on `struct worker_instance`:
```c
char *useragent;  // ATLASPOOL: miner software of the most recent connection
```

b) Populate it when a client attaches to its worker (`generate_user`, under
`instance_lock`). **Update on every connection** (the Jan 2026 fix — the original
version only set it on first connection, so it never reflected firmware upgrades):
```c
if (client->useragent && strlen(client->useragent) > 0) {
    free(worker->useragent);
    worker->useragent = strdup(client->useragent);
}
```

c) Include it in the per-worker stats JSON written to the users log (`statsupdate`,
via `yyjson_mut_pack_val`):
```c
"useragent", worker->useragent ? worker->useragent : "",
```

**Memory:** `worker_instance` objects persist for the process lifetime (there is no
worker-free path in ckpool; `worker->workername` is likewise never freed), so the
`strdup` on reconnect frees the prior value and does not leak.

---

### 4. Default Donation Address

**`src/ckpool.c`** (`main`): the compiled-in default solo-mining donation address
(used in `-B` solo mode when the operator does not override it) is set to AtlasPool's
address:
```c
ckpool.donaddress = "bc1q4mtk3hsnlfhh8475krdnf4hec86ue43eckywft";
```
Testnet/regtest donation addresses are left at upstream defaults.

---

## Build

Same as upstream (the capnp IPC shim builds when `capnp-rpc` / Cap'n Proto 1.1.0 is
present; `HAVE_CAPNP`-gated):
```bash
./autogen.sh
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig \
  ./configure --prefix=/usr/local CFLAGS='-O2 -march=armv8.2-a+crypto'
make -j$(nproc)
```
`configure` should report `IPC (mining Cap'n)...: yes`.

> **Built and validated on ARM (aarch64).** This build — and the four testnet4 blocks
> above — were produced on an **ARM64 system** (AWS Graviton), which is why the `CFLAGS`
> above use `-march=armv8.2-a+crypto`. **Build instructions may differ on Intel/x86-64
> systems:** that `-march` value is ARM-specific and will not compile on x86. Use an
> appropriate x86 target instead (e.g. `-march=native` on the build host, or
> `-march=x86-64-v2`/`-msse4.2` for a portable baseline). The rest of the build (autogen,
> configure flags, Cap'n Proto 1.1.0 requirement, dependencies) is architecture-independent;
> only the `-march` differs. The build has not been exercised on x86, so expect to adjust
> `-march` and verify the capnp toolchain paths on that platform.

---

## Maintenance

- Keep AtlasPool edits marked with `ATLASPOOL:` comments.
- To update to newer upstream: fetch upstream, create the next dated branch from the
  new base, and re-apply these four changes (re-locating against the new source, as was
  done here). Do not merge the stale branch forward.
- Update this document if a modification is added, removed, or changed.
