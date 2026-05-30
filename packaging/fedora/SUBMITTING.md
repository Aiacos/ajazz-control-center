# Submitting AJAZZ Control Center to Fedora

This document covers two ways to ship the RPM defined in
[`ajazz-control-center.spec`](./ajazz-control-center.spec):

- **Path A — Copr** (recommended first step): a self-service build service.
  No sponsorship, no review, available immediately. Users install with
  `dnf copr enable`.
- **Path B — Official Fedora repositories**: the full package-review +
  dist-git + Koji + Bodhi pipeline. Requires a sponsor and is much slower, but
  lands the package in the default Fedora repos.

Both paths consume the same `.spec` and the same source RPM (SRPM).

All `docs.fedoraproject.org` links below are the canonical sources for each
step.

______________________________________________________________________

## Prerequisites (both paths)

Install the packaging toolchain on a Fedora box:

```bash
sudo dnf install rpm-build rpmdevtools rpmlint \
                 fedora-packager fedora-review copr-cli
rpmdev-setuptree   # creates ~/rpmbuild/{SOURCES,SPECS,RPMS,SRPMS,BUILD}
```

### Build the SRPM

The spec fetches the upstream tag tarball as `Source0`. Download it, drop it in
`SOURCES`, and build the source RPM:

```bash
spectool -g -R packaging/fedora/ajazz-control-center.spec    # downloads Source0 into ~/rpmbuild/SOURCES
rpmbuild -bs packaging/fedora/ajazz-control-center.spec       # produces the .src.rpm
```

The resulting SRPM is written to
`~/rpmbuild/SRPMS/ajazz-control-center-0.1.0-1.fc*.src.rpm`.

> The `Source0` SHA-256 for `v0.1.0` is
> `9ec5342db63a905e3ab97d6596a053fd25e0b0142efdad6f09e293429613fe7f`
> (`sha256sum` of the GitHub tag tarball). Confirm it matches what `spectool`
> downloaded before building.

### Lint and a local test build

```bash
rpmlint packaging/fedora/ajazz-control-center.spec
# Full sandboxed build for the current release (catches missing BuildRequires):
mock -r fedora-rawhide-x86_64 ~/rpmbuild/SRPMS/ajazz-control-center-0.1.0-1.*.src.rpm
```

`mock` build configs:
<https://docs.fedoraproject.org/en-US/package-maintainers/Using_Mock_to_test_package_builds/>

______________________________________________________________________

## Path A — Copr (self-service, immediate)

Copr is Fedora's "Cool Other Package Repositories" build service. Anyone with a
Fedora Account System (FAS) login can build RPMs for multiple Fedora/EPEL
releases and architectures and publish a `dnf`-enableable repo. No review, no
sponsor.

Docs:

- Copr user docs: <https://docs.pagure.org/copr.copr/user_documentation.html>
- copr-cli quick reference: <https://developer.fedoraproject.org/deployment/copr/copr-cli.html>

### A1. One-time setup

1. Log in at <https://copr.fedorainfracloud.org/> with your FAS account.
1. Open <https://copr.fedorainfracloud.org/api/> and copy the generated token
   block into `~/.config/copr`.

```bash
sudo dnf install copr-cli
$EDITOR ~/.config/copr     # paste the [copr-cli] token block from the API page
```

### A2. Create the project

Pick the chroots (release × arch) you want to build for. Example: current
stable Fedora releases + Rawhide, x86_64 + aarch64:

```bash
copr-cli create ajazz-control-center \
    --chroot fedora-40-x86_64 \
    --chroot fedora-41-x86_64 \
    --chroot fedora-rawhide-x86_64 \
    --chroot fedora-40-aarch64 \
    --chroot fedora-41-aarch64 \
    --description "Control center for AJAZZ stream decks, keyboards and mice" \
    --instructions "sudo dnf copr enable aiacos/ajazz-control-center && sudo dnf install ajazz-control-center"
```

(Project name becomes `aiacos/ajazz-control-center` under your FAS username.)

### A3. Build

Upload the SRPM you built above, or point Copr at a URL:

