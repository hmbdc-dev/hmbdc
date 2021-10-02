#!/usr/bin/python3
#
# this script is an example of using the tips_console python module to interact with hmbdc/tips
# for simplicity, the example pub and sub to the same messages - publish to itself
# to run
# python3 tips-console --netProt rmcast  # use rmcast, other supported args see: tips-console --help
# example output:
#   python3 tips-console # use tcpcast
#   status': 'Session started'}
#   {'status': 'record bag ready! hit ctrl-d to exit'}
#   {'status': '0'}
#   {'status': 'exiting... '}
#   {'status': 'Session stopped'}
#   Done
from tips_console import TipsConsole
import sys
import time

cmd_line = sys.argv.copy()
cmd_line.pop(0)
console = TipsConsole(cmd_line)
console.ostr()
console.pubtags([1001, 2001])
console.subtags([1001, 2001])
time.sleep(1)
console.pubstr(1001, "hello world")
msgs = console.incoming_msgs()
line = next(msgs)
assert line["tag"] == 1001, line
assert line["msgstr"] == "hello world", line

console.ohex()
console.pubatt(2001, 4, bytearray(b'\x01\x02\x03\x04\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a'))
line = next(msgs)
assert line["tag"] == 2001, line
assert line["msgatt"][:4] == bytearray(b'\x01\x02\x03\x04'), line
assert line["att"][:10] == bytearray(b'\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a'), line
console.record("/tmp/1.bag", 1.5)
console.pubatt(2001, 4, bytearray(b'\x01\x02\x03\x04\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a'))
time.sleep(2)
for m in console.status_msgs():
    print(m)
    if m['status'] == 'record bag ready! hit ctrl-d to exit':
        console.exit()  # close console - also exit loop eventually
print("Done")


