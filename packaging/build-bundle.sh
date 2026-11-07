#!/bin/bash
# Builds Banana Player for the RG DS / RG DS Plus (aarch64) as a folder to copy onto the card, in an arm64 Debian 12
# container: Debian 12's glibc (2.36) is older than BananaOS's, so what's built here loads there.
#
#   packaging/build-bundle.sh            (on the host: runs itself in Docker)
#
# Out: dist/ports/BananaPlayer/ (bin, lib, share) and dist/ports/Banana Player.sh, ready for roms/ports on the card;
# dist/BananaPlayer-<version>-aarch64.zip with both.
#
# Bundled, built from source with only what playing needs: FFmpeg (decoders and demuxers), libplacebo, libass and
# libmpv, and the few libraries they pull in. Taken from BananaOS instead (not bundled): glibc, libstdc++, SDL2,
# SDL2_ttf, Mesa (EGL/GLES), libdrm, ALSA, FreeType, zlib, libpng: its SDL carries BananaOS's own patches
# (SDL_KMSDRM_FIRST picks the top panel), and its Mesa drives the Mali GPU.
set -euo pipefail

FFMPEG_VERSION=7.1.1
LIBPLACEBO_VERSION=v7.349.0
LIBASS_VERSION=0.17.3
MPV_VERSION=v0.40.0

HERE="$(cd "$(dirname "$0")/.." && pwd)"

if [ "${1:-}" != "--inside" ]; then
	mkdir -p "$HERE/dist" "$HERE/.cache/bundle"
	exec docker run --rm --platform linux/arm64 \
		-v "$HERE":/src -v "$HERE/.cache/bundle":/cache \
		debian:bookworm bash /src/packaging/build-bundle.sh --inside
fi

# ---- inside the container ----------------------------------------------------------------------------------------
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
	build-essential pkg-config cmake meson ninja-build nasm git ca-certificates curl xz-utils zip patchelf file \
	python3 python3-jinja2 python3-pip \
	libdav1d-dev libfreetype-dev libfribidi-dev libharfbuzz-dev libfontconfig-dev zlib1g-dev libasound2-dev \
	libsdl2-dev libsdl2-ttf-dev libegl-dev libgles-dev libdrm-dev >/dev/null

# mpv wants Meson 1.3 or newer; Debian 12 has 1.0
pip3 install -q --break-system-packages 'meson>=1.3' 2>&1 | grep -v "^WARNING" || true
hash -r

PREFIX=/opt/bp
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig
export LD_LIBRARY_PATH=$PREFIX/lib
SRC=/cache/src
mkdir -p $SRC $PREFIX
JOBS=$(nproc)

fetch() { # url dir
	[ -d "$SRC/$2" ] || { mkdir -p "$SRC/$2"; curl -sL "$1" | tar -x --strip-components=1 -C "$SRC/$2" -f - ${3:-}; }
}

echo "== FFmpeg $FFMPEG_VERSION"
fetch https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz ffmpeg-$FFMPEG_VERSION -J
cd $SRC/ffmpeg-$FFMPEG_VERSION
if [ ! -f $PREFIX/lib/libavcodec.so ]; then
	./configure --prefix=$PREFIX --enable-shared --disable-static --disable-programs --disable-doc \
		--disable-encoders --disable-muxers --disable-indevs --disable-outdevs --disable-network \
		--disable-debug --enable-libdav1d --enable-zlib --enable-pic >/dev/null
	make -j$JOBS >/dev/null && make install >/dev/null
fi

echo "== libplacebo $LIBPLACEBO_VERSION"
[ -d $SRC/libplacebo ] || git -c advice.detachedHead=false clone -q --depth 1 --branch $LIBPLACEBO_VERSION --recurse-submodules --shallow-submodules \
	https://code.videolan.org/videolan/libplacebo.git $SRC/libplacebo
if [ ! -f $PREFIX/lib/pkgconfig/libplacebo.pc ]; then
	cd $SRC/libplacebo
	rm -rf build
	meson setup build --prefix=$PREFIX --libdir=lib --buildtype=release -Dvulkan=disabled -Dopengl=enabled \
		-Dglslang=disabled -Dshaderc=disabled -Dlcms=disabled -Ddovi=disabled -Dlibdovi=disabled -Ddemos=false \
		-Dtests=false >/dev/null
	ninja -C build >/dev/null && ninja -C build install >/dev/null
fi

echo "== libass $LIBASS_VERSION"
fetch https://github.com/libass/libass/releases/download/$LIBASS_VERSION/libass-$LIBASS_VERSION.tar.xz libass-$LIBASS_VERSION -J
# with fontconfig: the one way libass falls back, character by character, to the font that has it (Korean, Thai,
# Arabic... in subtitles and titles). The bundled fonts.conf lists only the player's own fonts. (A libass from before
# that, without it, is built again, and mpv on it.)
if [ ! -f $PREFIX/lib/.libass-fontconfig ]; then
	rm -f $PREFIX/lib/libass.* $PREFIX/lib/libmpv.*
	cd $SRC/libass-$LIBASS_VERSION
	make distclean >/dev/null 2>&1 || true
	./configure --prefix=$PREFIX --disable-static --enable-fontconfig >/dev/null
	make -j$JOBS >/dev/null && make install >/dev/null && touch $PREFIX/lib/.libass-fontconfig
