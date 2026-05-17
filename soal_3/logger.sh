#!/bin/sh
set -eu

RAW_LOG="${RAW_LOG:-/logs/samba_raw.log}"
OUT_LOG="${OUT_LOG:-/logs/libraryit.log}"

mkdir -p "$(dirname "$RAW_LOG")" "$(dirname "$OUT_LOG")"
touch "$RAW_LOG" "$OUT_LOG"

tail -n0 -F "$RAW_LOG" | awk '
function trim(s) {
    gsub(/^[[:space:]]+/, "", s)
    gsub(/[[:space:]]+$/, "", s)
    return s
}

function action_for(op, level, access,   op_l, a) {
    op_l = tolower(op)

    if (op_l == "connect") return "CONNECT"
    if (op_l == "disconnect") return "DISCONNECT"

    if (level == "WARNING") return "DENIED"

    if (op_l == "openat") {
        if (index(access, "w") > 0 || index(access, "a") > 0) return "WRITE"
        return "READ"
    }

    if (op_l == "create_file" || op_l == "pwrite" || op_l == "write" || op_l == "mkdirat" ||
        op_l == "renameat" || op_l == "unlinkat" || op_l == "rmdir" || op_l == "ftruncate") {
        return "WRITE"
    }

    if (op_l == "read" || op_l == "pread" || op_l == "readdir" || op_l == "fdopendir") {
        return "READ"
    }

    a = op
    gsub(/[^[:alnum:]_]/, "_", a)
    return toupper(a)
}

function target_for(share, n, fields,   i, t) {
    for (i = n; i >= 1; i--) {
        t = trim(fields[i])
        if (t == "") continue
        if (t == "r" || t == "w" || t == "rw" || t == "a") continue
        if (t ~ /^0x[0-9A-Fa-f]+$/) continue
        if (t ~ /^fail \(/) continue
        if (t ~ /^ok$/) continue
        return t
    }
    return share
}

/^\[[0-9]{4}\/[0-9]{2}\/[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+, *[0-9]+\]/ {
    ts = substr($0, 2, 19)
    gsub(/\//, "-", ts)
    have_ts = 1
    next
}

have_ts && /^[[:space:]]*[^[:space:]].*\|/ {
    line = trim($0)
    n = split(line, f, "|")
    if (n < 4) next

    user = trim(f[1])
    share = trim(f[2])
    op = trim(f[3])
    result = trim(f[4])
    access = (n >= 5 ? tolower(trim(f[5])) : "")

    level = (result ~ /^fail/ ? "WARNING" : "INFO")
    action = action_for(op, level, access)
    target = target_for(share, n, f)

    print "[" ts "] [" level "] [" user "] [" action "] [" target "]"
    fflush()
    next
}
' | tee -a "$OUT_LOG"