```bash
# From the local SRPM:
copr-cli build ajazz-control-center \
    ~/rpmbuild/SRPMS/ajazz-control-center-0.1.0-1.*.src.rpm

# Or from a URL (e.g. a GitHub release-attached SRPM):
copr-cli build ajazz-control-center \
    https://github.com/Aiacos/ajazz-control-center/releases/download/v0.1.0/ajazz-control-center-0.1.0-1.src.rpm
```

`copr-cli build` streams progress; Ctrl+C is safe (it does not cancel the
build). Copr also supports building straight from a dist-git/SCM checkout
(`copr-cli buildscm ...`) and automatic rebuilds on a Git webhook if you prefer
not to upload SRPMs by hand.

### A4. Tell users how to install

```bash
sudo dnf install dnf-plugins-core      # provides the 'copr' plugin (usually preinstalled)
sudo dnf copr enable aiacos/ajazz-control-center
sudo dnf install ajazz-control-center
```

Put exactly those three lines in the project README's install section.

______________________________________________________________________

## Path B — Official Fedora repositories (review + sponsorship)

This is the long route: a human reviewer must approve the package against the
Fedora Packaging Guidelines, and because you have no package in Fedora yet you
must also be **sponsored** into the `packager` group. Plan on days-to-weeks of
back-and-forth.

Authoritative process docs:

- New Package Process for New Contributors:
  <https://docs.fedoraproject.org/en-US/package-maintainers/New_Package_Process_for_New_Contributors/>
- Package Review Process:
  <https://docs.fedoraproject.org/en-US/package-maintainers/Package_Review_Process/>
- Review Guidelines (the checklist reviewers use):
  <https://fedoraproject.org/wiki/Packaging:ReviewGuidelines>
- Packaging Guidelines (the rules the package must satisfy):
  <https://docs.fedoraproject.org/en-US/packaging-guidelines/>
- How to Get Sponsored into the Packager Group:
  <https://docs.fedoraproject.org/en-US/package-maintainers/How_to_Get_Sponsored_into_the_Packager_Group/>

### B1. Accounts

1. Create a Fedora Account System (FAS) account:
   <https://accounts.fedoraproject.org/> and sign the contributor agreement.
1. Create a Red Hat Bugzilla account using the **same email** as FAS:
   <https://bugzilla.redhat.com/> (reviews are filed there).

### B2. Self-check against the guidelines

1. Build the SRPM (see Prerequisites) and run a clean mock build for Rawhide
   plus the current stable release.

1. Run the automated reviewer locally and read its report:

   ```bash
   fedora-review -n ajazz-control-center \
       -m fedora-rawhide-x86_64        # or run it against the bug later with -b <BUG_ID>
   ```

1. Run `rpmlint` on **both** the SRPM and every produced binary RPM; paste the
   output (and explain any remaining warnings) in the review.

### B3. File the review request