fi

echo "== mpv $MPV_VERSION"
[ -d $SRC/mpv ] || git -c advice.detachedHead=false clone -q --depth 1 --branch $MPV_VERSION https://github.com/mpv-player/mpv.git $SRC/mpv
if [ ! -f $PREFIX/lib/libmpv.so ]; then
	cd $SRC/mpv
	rm -rf build
	# libmpv only, drawn through its render API (plain-gl: no window system), sound to ALSA
	meson setup build --prefix=$PREFIX --libdir=lib --buildtype=release -Dlibmpv=true -Dcplayer=false \
		-Dauto_features=disabled -Dgl=enabled -Dplain-gl=enabled -Dalsa=enabled -Dzlib=enabled -Diconv=enabled \
		-Dlibavdevice=enabled 2>&1 | tail -40
	ninja -C build >/dev/null && ninja -C build install >/dev/null
fi

echo "== banana-player"
rm -rf /tmp/bp-build
cmake -S /src -B /tmp/bp-build -DCMAKE_BUILD_TYPE=Release -DBP_GLES=ON \
	-DCMAKE_INSTALL_RPATH='$ORIGIN/../lib' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON >/dev/null
cmake --build /tmp/bp-build -j$JOBS 2>&1 | grep -E "error|warning" || true
/tmp/bp-build/core-test

echo "== bundle"
VERSION=$(grep -oP 'project\(banana-player VERSION \K[0-9.]+' /src/CMakeLists.txt)
OUT=/src/dist/ports
B="$OUT/BananaPlayer"
rm -rf "$OUT" && mkdir -p "$B/bin" "$B/lib" "$B/share/banana-player"
cp /tmp/bp-build/banana-player "$B/bin/"
cp -r /src/assets/fonts "$B/share/banana-player/"
# fontconfig's view of the fonts: the player's folder only (the launcher sets FONTCONFIG_FILE), its cache in
# $XDG_CACHE_HOME (run-port: /data/home/.cache)
cat > "$B/share/banana-player/fonts.conf" <<'CONF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
	<dir prefix="relative">fonts</dir>
	<cachedir prefix="xdg">fontconfig</cachedir>
</fontconfig>
CONF
cp /src/LICENSE /src/README.md "$B/"
# the launcher (the Store's menu.launch, and what the Ports shelf's script runs), the version the Store reads
# (package.version_file) and the tile's picture (menu.media.image)
cp /src/packaging/launch.sh "$B/launch.sh"
echo "$VERSION" > "$B/VERSION"
python3 /src/packaging/icon/make-icon.py "$B/share/banana-player/icon.png" 16

# what the player loads that BananaOS doesn't have: libmpv and everything it needs, minus the system's own
SYSTEM='^(linux-vdso|ld-linux|libc\.|libm\.|libdl\.|libpthread\.|librt\.|libresolv\.|libstdc\+\+|libgcc_s|libSDL2|libasound|libfreetype|libz\.|libpng|libdrm|libEGL|libGLES|libGL\.|libgbm|libGLdispatch|libGLX|libX|libxcb|libwayland|libbz2|libbrotli)'
# a file's own NEEDED entries (not ldd's whole tree: a system library's needs are the system's), with where each is
needed() {
	local f=$1
	readelf -d "$f" | awk -F'[][]' '/NEEDED/ {print $2}' | while read -r name; do
		echo "$name $(ldd "$f" | awk -v n="$name" '$1 == n {print $3}')"
	done
}
declare -A seen
queue=("$B/bin/banana-player")
while [ ${#queue[@]} -gt 0 ]; do
	f=${queue[0]}; queue=("${queue[@]:1}")
	while read -r name path; do
		[[ "$name" =~ $SYSTEM ]] && continue
		[ -n "${seen[$name]:-}" ] && continue
		seen[$name]=1
		cp -L "$path" "$B/lib/$name"
		queue+=("$B/lib/$name")
	done < <(needed "$f")
done
# each bundled library finds its neighbours first
for l in "$B"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$l"; done
strip --strip-unneeded "$B/bin/banana-player" "$B"/lib/*.so* 2>/dev/null || true

cp /src/packaging/banana-player-port.sh "$OUT/Banana Player.sh"
# the shelf's picture: BananaOS looks for media/box/<entry's name>.png beside the entries
mkdir -p "$OUT/media/box"
python3 /src/packaging/icon/make-icon.py "$OUT/media/box/Banana Player.png" 16
chmod +x "$OUT/Banana Player.sh" "$B/launch.sh" "$B/bin/banana-player"

echo "bundled libraries:"; ls "$B/lib" | sed 's/^/  /'
du -sh "$B"
cd "$OUT" && rm -f /src/dist/BananaPlayer-$VERSION-aarch64.zip && zip -qr /src/dist/BananaPlayer-$VERSION-aarch64.zip "Banana Player.sh" BananaPlayer media
echo "done: dist/ports/ and dist/BananaPlayer-$VERSION-aarch64.zip"
