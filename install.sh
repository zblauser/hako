#!/usr/bin/env sh
# hako installer. Fetches latest GitHub release for current OS/arch.
# Usage:  curl -fsSL https://raw.githubusercontent.com/mithraeums/hako/main/install.sh | sh
# Env:    REPO=owner/hako        (override default)
#         PREFIX=/usr/local      (install dir; defaults to ~/.local if not writable)
#         VERIFY=0               (skip sha256 verify; default 1)

set -eu

REPO="${REPO:-mithraeums/hako}"
PREFIX="${PREFIX:-}"
VERIFY="${VERIFY:-1}"

uname_s="$(uname -s)"
uname_m="$(uname -m)"

case "$uname_s" in
	Linux*)
		asset="hako-linux.tar.gz"; dirname="hako-linux"; bin=hako; ext=tar.gz
		;;
	Darwin*)
		asset="hako-macos.tar.gz"; dirname="hako-macos"; bin=hako; ext=tar.gz
		;;
	MINGW*|MSYS*|CYGWIN*)
		asset="hako-windows.zip"; dirname="hako-windows"; bin=hako.exe; ext=zip
		;;
	*) echo "unsupported OS: $uname_s — try building from source (gcc -O2 -Wall hako.c -o hako -lpthread)" >&2; exit 1 ;;
esac

base="${REPO}"
api="https://api.github.com/repos/${base}/releases/latest"
echo "fetching latest release of ${base}..."

tag="$(curl -fsSL "$api" | grep -m1 '"tag_name":' | sed -E 's/.*"tag_name": *"([^"]+)".*/\1/' || true)"
if [ -z "$tag" ]; then
	echo "could not resolve latest tag (private repo? rate-limited?)" >&2
	exit 1
fi
echo "latest: $tag"

url="https://github.com/${base}/releases/download/${tag}/${asset}"
sha_name="${asset%.tar.gz}"
sha_name="${sha_name%.zip}.sha256"
sha_url="https://github.com/${base}/releases/download/${tag}/${sha_name}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "downloading $asset"
curl -fL --progress-bar -o "${tmp}/${asset}" "$url"

case "$ext" in
	tar.gz) tar -xzf "${tmp}/${asset}" -C "$tmp" ;;
	zip)    unzip -q "${tmp}/${asset}" -d "$tmp" ;;
esac

if [ "$VERIFY" = "1" ]; then
	echo "verifying sha256..."
	if curl -fsSL -o "${tmp}/${sha_name}" "$sha_url"; then
		(cd "${tmp}/${dirname}" && \
			if command -v sha256sum >/dev/null 2>&1; then sha256sum -c "../${sha_name}" >/dev/null; \
			elif command -v shasum    >/dev/null 2>&1; then shasum -a 256 -c "../${sha_name}" >/dev/null; \
			elif command -v sha256    >/dev/null 2>&1; then \
				want="$(awk '{print $1}' "../${sha_name}")"; \
				got="$(sha256 -q "$bin")"; \
				[ "$want" = "$got" ] || { echo "sha256 mismatch" >&2; exit 1; }; \
			else echo "no sha256 tool found, skipping verify" >&2; \
			fi) || { echo "sha256 verify FAILED" >&2; exit 1; }
		echo "sha256 ok."
	else
		echo "warning: sha sidecar missing — skipping verify (set VERIFY=0 to silence)" >&2
	fi
fi

if [ -z "$PREFIX" ]; then
	if [ -w "/usr/local/bin" ] || [ "$(id -u)" = "0" ]; then
		PREFIX=/usr/local
	else
		PREFIX="${HOME}/.local"
		mkdir -p "${PREFIX}/bin"
	fi
fi

install -m 0755 "${tmp}/${dirname}/${bin}" "${PREFIX}/bin/${bin}"

if [ "$uname_s" = "Darwin" ] && command -v xattr >/dev/null 2>&1; then
	xattr -d com.apple.quarantine "${PREFIX}/bin/${bin}" 2>/dev/null || true
fi

echo "installed: ${PREFIX}/bin/${bin}"
"${PREFIX}/bin/${bin}" --version || true

case ":${PATH}:" in
	*":${PREFIX}/bin:"*) ;;
	*) echo "note: ${PREFIX}/bin not in PATH. add to your shell rc:"
	   echo "    export PATH=\"${PREFIX}/bin:\$PATH\"" ;;
esac
