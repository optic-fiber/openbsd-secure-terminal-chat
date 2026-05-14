#!/bin/sh
P=/tmp/chat-pipe
O=/tmp/chat-output
L=/tmp/chat.log
M=1000

[ -p "$P" ] && rm -f "$P"
mkfifo "$P"
chown _chatd:chatusers "$P"
chmod 660 "$P"

> "$O"
chown _chatd:chatusers "$O"
chmod 660 "$O"

[ -f "$L" ] || { > "$L"; chown _chatd:chatusers "$L"; chmod 660 "$L"; }

while true; do
    cat "$P" | while IFS= read -r line; do
        [ -z "$line" ] && continue
        echo "$line" >> "$L"
        echo "$line" >> "$O"
        if [ $(wc -l < "$L") -gt $M ]; then
            tail -n $M "$L" > "$L.tmp"
            mv "$L.tmp" "$L"
            chown _chatd:chatusers "$L"
            chmod 660 "$L"
        fi
    done
    sleep 0.1
done
