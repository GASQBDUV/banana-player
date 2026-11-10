#!/bin/bash
# End-to-end smoke test of the web dashboard's HTTP API against a RUNNING player, with curl and jq.
#
#   BP_WEB_FORCE=1 BP_WEB_PIN=ACDE BP_WEB_PORT=8080 BP_BROWSE=<library> ./build/banana-player --state-dir <scratch> \
#       --press "<many info steps so it stays up>"      # no file: the browser mode, where Stop closes the video
#   tools/web-smoke.sh [http://127.0.0.1:8080] [PIN]
#
# The library (root 0) needs "Music/Yellow Album/01 Sunny Peel.mp3" and "02 Split Decision.mp3". All it creates goes
# under one scratch folder in root 0, removed at the end. The wrong-PIN lockout runs last: it locks this machine out
# for a minute. Exit status 1 when anything failed.
set -u
BASE=${1:-http://127.0.0.1:8080}
PIN=${2:-ACDE}
SONG="0/Music/Yellow Album/01 Sunny Peel.mp3"
SONG2="0/Music/Yellow Album/02 Split Decision.mp3"
T=$(mktemp -d)
JAR=$T/jar
D="smoke-$$"
pass=0; fail=0; failed=()
CODE=0

command -v jq >/dev/null || { echo "needs jq"; exit 2; }
trap 'rm -rf "$T"' EXIT

ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); failed+=("$1"); echo "FAIL: $1"; }
check() { if [ "$2" = 0 ]; then ok; else bad "$1"; fi; }

# req METHOD PATH [curl args...]: body in $T/body, status in $CODE; sends the cookie and X-BP
req() {
	local m=$1 p=$2; shift 2
	CODE=$(curl -s -o "$T/body" -w '%{http_code}' -X "$m" -b "$JAR" -H 'X-BP: 1' "$@" "$BASE$p")
}
post() { local p=$1; shift; req POST "$p" "$@"; }
get() { local p=$1; shift; req GET "$p" "$@"; }
raw() { CODE=$(curl -s -o "$T/body" -w '%{http_code}' "$@"); }   # no cookie, no X-BP
status_is() { [ "$CODE" = "$2" ]; check "$1 (status $CODE, want $2: $(head -c 120 "$T/body"))" $?; }
status_in() { local n=$1; shift; local c; for c in "$@"; do [ "$CODE" = "$c" ] && { ok; return; }; done; bad "$n (status $CODE: $(head -c 120 "$T/body"))"; }
jq_is() { jq -e "$2" "$T/body" >/dev/null 2>&1; check "$1 ($2 on $(head -c 160 "$T/body"))" $?; }
state() { curl -s -b "$JAR" "$BASE/api/state"; }
wait_state() { # NAME FILTER [secs]
	local n=$((${3:-4} * 5))
	while [ $n -gt 0 ]; do
		state | jq -e "$2" >/dev/null 2>&1 && { ok; return; }
		sleep 0.2; n=$((n - 1))
	done
	bad "$1 (state: $(state | head -c 300))"
}

echo "== refusals before login"
raw "$BASE/api/state"; status_is "state signed out" 401
raw "$BASE/api/roots"; status_is "roots signed out" 401
raw -X POST -d pin=$PIN "$BASE/api/login"; status_is "login without X-BP" 403
raw -X POST -H 'X-BP: 1' -H 'Origin: http://evil.example' -d pin=$PIN "$BASE/api/login"; status_is "login with a foreign Origin" 403
raw -H 'Host: example.com' "$BASE/api/state"; status_is "Host that isn't an IP" 403
raw -X POST -H 'X-BP: 1' -H "Origin: $BASE" -d pin=$PIN -c "$JAR" "$BASE/api/login"; status_is "login: right PIN, own Origin" 200
grep -q bp_s "$JAR"; check "login sets bp_s" $?
grep -qi httponly "$JAR"; check "cookie is HttpOnly" $?
curl -s -i -b "$JAR" "$BASE/api/state" | tr -d '\r' | grep -qi '^cache-control: no-store'; check "state is no-store" $?

echo "== CSRF with a session"
raw -b "$JAR" -X POST -d c=pause "$BASE/api/cmd"; status_is "POST without X-BP" 403
raw -b "$JAR" -X POST -H 'X-BP: 1' -H 'Origin: http://evil.example' -d c=pause "$BASE/api/cmd"; status_is "POST with a foreign Origin" 403
raw -b "$JAR" -H 'Host: example.com' "$BASE/api/roots"; status_is "rebinding Host with a session" 403

