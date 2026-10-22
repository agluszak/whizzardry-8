@AGENTS.md

## Claude Code cloud sessions

The cloud environment's setup script (`tools/cloud-setup.sh`) installs the toolchain.
Before building, run `tools/cloud-deps.sh` once: it restores the Conan packages an
earlier session saved in the project's shared files (or builds and saves them) and
configures `build/linux`. A first build of the dependencies takes a long time, so run
it in the background. Then:

    . build/linux/conan/conanbuild.sh && cmake --build --preset linux
    . build/linux/conan/conanrun.sh && xvfb-run -a ctest --preset linux
