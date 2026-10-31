#!/bin/sh
# Runs the SFTP integration tests (tst_sftp) against two throwaway servers on
# 127.0.0.1:
#   - OpenSSH's sshd, started as the current user with its own host key and
#     config (no root needed), for key logins and real file operations;
#   - tests/sftp/hostile_server.py (needs Python 3 with paramiko), which
#     misbehaves on purpose.
#
# Usage: run-integration-tests.sh BUILD_DIR [PYTHON] [test arguments...]
# The hostile tests are skipped if PYTHON can't import paramiko.
#
# On a machine without a usable sshd, a Docker server such as
# linuxserver/openssh-server works too: set the TEXTEDITOR_TEST_* variables
# described in tests/tst_sftp.cpp yourself and run tst_sftp directly. The
# server's folder must be bind-mounted so the tests can see it locally.
set -eu

build=${1:?usage: run-integration-tests.sh BUILD_DIR [PYTHON] [test arguments...]}
python=${2:-python3}
shift
[ $# -gt 0 ] && shift
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/texteditor-sftp.XXXXXX")
sshd_port=${SSHD_PORT:-22222}
hostile_port=${HOSTILE_PORT:-22223}

cleanup() {
    for pid in "$work"/*.pid; do
        [ -f "$pid" ] && kill "$(cat "$pid")" 2>/dev/null || true
    done
    [ -n "${hostile_pid:-}" ] && kill "$hostile_pid" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

ssh-keygen -q -t ed25519 -N '' -C test-host -f "$work/host_key"
ssh-keygen -q -t ed25519 -N '' -C other-host -f "$work/other_host_key"
ssh-keygen -q -t ed25519 -N '' -C test-client -f "$work/client_key"
ssh-keygen -q -t ed25519 -N 'test passphrase' -C test-client-enc -f "$work/client_key_enc"
cat "$work/client_key.pub" "$work/client_key_enc.pub" > "$work/authorized_keys"
chmod 600 "$work/authorized_keys"
mkdir "$work/files"

cat > "$work/sshd_config" <<CONF
Port $sshd_port
ListenAddress 127.0.0.1
HostKey $work/host_key
PidFile $work/sshd.pid
AuthorizedKeysFile $work/authorized_keys
PasswordAuthentication no
KbdInteractiveAuthentication no
UsePAM no
StrictModes no
Subsystem sftp internal-sftp
CONF
sshd=$(command -v sshd || echo /usr/sbin/sshd)
"$sshd" -f "$work/sshd_config" -E "$work/sshd.log"

# Servers that offer only outdated algorithms, which TextEditor+ must
# refuse: CBC ciphers, SHA-1 MACs, SHA-1 key exchange, SHA-1 RSA host keys.
ssh-keygen -q -t rsa -b 3072 -N '' -f "$work/rsa_host_key"
weak_ports=""
port=$((sshd_port + 10))
for weak in "Ciphers aes128-cbc,aes256-cbc" \
            "Ciphers aes128-ctr
MACs hmac-sha1" \
            "KexAlgorithms diffie-hellman-group14-sha1,diffie-hellman-group1-sha1" \
            "HostKeyAlgorithms ssh-rsa"; do
    sed -e "s/^Port .*/Port $port/" -e "s#^HostKey .*#HostKey $work/rsa_host_key#" \
        -e "s#^PidFile .*#PidFile $work/weak$port.pid#" "$work/sshd_config" > "$work/weak$port.conf"
    printf '%s\n' "$weak" >> "$work/weak$port.conf"
    if "$sshd" -f "$work/weak$port.conf" -E "$work/weak$port.log" 2>/dev/null; then
        weak_ports="$weak_ports${weak_ports:+,}$port"
    else
        echo "This sshd can't offer: $weak (skipped)" >&2
    fi
    port=$((port + 1))
done
export TEXTEDITOR_TEST_WEAK_PORTS=$weak_ports

if "$python" -c 'import paramiko' 2>/dev/null; then
    "$python" "$here/hostile_server.py" "$hostile_port" "$work/host_key" > "$work/hostile.log" 2>&1 &
    hostile_pid=$!
    export TEXTEDITOR_TEST_HOSTILE_PORT=$hostile_port
else
    echo "paramiko not found: skipping the hostile-server tests" >&2
fi
sleep 1

export TEXTEDITOR_TEST_SFTP_PORT=$sshd_port
export TEXTEDITOR_TEST_SFTP_USER=$(id -un)
export TEXTEDITOR_TEST_SFTP_KEY=$work/client_key
export TEXTEDITOR_TEST_SFTP_KEY_ENC=$work/client_key_enc
export TEXTEDITOR_TEST_SFTP_DIR=$(cd "$work/files" && pwd -P)
export TEXTEDITOR_TEST_OTHER_HOSTKEY="$(cut -d' ' -f1,2 "$work/other_host_key.pub")"

"$build/tests/tst_sftp" "$@"