echo "== static"
raw "$BASE/"; status_is "index without a session" 200
grep -q '<title>' "$T/body"; check "index is html" $?
raw "$BASE/app.js"; status_is "app.js" 200
et=$(curl -s -i "$BASE/app.js" | tr -d '\r' | awk 'tolower($1)=="etag:"{print $2}')
[ -n "$et" ]; check "app.js has an ETag" $?
raw -H "If-None-Match: $et" "$BASE/app.js"; status_is "app.js 304" 304
raw "$BASE/nope.txt"; status_is "unknown path" 404

echo "== roots / ls / media"
get /api/roots; status_is roots 200; jq_is "roots has a free size" '.roots[0].free > 0'
get /api/ls --get --data-urlencode "p=0"; status_is "ls root" 200; jq_is "ls lists folders" '.items | map(select(.dir)) | length > 0'
get /api/ls --get --data-urlencode "p=0/Music/Yellow Album"; status_is "ls album" 200
jq_is "ls item has kind and playable" '.items[] | select(.name=="01 Sunny Peel.mp3") | .kind == "audio" and .playable == "yes"'
for bp in "0/../etc" "0/.hidden" "7/x" "0//x" "/etc/passwd" "../x"; do
	get /api/ls --get --data-urlencode "p=$bp"
	status_in "ls refuses '$bp'" 400 403 404
done
get /api/media --get --data-urlencode "p=$SONG"; status_is media 200; jq_is "media: an mp3, playable" '.playable == "yes" and (.duration > 1)'
get /api/cover --get --data-urlencode "p=$SONG"; status_in "cover answers" 200 404

echo "== file with Range"
get /api/file --get --data-urlencode "p=$SONG" -D "$T/h"; status_is "file" 200
size=$(wc -c < "$T/body" | tr -d ' ')
grep -qi '^accept-ranges: bytes' "$T/h"; check "file: Accept-Ranges" $?
get /api/file --get --data-urlencode "p=$SONG" -H 'Range: bytes=10-19' -D "$T/h"; status_is "file range" 206
[ "$(wc -c < "$T/body" | tr -d ' ')" = 10 ]; check "range gives 10 bytes" $?
grep -qi "^content-range: bytes 10-19/$size" "$T/h"; check "Content-Range" $?
get /api/file --get --data-urlencode "p=$SONG" -H 'Range: bytes=-5'; status_is "suffix range" 206
get /api/file --get --data-urlencode "p=$SONG" -H "Range: bytes=$((size + 5))-"; status_is "range past the end" 416
get /api/file --get --data-urlencode "p=$SONG" --data-urlencode "dl=1" -D "$T/h"; grep -qi '^content-disposition: attachment' "$T/h"; check "dl=1 is an attachment" $?

