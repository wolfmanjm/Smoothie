#!/usr/bin/env python
"""\
Stream g-code to Smoothie telnet connection

Based on GRBL stream.py
"""

from __future__ import print_function
import sys
import telnetlib
import argparse

def write_raw_sequence(tn, seq):
    sock = tn.get_socket()
    if sock is not None:
        sock.send(seq)

# Define command line argument interface
parser = argparse.ArgumentParser(description='Stream g-code file to Smoothie over telnet.')
parser.add_argument('gcode_file', type=argparse.FileType('r'),
        help='g-code filename to be streamed')
parser.add_argument('ipaddr',
        help='Smoothie IP address')
parser.add_argument('-q','--quiet',action='store_true', default=False,
        help='suppress output text')
args = parser.parse_args()

f = args.gcode_file
verbose = True
if args.quiet : verbose = False

# Stream g-code to Smoothie
print("Streaming " + args.gcode_file.name + " to " + args.ipaddr)

tn = telnetlib.Telnet(args.ipaddr)
# read startup prompt
tn.read_until("> ")

# turn off prompt
write_raw_sequence(tn, telnetlib.IAC + telnetlib.DONT + "\x55")

for line in f:
    tn.write(line)
    tn.read_until("ok")
    if verbose: print("SND: " + line, end="")

# turn on prompt
write_raw_sequence(tn, telnetlib.IAC + telnetlib.DO + "\x55")
tn.write("exit\n")

print("Done")



