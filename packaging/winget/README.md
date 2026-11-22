# winget manifest

Template for submitting Polytsia to [winget-pkgs](https://github.com/microsoft/winget-pkgs).

1. Publish a release with a `vX.Y.Z` tag. The Build workflow uploads `Polytsia-X.Y.Z.exe` and prints its SHA-256 in the release notes.
2. Copy `Qahnaarin7.Polytsia.yaml`, replace `X.Y.Z` and `<SHA256>`, and validate it with `winget validate`.
3. Submit it with [wingetcreate](https://github.com/microsoft/winget-create): `wingetcreate submit <folder>`.

Polytsia is a portable app, so the manifest uses `InstallerType: portable`.
