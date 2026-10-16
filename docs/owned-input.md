# Explicit owned encoded-input application

This optional source application consumes an already configured `/dev/nt98513-capture` endpoint. It does not identify a HAL, initialize an SDK/sensor/ISP, create the endpoint, provide a keeper, or establish physical capture. The existing default build and release workflow are unchanged.

From a normal Linux checkout, with a C11 compiler, pthreads, make and Python 3.9+:

```
make -C src divinus-owned CC=cc
./owned-build/divinus-owned --input nt98513-owned --config /absolute/owned.conf
```

Copy `owned-input.conf.example` to a private regular file owned by the effective UID, mode0600. Set all five required keys: IPv4 `bind`, `port`, nonempty printable `user` and `password` (maximum31 bytes), and `timestamp=linux-monotonic-arrival`. The username cannot contain a colon. The empty example intentionally fails. Unknown keys/capabilities fail before startup. This mode supports authenticated TCP-interleaved H.264 RTSP plus RTCP, not UDP/audio/HTTP/ONVIF/IDR or other SDK controls. Basic authentication needs a protected network/transport; it supplies no encryption.

STARTING means awaiting input; LISTEN appears only after configuration/SPS/PPS seeding and authenticated listener startup. Generation replacement disconnects clients and clears caches; clients reconnect/re-DESCRIBE and wait for natural IDRs. Timestamps are Linux monotonic read-arrival, not sensor PTS. Normal peer failures drop only the peer. Signals INT/TERM/QUIT/HUP stop and join; there is no automatic restart. If join fails, the process exits terminally before freeing possibly live contexts. Synchronous callbacks are not forcibly interrupted. Zero-client success does not prove delivery.

## Self-contained verification

```
python3 tools/test_owned_input.py --out /absolute/new-results --cc gcc --rtsp
python3 tools/test_owned_input.py --out /absolute/other-results --cc gcc --rtsp --ffmpeg /usr/bin/ffmpeg --ffprobe /usr/bin/ffprobe
```

Output directories must be new. Tests execute real main/session/POSIX/listener/parser/packetizer/shutdown and newly owned localhost/socketpair peers. Only capture-device open/read/ioctl is substituted. They never open camera hardware. Native and ASan/UBSan, exact framing, legacy auth/framing, RTCP, configuration, stop/error/join-failure and parallel production/fixture goals run. FFmpeg compares actual decoded pixels to the included authored fixture. Host tools must already be installed; these commands download nothing. Linux is the main runner platform; `tests/host-compat` provides primitive UAPI definitions for a separately supplied macOS host fixture build, not a target ABI replacement.

For the locally validated Buildroot12.4 toolchain, add:

```
--mips-cc /absolute/host/bin/mipsel-openipc-linux-musl-gcc --toolchain-pin tools/owned_mips_toolchain_pin.json
```

The runner checks wrapper/compiler/cc1/linker hashes, compiles statically and inspects MIPS32r2/o32/soft-float ELF and production symbols. It never executes target code. The pin is provenance for an explicitly supplied compiler, not a download instruction or promise of a public toolchain archive. Sysroot/runtime support and local builder identities are recorded in integration evidence separately. Hosted cross-build remains unavailable until a reproducible verified toolchain source is selected.

## Source identity and provenance

`tools/owned_sources.json` names exactly39 production files/12C units. It includes build/generator metadata; all paths are repository-relative. The generator rejects missing/unsafe paths and the build refuses an external dependency override. Dependency files are vendored under `third_party/nt98513-owned-capture`, with original includes and hashes in `PROVENANCE.json`. No sibling repository, submodule, firmware blob or SDK archive is needed. Atomic header generation supports `make -j4 -C src divinus-owned divinus-owned-fixture`; use separate output directories for independent builds with different flags. Generated binaries and compiler flags are separate from source identity.

The source adaptation derives from corrected owner-fork base `b47abc428d6058731fbc256fe73e05e118d61326` and accepted application ID `f356a72541d0c0102de9b6490caa1f058604f696f098df21bc84932790162848`. Layout/build changes deliberately mint a new ID rather than reusing that acceptance. This repository target is not the legacy `build.sh mips-musl` Ingenic build.

Keep root LICENSE/COPYING and `licenses/` notices. Session/reader/wire are GPL-2.0-only, UAPI GPL-2.0 WITH Linux-syscall-note, parser/bridge/runtime/RTCP MIT. The owned executable is not described as MIT-only. The inherited default-mode third-party notices remain intact.

The new test-only workflow uses a commit-pinned checkout action and Ubuntu24.04 supplied GCC13/Python/FFmpeg, records actual versions, and grants read-only contents permission. The hosted image is rolling; hosted jobs are not claimed reproducible or locally executed. No release step or private compiler URL is added. Existing `build.yml` release behavior is untouched. Full legacy platform matrix, hosted CI, package migration and hardware acceptance require their own validation.
