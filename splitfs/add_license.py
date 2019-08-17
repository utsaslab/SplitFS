#!/usr/bin/python

import re
import sys
import os

def add_license(filename, lines):
	print "Add license to ", filename
	fd = open(filename, 'r')
	lines1 = fd.read()
	lines2 = lines + lines1
	lines2 = lines2.replace('\r', '')

	fd.close()
	fd = open(filename, 'w')
	fd.write(lines2)
	fd.close()


def main():
	if len(sys.argv) < 2:
		print "Usage: $python add_license.py license_file"
		sys.exit(0)

	license = sys.argv[1]
	print "License:", license
	fd = open(license, 'rb')
	lines = fd.read()

	for root, dirs, files in os.walk("."):
		for file1 in files:
			name = os.path.join(root, file1)
			if name.endswith(".c"):	add_license(name, lines)
			if name.endswith(".h"):	add_license(name, lines)

	return

main()