echo "== upload / mkdir / rename / move / delete"
post /api/mkdir --data-urlencode "p=0" --data-urlencode "name=$D"; status_is "mkdir scratch" 200
printf 'hello-one' > "$T/a.mp3"; printf 'hello-two' > "$T/b.mp3"
req PUT "/api/upload?dir=0/$D&name=a.mp3" --data-binary @"$T/a.mp3"; status_is "upload a.mp3" 200
req PUT "/api/upload?dir=0/$D&name=a.mp3" --data-binary @"$T/a.mp3"; status_is "upload again: name taken" 409
req PUT "/api/upload?dir=0/$D&name=a.mp3&overwrite=1" --data-binary @"$T/b.mp3"; status_is "upload with overwrite" 200
get /api/file --get --data-urlencode "p=0/$D/a.mp3"; [ "$(cat "$T/body")" = "hello-two" ]; check "overwritten content" $?
req PUT "/api/upload?dir=0/$D&name=notes.exe" --data-binary @"$T/a.mp3"; status_is "upload a disallowed type" 415
req PUT "/api/upload?dir=0/$D&name=..%2Fx.mp3" --data-binary @"$T/a.mp3"; status_is "upload a name with /" 400
req PUT "/api/upload?dir=0/$D&name=c.mp3&rel=Album%20X/CD%201" --data-binary @"$T/a.mp3"; status_is "upload into a rel folder" 200
get /api/ls --get --data-urlencode "p=0/$D/Album X/CD 1"; status_is "rel folder exists" 200; jq_is "rel upload is there" '.items[0].name == "c.mp3"'
req PUT "/api/upload?dir=0/$D&name=d.mp3&rel=..%2Fout" --data-binary @"$T/a.mp3"; status_is "rel with .. refused" 400
req PUT "/api/upload?dir=0/$D&name=e.mp3" -H 'Transfer-Encoding: chunked' --data-binary @"$T/a.mp3"; status_in "chunked upload (no length)" 411 400
post /api/mkdir --data-urlencode "p=0/$D" --data-urlencode "name=Sub"; status_is "mkdir Sub" 200
post /api/mkdir --data-urlencode "p=0/$D" --data-urlencode "name=Sub"; status_is "mkdir existing" 409
post /api/mkdir --data-urlencode "p=0/$D" --data-urlencode "name=a/b"; status_is "mkdir with /" 400
post /api/rename --data-urlencode "p=0/$D/a.mp3" --data-urlencode "name=renamed.mp3"; status_is "rename" 200
post /api/rename --data-urlencode "p=0/$D/renamed.mp3" --data-urlencode "name=bad.exe"; status_in "rename to a disallowed type" 400 415
post /api/move --data-urlencode "p[]=0/$D/renamed.mp3" --data-urlencode "to=0/$D/Sub"; status_is "move" 200
get /api/ls --get --data-urlencode "p=0/$D/Sub"; jq_is "moved file is in Sub" '.items[0].name == "renamed.mp3"'
post /api/move --data-urlencode "p[]=0/$D/Sub" --data-urlencode "to=0/$D/Sub"; [ "$CODE" -ge 400 ]; check "move a folder into itself ($CODE)" $?
post /api/delete --data-urlencode "p[]=0/$D/Sub/renamed.mp3" --data-urlencode "p[]=0/$D/Album X"; status_is "delete a file and a folder" 200
get /api/ls --get --data-urlencode "p=0/$D"; jq_is "scratch holds only Sub" '[.items[].name] == ["Sub"]'
post /api/delete --data-urlencode "p[]=0"; [ "$CODE" -ge 400 ]; check "delete a root refused ($CODE)" $?

echo "== playlists"
post /api/playlists/create --data-urlencode "name=Smoke $$"; status_is "create" 200; ID=$(jq -r .id "$T/body")
[ -n "$ID" ] && [ "$ID" != null ]; check "create gives an id" $?
post /api/playlists/create --data-urlencode "name=smoke $$"; status_in "duplicate name (ignoring case) refused" 409 400
get /api/playlist --get --data-urlencode "id=$ID"; status_is "open empty" 200; REV=$(jq -r .rev "$T/body")
post /api/playlist/add --data-urlencode "id=$ID" --data-urlencode "rev=$REV" --data-urlencode "p[]=$SONG" --data-urlencode "p[]=$SONG2"; status_is "add two" 200
REV2=$(jq -r .rev "$T/body"); jq_is "add: count 2" '.count == 2'
post /api/playlist/add --data-urlencode "id=$ID" --data-urlencode "rev=$REV" --data-urlencode "p[]=$SONG"; status_is "add with a stale rev" 409
jq_is "stale: code rev + current rev" ".code == \"rev\" and .rev == \"$REV2\""
post /api/playlist/add --data-urlencode "id=$ID" --data-urlencode "rev=$REV2" --data-urlencode "p[]=$SONG"; status_is "add a duplicate" 200; REV3=$(jq -r .rev "$T/body")
post /api/playlist/dedupe --data-urlencode "id=$ID" --data-urlencode "rev=$REV3"; status_is "dedupe" 200; jq_is "dedupe leaves 2" '.count == 2'; REV4=$(jq -r .rev "$T/body")
post /api/playlist/move --data-urlencode "id=$ID" --data-urlencode "rev=$REV4" --data-urlencode "i[]=1" --data-urlencode "to=0"; status_is "move 1 to the top" 200; REV5=$(jq -r .rev "$T/body")
get /api/playlist --get --data-urlencode "id=$ID"; jq_is "order changed" '.items[0].title | test("Split")'
jq_is "items have a virtual p" '.items[0].p | startswith("0/")'
post /api/playlist/remove --data-urlencode "id=$ID" --data-urlencode "rev=$REV5" --data-urlencode "i[]=0"; status_is "remove one" 200
get /api/playlists; jq_is "list shows the playlist with 1" ".playlists[] | select(.id == \"$ID\") | .count == 1"
post /api/playlists/rename --data-urlencode "id=$ID" --data-urlencode "name=Smoke renamed $$"; status_is "rename playlist" 200; ID2=$(jq -r '.id // empty' "$T/body"); ID2=${ID2:-$ID}
get /api/playlists; jq_is "renamed in the list" ".playlists | map(.name) | index(\"Smoke renamed $$\") != null"
post /api/playlists/create --data-urlencode "name=Smoke play $$"; PLAY=$(jq -r .id "$T/body")
get /api/playlist --get --data-urlencode "id=$PLAY"; PR=$(jq -r .rev "$T/body")
post /api/playlist/add --data-urlencode "id=$PLAY" --data-urlencode "rev=$PR" --data-urlencode "p[]=$SONG" --data-urlencode "p[]=$SONG2"; status_is "add to the play playlist" 200

