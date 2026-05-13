#!/bin/sh
PIPE=/tmp/chat-pipe
OUT=/tmp/chat-output
LOG=/tmp/chat.log

[ -p "$PIPE" ] && rm -f "$PIPE"
mkfifo "$PIPE"
chown _chatd:chatusers "$PIPE"
chmod 660 "$PIPE"

> "$OUT"
chown _chatd:chatusers "$OUT"
chmod 660 "$OUT"

> "$LOG"
chown _chatd:chatusers "$LOG"
chmod 660 "$LOG"

echo "Chat-Dispatcher started"

while true; do
    cat "$PIPE" | while IFS= read -r line; do
        [ -z "$line" ] && continue
        echo "$(date '+%H:%M') $line" >> "$LOG"
        echo "$line" >> "$OUT"
    done
    sleep 0.1
done