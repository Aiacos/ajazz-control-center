<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

## Summary

<!-- A short description of what this PR does and why. -->

## Type of change

<!-- Tick the boxes that apply. -->

- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] New device support (protocol implementation)
- [ ] Breaking change (fix or feature that changes the public API / ABI / profile schema)
- [ ] Documentation only
- [ ] Build / CI / tooling

## Devices touched

<!-- Codenames from docs/_data/devices.yaml, e.g. akp03, akp153e, proprietary_keyboard. -->

## Checklist

- [ ] My commits follow [Conventional Commits](https://www.conventionalcommits.org).
- [ ] `pre-commit run --all-files` passes locally.
- [ ] `ctest --test-dir build` passes locally (or only doc / CI changes).
- [ ] If I added a new device, I updated `docs/_data/devices.yaml` and ran
  `python3 scripts/generate-docs.py`.
- [ ] I have not copied vendor code (clean-room policy — see CONTRIBUTING.md).
- [ ] If user-visible behavior changed, the docs / wiki have been updated.

## How was this tested?

<!-- Describe the tests you ran to verify your changes. Provide instructions
     so we can reproduce. List the hardware (if any) you used. -->

## Related issues

<!-- e.g. Closes #123, Refs #456. -->