1. Host the `.spec` and the `.src.rpm` at public URLs (e.g. attach them to a
   GitHub release, or use Copr's build output URLs from Path A).

1. File a **Package Review** bug on Red Hat Bugzilla:

   - Product: **Fedora**, Component: **Package Review**.
   - Summary: `Review Request: ajazz-control-center - Cross-platform control center for AJAZZ devices`
   - Include the Spec URL, SRPM URL, a short description, the Copr/koji
     scratch-build link, and your `rpmlint` output.

1. Because you are not yet a packager, mark the bug as **blocking
   `FE-NEEDSPONSOR`** (Bugzilla bug id **177841**) so a sponsor can find it:
   <https://bugzilla.redhat.com/show_bug.cgi?id=177841>

1. (Recommended) Submit a Koji **scratch build** to prove it builds in the real
   buildsystem and link it in the bug:

   ```bash
   koji build --scratch rawhide ~/rpmbuild/SRPMS/ajazz-control-center-0.1.0-1.*.src.rpm
   ```

### B4. Review + sponsorship

- A reviewer takes the bug (sets the `fedora-review` flag to `?` then `+` on
  approval) and walks the
  [Review Guidelines](https://fedoraproject.org/wiki/Packaging:ReviewGuidelines)
  checklist (license correct + `%license`, no bundled libs — note we use
  `-DAJAZZ_USE_SYSTEM_DEPS=ON` precisely so hidapi/json are **not** bundled,
  `desktop-file-validate`, AppStream validation, sane `BuildRequires`, etc.).
- A **sponsor** must add you to the `packager` group. Helping with other
  reviews and being responsive on the bug speeds this up. Sponsorship is not
  automatic.

### B5. After approval — create the dist-git repo and build

Once `fedora-review +` is set and you are sponsored:

```bash
# 1. Request the dist-git repository (needs a Pagure API token; -c <bug>):
fedpkg request-repo ajazz-control-center <REVIEW_BUG_ID>

# 2. After releng creates it, clone and import the approved SRPM:
fedpkg clone ajazz-control-center
cd ajazz-control-center
fedpkg import ~/rpmbuild/SRPMS/ajazz-control-center-0.1.0-1.*.src.rpm
git commit -m "Initial import (#<REVIEW_BUG_ID>)"
git push

# 3. Build for Rawhide (the default branch):
fedpkg build

# 4. Request branches for stable releases, then build + ship via Bodhi:
fedpkg request-branch f41          # opens a releng request; repeat per release
git switch f41 && git merge rawhide && git push
fedpkg build
fedpkg update                      # creates the Bodhi update for testing -> stable
```

- `fedpkg`/dist-git workflow:
  <https://docs.fedoraproject.org/en-US/package-maintainers/Package_Maintenance_Guide/>
- Bodhi (updates) docs: <https://docs.fedoraproject.org/en-US/bodhi/>

Rawhide does not need a Bodhi update; branched stable releases (e.g. F41) do.

______________________________________________________________________

## Per-release update checklist

Run this every time upstream tags a new `vX.Y.Z`.

1. **Bump the spec** in `packaging/fedora/ajazz-control-center.spec`:
   - Set `Version:` to the new upstream version.
   - Reset `Release:` to `1%{?dist}`.
   - Add a new `%changelog` entry **at the top** (newest first), dated, with
     your name/email and a one-line summary. Keep older entries.
1. **Refresh the source + checksum:**
   ```bash
   spectool -g -R packaging/fedora/ajazz-control-center.spec
   sha256sum ~/rpmbuild/SOURCES/ajazz-control-center-X.Y.Z.tar.gz
   ```
   Update the SHA-256 noted in this file's Prerequisites section.
1. **Rebuild + lint:**
   ```bash
   rpmbuild -bs packaging/fedora/ajazz-control-center.spec
   rpmlint packaging/fedora/ajazz-control-center.spec
   mock -r fedora-rawhide-x86_64 ~/rpmbuild/SRPMS/ajazz-control-center-X.Y.Z-1.*.src.rpm
   ```
   Watch for new Qt point-release warnings and any new/dropped `BuildRequires`
   (e.g. a new Qt module pulled in by upstream).
1. **Copr (Path A):**
   ```bash
   copr-cli build ajazz-control-center \
       ~/rpmbuild/SRPMS/ajazz-control-center-X.Y.Z-1.*.src.rpm
   ```
1. **Official Fedora (Path B), if maintaining there:**
   ```bash
   cd <dist-git clone>
   git switch rawhide
   spectool -g -R *.spec           # download new tarball
   fedpkg new-sources ajazz-control-center-X.Y.Z.tar.gz   # upload to the lookaside cache
   git commit -am "Update to X.Y.Z"
   git push && fedpkg build
   # then for each stable branch:
   git switch fNN && git merge rawhide && git push && fedpkg build && fedpkg update
   ```
1. **Only when packaging itself changes** (not a version bump): bump `Release:`
   and add a `%changelog` entry explaining the packaging change.

> Note on `BuildRequires`/options that are easy to get wrong on a bump:
> the build must keep `-DAJAZZ_USE_SYSTEM_DEPS=ON` (no bundled hidapi/json —
> Fedora forbids bundling), `-DAJAZZ_BUILD_TESTS=OFF`, and
> `-DAJAZZ_ENABLE_WERROR=OFF`. The udev rule installs to `%{_udevrulesdir}`
> and the metainfo file keeps its reverse-DNS name
> (`io.github.Aiacos.AjazzControlCenter.appdata.xml`).
