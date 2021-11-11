#!/usr/bin/python3
#
# this script is an example of using the tips_console python module to interact with hmbdc/tips
# for simplicity, the example pub and sub to the same messages - publish to itself
# record a message bag in the first console and play it back using a second console instance
# 
# to run
# python3 ./tips-console --netProt rmcast  # use rmcast, other supported args see: tips-console --help
# example output:
# {'status': 'Session started'}
# {'status': 'record bag ready! hit ctrl-d to exit'}
# {'status': '0'}
# {'status': 'exiting... '}
# {'status': 'Session stopped'}
# recorded a bag
# {'status': 'Session started'}
# {'status': 'bag play done '}
# {'status': 'exiting... '}
# {'status': 'Session stopped'}
# Done playing back the bag

from tips_console import TipsConsole
import sys
import time
import itertools

cmd_line = sys.argv.copy()
cmd_line.pop(0)
console = TipsConsole(cmd_line) # example cmd_line="./tips-console --netProt rmcast"
console.ostr()                  # output in string format
console.pubtags([1001, 2001])   # declare pub tags
console.subtags([1001, 2001])   # declare sub tags - we publish to ourself so the same tags
console.pubstr(1001, "hello world")             # publish a string
msgs = console.incoming_msgs()                  # process incoming msgs
message = next(msgs)                            # a dictionary
assert message["tag"] == 1001, message
assert message["msgstr"] == "hello world", message  # called ostr() earlier, so we get msgstr

console.ohex()                  # now switch output to be hex numbers

#publish message tagged 2001 with 4 bytes, and 10 bytes attachment using bytearray of 14 bytes
console.pubatt(2001, 4, bytearray(b'\x01\x02\x03\x04\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a'))
message = next(msgs)

#this is what we get from the above pubatt
assert message["tag"] == 2001, message
assert message["msgatt"][:4] == bytearray(b'\x01\x02\x03\x04'), message
assert message["att"][:10] == bytearray(b'\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a'), message

#now record the future message for 1.5 sec into a bag file
console.record("/tmp/1.bag", 1.5)
#the following messages, when received, will go to bag file - not showing in incoming_msgs()
console.pubatt(2001, 4, bytearray(b'\x01\x02\x03\x04\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a'))

time.sleep(2)   # record must be done

# print out the status messages (stderr from tips-console)
for m in console.status_msgs():
    print(m)
    if m['status'] == 'record bag ready! hit ctrl-d to exit':
        console.exit()  # close console - also exit loop eventually
print("recorded a bag")

console2= TipsConsole(cmd_line)
console2.subtags([2001])        # sub tags from the bag
console2.ohex()                 # now switch output to be hex numbers
console2.play("/tmp/1.bag")     # play the recorded bag
msgs = console2.incoming_msgs()                 # process incoming msgs
message = next(msgs)                            # a dictionary
#this is what we get from the above bag
assert message["tag"] == 2001, message
assert message["msgatt"][:4] == bytearray(b'\x01\x02\x03\x04'), message
assert message["att"][:10] == bytearray(b'\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a'), message
console2.exit()  # close console

for m in itertools.islice(console2.status_msgs(), 5):
    print(m)

print("Done playing back the bag")