echo "== player, SSE"
post /api/cmd -d c=toggle; status_is "cmd with nothing playing" 409
post /api/cmd -d c=bogus; status_is "unknown cmd" 400
curl -s -N --max-time 14 -b "$JAR" "$BASE/api/events" > "$T/sse" &
SSEPID=$!
sleep 1
post /api/play --data-urlencode "p=$SONG"; status_is "play a file" 200
wait_state "state shows the song" '.path | endswith("01 Sunny Peel.mp3")'
wait_state "state: playing" '.paused == false and .duration > 1'
post /api/cmd -d c=pause; status_is "pause" 200; wait_state "state: paused" '.paused == true'
post /api/cmd -d c=play; status_is "play" 200; wait_state "state: playing again" '.paused == false'
post /api/cmd -d c=toggle; wait_state "toggle pauses" '.paused == true'; post /api/cmd -d c=toggle
post /api/playlist/add --data-urlencode "id=$PLAY" --data-urlencode "rev=$(curl -s -G -b "$JAR" --data-urlencode "id=$PLAY" "$BASE/api/playlist" | jq -r .rev)" --data-urlencode "p[]=$SONG"; status_is "a playlist edit while SSE listens" 200
post /api/cmd -d c=seek -d t=30; status_is "seek" 200; wait_state "position near 30" '(.position > 28 and .position < 45)'
post /api/cmd -d c=seekrel -d d=-10; status_is "seekrel" 200
post /api/cmd -d c=loop -d v=all; status_is "loop all" 200; wait_state "state loop all" '.loop == "all"'
post /api/cmd -d c=loop -d v=no; wait_state "state loop no" '.loop == "no"'
post /api/cmd -d c=loop -d v=sideways; status_is "bad loop" 400
post /api/cmd -d c=next; status_is "next" 200; wait_state "next: song 2" '.path | endswith("02 Split Decision.mp3")'
post /api/cmd -d c=prev; status_is "prev" 200; wait_state "prev: song 1" '.path | endswith("01 Sunny Peel.mp3")'
post /api/volume -d v=37; status_is "volume" 200
post /api/eq -d on=1 -d preset="Bass Boost"; status_is "eq on + preset" 200; wait_state "state eq on, Bass Boost" '.eq.on == true and .eq.preset == "Bass Boost" and .eq.gains[1] == 5'
post /api/eq --data-urlencode "gains=1,2,3,4,5,6,7,8,9,10"; status_is "eq gains" 200; wait_state "state eq gains" '.eq.gains[9] == 10'
post /api/eq --data-urlencode "gains=1,2"; status_is "eq with two gains" 400
post /api/eq -d preset=Nonexistent; status_in "unknown eq preset" 400 404
post /api/eq -d on=0 -d preset=Flat; status_is "eq off" 200
post /api/fx -d key=hall; status_in "fx hall" 200 400
get /api/eq; status_is "eq list" 200
get /api/source; status_is "source" 200; jq_is "source has the album" '.items | length >= 2'
post /api/play --data-urlencode "p[]=$SONG2" --data-urlencode "p[]=$SONG"; status_is "play a selection" 200; wait_state "selection plays song 2 first" '(.path | endswith("02 Split Decision.mp3")) and (.source.count == 2)'
post /api/play --data-urlencode "playlist=$PLAY" -d index=1; status_is "play a playlist at 1" 200; wait_state "playlist source at 1" '.source.kind == "playlist" and .source.index == 1'
post /api/cmd -d c=shuffle; status_is "shuffle" 200
post /api/play --data-urlencode "p=$SONG"; status_is "play a song again" 200; wait_state "the song again" '.path | endswith("01 Sunny Peel.mp3")'
post /api/browser-play; status_is "browser-play" 200; wait_state "state browserPlaying and paused" '.browserPlaying == true and .paused == true'
post /api/handoff --data-urlencode "p=$SONG" -d t=20; status_is "handoff to the handheld" 200; wait_state "handoff: playing from 20 s" '.paused == false and .position > 18'
wait_state "handoff clears browserPlaying" '.browserPlaying == false' 6
post /api/cmd -d c=stop; status_is "stop" 200; wait_state "state idle after stop" '.path == ""' 6
wait $SSEPID 2>/dev/null
[ -n "${SMOKE_KEEP_SSE:-}" ] && cp "$T/sse" "$SMOKE_KEEP_SSE"
n=$(grep -c '^event: state' "$T/sse"); [ "$n" -ge 6 ]; check "SSE saw >= 6 state events (got $n)" $?
grep '^data:' "$T/sse" | head -1 | sed 's/^data: *//' | jq -e '.eq' >/dev/null; check "SSE data is a snapshot" $?
grep -q 'Sunny Peel' "$T/sse"; check "SSE carried the song" $?
grep -q '^event: playlists' "$T/sse"; check "SSE saw a playlists event" $?
grep -q '^event: recent' "$T/sse"; check "SSE saw a recent event" $?

