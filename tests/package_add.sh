#!/bin/sh
set -eu
launcher=${1:?pass ziran launcher}
native=$(dirname "$launcher")/ziran-add
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
cat > "$fixture/ziran.toml" <<'EOF'
[package]
name = "consumer"

[toolchain]
git = "https://github.com/ziranlang/ziran.git"
ref = "master"
EOF
cat > "$fixture/ok" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$fixture/fail" <<'EOF'
#!/bin/sh
exit 1
EOF
chmod +x "$fixture/ok" "$fixture/fail"
cd "$fixture"
"$native" "$fixture/ok" kryonlabs/plot
grep -q '^\[dependencies.plot\]$' ziran.toml
grep -q '^git = "https://github.com/kryonlabs/plot.git"$' ziran.toml
cp ziran.toml saved.toml
if "$native" "$fixture/ok" kryonlabs/plot >/dev/null 2>&1; then
    echo 'duplicate add unexpectedly succeeded' >&2
    exit 1
fi
cmp saved.toml ziran.toml
if "$native" "$fixture/fail" kryonlabs/kryon >/dev/null 2>&1; then
    echo 'failed lock unexpectedly succeeded' >&2
    exit 1
fi
cmp saved.toml ziran.toml
"$native" "$fixture/ok" owner/chart-tools --ref v1.2
grep -q '^\[dependencies.chart_tools\]$' ziran.toml
grep -q '^ref = "v1.2"$' ziran.toml
