#
# python3 interface for tips-console  
#
import subprocess
import sys
import time
import os

class TipsConsole :
    """[summary]
    provides python interface to hmbdc tips console
    almost 1-1 match to the console cmds
    """
    def __init__(self, console_cmd_line) :
        """constructor
        Args:
            console_cmd_line (string): the full console invoke cmd line, examples:
            "./tips-console --netProt rmcast"
        """
        #Popen Console, so we can use console-tcpcast as a console to hmbdc-tcpcast functions
        self._pipe = subprocess.Popen(console_cmd_line, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    
    def pubtags(self, tags) :
        """see console cmd with the same name

        Args:
            tags (sequence): list of message tags: for example [1001, 2003]
        """
        tags_str = " ".join(str(t) for t in tags)
        self._pipe.stdin.write("pubtags " + tags_str + "\n")
        self._pipe.stdin.flush()
    
    def subtags(self, tags) :
        """see console cmd with the same name

        Args:
            tags (sequence): list of message tags
        """
        tags_str = " ".join(str(t) for t in tags)
        self._pipe.stdin.write("subtags " + tags_str + "\n")
        self._pipe.stdin.flush()

    def pubstr(self, tag, msgstr) :
        """see console cmd with the same name

        Args:
            tag (int): message tag
            msgstr (string): a message in the format of string 
        """
        cmd = "pubstr " + str(tag) + " " + msgstr + "\n"
        self._pipe.stdin.write(cmd)
        self._pipe.stdin.flush()

    def pub(self, tag, byte_array) :
        """see console cmd with the same name

        Args:
            tag (int): message tag
            byte_array (bytearray): bytes of the message
        """
        cmd = " ".join(str(a) for a in ["pub", tag, len(byte_array)])
        self._pipe.stdin.write(cmd)
        for i in range(len(byte_array)):
            self._pipe.stdin.write(' {:02X}'.format(byte_array[i]))
        self._pipe.stdin.write('\n')
        self._pipe.stdin.flush()

    def pubatt(self, tag, msg_len, byte_array) :
        """see console cmd with the same name

        Args:
            tag (int): message tag
            msg_len (int): message length - it does not include the attachment length
            byte_array (bytearray): the message bytes followed by attachment bytes
        """
        cmd = " ".join(str(a) for a in ["pubatt", tag, msg_len, len(byte_array) - msg_len])
        self._pipe.stdin.write(cmd)
        for i in range(len(byte_array)):
            self._pipe.stdin.write(' {:02X}'.format(byte_array[i]))
        self._pipe.stdin.write('\n')
        self._pipe.stdin.flush()

    def play(self, bag_file) :
        """see console cmd with the same name

        Args:
            bag_file (string): play a recorded bag file
        """
        self._pipe.stdin.write("play " + bag_file + "\n")
        self._pipe.stdin.flush()

    def ohex(self) :
        """see console cmd with the same name
        """
        self._pipe.stdin.write("ohex\n")
        self._pipe.stdin.flush()
    
    def ostr(self) :
        """see console cmd with the same name
        """
        self._pipe.stdin.write("ostr\n")
        self._pipe.stdin.flush()

    def record(self, bag_file, seconds) :
        """see console cmd with the same name

        Args:
            bag_file (string): recorded bag file name
            seconds (float): seconds before end recording and stop the console
        """
        cmd = " ".join(["record", bag_file, str(seconds)]) + "\n"
        self._pipe.stdin.write(cmd)
        self._pipe.stdin.flush()

    def exit(self) :
        """see console cmd with the same name
        """
        self._pipe.stdin.write("exit\n")
        self._pipe.stdin.flush()

    def incoming_msgs(self) :
        """sequence of incoming messages

        Yields:
            a dictionary: contains fields for a message
        """
        read_att = False
        while True:
            l = self._pipe.stdout.readline()
            if not l: break
            if not read_att:
                res = {}
                (tagstr, l) = l.split(maxsplit=1)
                res["tag"] = int(tagstr)
                (type, l) = l.split('=', maxsplit=1)

                if type == "msgstr":
                    res["msgstr"] = l[1:][:-1]   #the first and last char ignored
                    yield res
                elif type == "msg":
                    bytes = map(lambda x: int(x, 16), l.split())
                    res["msg"] = bytearray(bytes)
                    yield res
                elif type == "msgatt":
                    bytes = map(lambda x: int(x, 16), l.split())
                    res["msgatt"] = bytearray(bytes)
                    read_att = True
            else:
                (typestr, l) = l.split('=', maxsplit=1)
                if typestr == "att":
                    bytes = map(lambda x: int(x, 16), l.split())
                    res["att"] = bytearray(bytes)
                    read_att = False
                    yield res
                else:
                    print("while expecting att, got {}= and {}".format(typestr, l), file=sys.stderr)

    def status_msgs(self) :
        """sequence for status messages (console stderr output)
            Each stderr line maps to one status message

        Yields:
            dictionary: contains 'status' info line or 'other' line that isn't a status
        """
        while True:
            l = self._pipe.stderr.readline()
            if not l: break
            res = {}
            (typestr, l) = l.split(maxsplit=1)
            if typestr == "[status]":
                res["status"] = l[:-1]
                yield res
            else:
                res["other"] = l[:-1]
                yield res