echo "== recent"
get /api/recent; status_is "recent" 200
jq_is "recent: song 1 newest, then song 2, as played" "(.items[0].p == \"$SONG\") and ([.items[].p] | index(\"$SONG2\")) != null and (.items[0].kind == \"audio\") and (.items[0].played == true)"
jq_is "recent: no state folder, nothing hidden" '[.items[].p | select(test("/\\."))] | length == 0'
post /api/recent/remove --data-urlencode "p=$SONG2"; status_is "remove from recent" 200
get /api/recent; jq_is "song 2 gone from recent" "[.items[].p] | index(\"$SONG2\") == null"
post /api/recent/remove --data-urlencode "p=$SONG2"; status_is "remove one not there" 404
raw -X POST -b "$JAR" "$BASE/api/recent/clear"; status_is "clear without X-BP" 403
post /api/recent/clear; status_is "clear recent" 200
get /api/recent; jq_is "recent empty after clear" '.items | length == 0'

echo "== cleanup"
get /api/playlist --get --data-urlencode "id=$ID2"; status_is "open renamed playlist" 200
post /api/playlists/delete --data-urlencode "id=$ID2"; status_is "delete playlist" 200
post /api/playlists/delete --data-urlencode "id=$PLAY"; status_is "delete play playlist" 200
get /api/playlist --get --data-urlencode "id=$ID2"; status_is "deleted playlist is gone" 404
post /api/delete --data-urlencode "p[]=0/$D"; status_is "remove the scratch folder" 200
get /api/ls --get --data-urlencode "p=0"; jq_is "scratch folder gone" "[.items[].name] | index(\"$D\") == null"

echo "== logout, then lockout"
post /api/logout; status_is logout 200
raw -b "$JAR" "$BASE/api/state"; status_is "state after logout" 401
for i in 1 2 3 4; do raw -X POST -H 'X-BP: 1' -d pin=WWWW "$BASE/api/login"; done
status_is "4th wrong PIN is a plain 401" 401
jq_is "401 carries tries left" '.left == 1'
raw -X POST -H 'X-BP: 1' -d pin=WWWW "$BASE/api/login"
raw -X POST -H 'X-BP: 1' -d pin=$PIN "$BASE/api/login"
status_in "right PIN during lockout is refused" 429 401
jq_is "lockout says how long" '.retryAfter > 0'

echo
echo "web-smoke: $pass passed, $fail failed"
[ $fail = 0 ] || { printf ' - %s\n' "${failed[@]}"; exit 1; }
