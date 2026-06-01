# Flatpak packaging

`io.github.Aiacos.AjazzControlCenter.yml` is the Flatpak manifest. It builds
offline (the `flatpak-builder` sandbox has no network), so every dependency is
provided as a pinned module or a vendored source list.

## Stream Dock mirajazz sidecar (`streamdock-host`)

The AKP03 / AKP05-N4 / AKP153 Stream Dock families are driven by the
out-of-process Rust sidecar `streamdock-host/`. Because the sandbox is offline,
the in-CMake cargo build is disabled for Flatpak (`-DAJAZZ_BUILD_SIDECAR=OFF` on
the app module) and the sidecar is built by a dedicated `streamdock-host` module
that compiles **offline** from `cargo-sources.json` using the
`org.freedesktop.Sdk.Extension.rust-stable` SDK extension. The binary installs to
`/app/bin/streamdock-host`, which the app resolves via
`QCoreApplication::applicationDirPath()`.

### Regenerating `cargo-sources.json`

`cargo-sources.json` is a **generated** vendored-dependency list derived from
`streamdock-host/Cargo.lock`. Regenerate it whenever the Rust dependencies (or
the pinned `mirajazz` git revision) change:

```bash
# flatpak-cargo-generator.py lives in flatpak/flatpak-builder-tools/cargo
# (deps: aiohttp, toml). It needs network access to resolve the git deps.
python3 flatpak-cargo-generator.py streamdock-host/Cargo.lock \
  -o packaging/flatpak/cargo-sources.json
```

Commit the regenerated file alongside the `Cargo.lock` change so the offline
Flatpak build stays in sync.

## Building locally

```bash
flatpak-builder --user --install --force-clean build-flatpak \
  packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml
```
