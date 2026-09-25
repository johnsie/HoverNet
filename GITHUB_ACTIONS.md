# GitHub Actions builds

The GitHub workflow in `.github/workflows/build.yml` builds and tests HoverNet without replacing the existing GitLab pipeline.

## Produced packages

Every push, pull request, and manual run produces two downloadable workflow artifacts:

- `hovernet-linux-packages` contains the Ubuntu/Debian packages for the game client and race server.
- `hovernet-windows-installer` contains the Windows game setup executable.

The Linux game and race server are built through the repository's CMake project and the complete test suite is run before the DEB packages are created. The game DEB includes the tracks. The Windows client is built as a 32-bit Release application, with SDL2 and the existing runtime assets included by the Inno Setup installer.

## Releases

Pushing a tag whose name starts with `v`, for example `v0.2.0`, runs both platform builds and creates a GitHub Release containing both DEBs and the Windows setup EXE.

The repository's GitHub Actions setting must allow workflows to create releases. Under **Settings > Actions > General > Workflow permissions**, select **Read and write permissions** if the organisation does not already grant this to the release job.

## Deployment

Production deployment remains in GitLab. This keeps the existing protected GitLab runner, environment approvals, and `/usr/local/sbin/hovernet-deploy` privilege boundary unchanged. The packages built by GitHub have the same layout and use the same Debian packaging scripts, so a protected GitHub deployment can be added later after a GitHub self-hosted runner has been registered on the race server.
