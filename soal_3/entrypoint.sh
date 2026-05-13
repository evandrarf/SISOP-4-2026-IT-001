#!/bin/sh
set -eu

ensure_group() {
    name="$1"
    gid="$2"
    if getent group "$name" >/dev/null 2>&1; then
        return
    fi
    if getent group "$gid" >/dev/null 2>&1; then
        groupadd "$name"
    else
        groupadd -g "$gid" "$name"
    fi
}

ensure_user() {
    name="$1"
    uid="$2"
    primary_group="$3"
    password="$4"

    if ! id "$name" >/dev/null 2>&1; then
        if getent passwd "$uid" >/dev/null 2>&1; then
            useradd -M -N -s /usr/sbin/nologin -g "$primary_group" "$name"
        else
            useradd -M -N -s /usr/sbin/nologin -u "$uid" -g "$primary_group" "$name"
        fi
    fi

    echo "$name:$password" | chpasswd
    printf '%s\n%s\n' "$password" "$password" | smbpasswd -a -s "$name" >/dev/null 2>&1 || true
    smbpasswd -e "$name" >/dev/null 2>&1 || true
}

ensure_group readonly 1000
# Ubuntu biasanya sudah punya group staff dengan gid 50.
if ! getent group staff >/dev/null 2>&1; then
    groupadd -g 50 staff
fi

ensure_user member 1000 readonly member123
ensure_user contributor 1001 staff contrib456
ensure_user librarian 1002 staff lib789

usermod -aG readonly member
usermod -aG staff contributor
usermod -aG staff librarian

mkdir -p /libraryit/ebooks /libraryit/papers /libraryit/sourcecode /libraryit/docs
mkdir -p /logs

touch /logs/libraryit.log
chmod 664 /logs/libraryit.log

# ebooks & papers: staff rw, readonly r.
chown root:staff /libraryit/ebooks /libraryit/papers
chmod 2770 /libraryit/ebooks /libraryit/papers
setfacl -m g:readonly:rx /libraryit/ebooks /libraryit/papers
setfacl -d -m g:readonly:rx /libraryit/ebooks /libraryit/papers
setfacl -d -m g:staff:rwx /libraryit/ebooks /libraryit/papers

# sourcecode di host harus 750 dan hanya owner+group yang bisa akses.
chown root:staff /libraryit/sourcecode
chmod 750 /libraryit/sourcecode

# docs tidak bisa dimodifikasi langsung dari host user biasa.
# Librarian tetap bisa write lewat Samba.
chown root:staff /libraryit/docs
chmod 550 /libraryit/docs
setfacl -m u:librarian:rwx /libraryit/docs
setfacl -m g:readonly:rx /libraryit/docs
setfacl -m g:staff:rx /libraryit/docs
setfacl -d -m u:librarian:rwx /libraryit/docs
setfacl -d -m g:readonly:rx /libraryit/docs
setfacl -d -m g:staff:rx /libraryit/docs

testparm -s >/dev/null

exec smbd --foreground --no-process-group --configfile=/etc/samba/smb.conf
