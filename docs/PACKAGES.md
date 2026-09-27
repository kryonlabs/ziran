# Packages and project imports

A Ziran application is a package with a `ziran.toml` and a committed
`ziran.lock`. Dependencies and the compiler are fetched from public HTTPS Git
repositories and pinned to full commits in the lock. `make install-user`
installs a bootstrap `ziran` command; project commands build and use the
compiler pinned by the application lock.

```toml
[package]
name = "Example"
entry = "src/app.zi"
module_roots = ["src"]

[toolchain]
git = "https://github.com/ziranlang/ziran.git"
ref = "master"

[dependencies.Kryon]
git = "https://github.com/kryonlabs/kryon.git"
ref = "master"
```

An exported module is a package-owned file, often `src/ui/Kryon/module.zi`:

```toml
[exports]
Kryon = "src/ui/Kryon/module.zi"
```

Applications import the public entry by its module name:

```zi
using UI :: #import "Kryon";
```

Each module resolves short imports in its own package, then its direct
dependencies, then the pinned standard library. Two direct dependencies that
export the same module name produce an error. Rename one at the dependency
declaration:

```toml
[dependencies.Graphics]
git = "https://example.com/graphics.git"
ref = "master"
module_aliases = { Drawing = "GraphicsDrawing" }
```

Package modules receive an internal identity derived from their exact Git
source and commit. Two versions of a transitive dependency can coexist in one
build. The identity is carried through checked IR, saved `.zir`, portable
bundles, and native output names. Source code continues to use short imports.

Commands:

```sh
ziran add Kryon --git https://github.com/kryonlabs/kryon.git
ziran lock
ziran update Kryon
ziran fetch
ziran check --project
ziran ir --project --entry app:main -o build/ir
ziran build --project --target=c --entry app:main -o build/c
ziran tool Kryon build
```

`--locked` requires a matching lock and ignores local development overrides;
`--offline` requires cached checkouts. The cache is under
`$XDG_CACHE_HOME/ziran` or `~/.cache/ziran`.

For local development, an ignored `ziran.local.toml` can map package names to
working directories:

```toml
[overrides]
Kryon = "../kryon"
ziran = "../ziran"
```

Production builds omit overrides and use the locked Git commits. A host module
that calls into the application can use an explicit reverse bridge:

```toml
[package]
bridge_modules = ["app"]
```

This makes only the named app module visible to direct dependencies. It does
not change the package's public exports. Native system libraries remain
ordinary platform build dependencies.
