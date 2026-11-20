# Stamp the service worker's cache name with a hash of the wasm, so each
# release replaces the last one's cache.
# $ cmake -DSW_IN=web/sw.js -DWASM=index.wasm -DOUT=sw.js -P stamp_sw.cmake
file(SHA256 "${WASM}" hash)
string(SUBSTRING "${hash}" 0 12 short)
file(READ "${SW_IN}" content)
string(REPLACE "@CACHE_VERSION@" "${short}" content "${content}")
file(WRITE "${OUT}" "${content}")
