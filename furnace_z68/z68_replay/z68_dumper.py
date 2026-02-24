#!/usr/bin/env python
import sys
import os
import numpy

Z68_VERSION = 1

print("Z68 DUMPER v1.0")
print("Written by Franck 'hitchhikr' Charlet.")
if(len(sys.argv) < 2):
    print("Usage z68_dumper.py <filename>")
    sys.exit(1)
filename = sys.argv[1]
map_filename = filename[0:len(filename) - 4] + ".dump"
f = open(filename, 'r+b')
m = open(map_filename, 'w')
readdata = b''
newdata = bytearray()

for b in f:
    readdata = readdata + b

if readdata[0] != 0x7a or readdata[1] != 0x36 or readdata[2] != 0x38 or readdata[3] != Z68_VERSION:
    print("Error: Not a z68 file.")
    sys.exit()
if readdata[3] != 1:
    print("Error: Wrong file version.")
    sys.exit()

print("----------------------------------")
print("Generating '" + map_filename + "'...", end="")
# header size
header_size = 14
i = header_size
loop_point = int((readdata[4] << 24) | (readdata[5] << 16) | (readdata[6] << 8) | readdata[7])
samples_pos = int((readdata[8] << 24) | (readdata[9] << 16) | (readdata[10] << 8) | readdata[11])
speed = (readdata[12] << 8) | readdata[13]
m.write("DUMP FOR: %s\n" % filename)
m.write("INITIAL SPEED: 0x%.02x\n" % speed)
m.write("LOOP POINT: 0x%.04x\n" % loop_point)
if(samples_pos):
    samples = (readdata[samples_pos] << 8) | readdata[samples_pos + 1]
    m.write("%d SAMPLES AT 0x%x:\n" % (samples, samples_pos))
    samples_pos = samples_pos + 2
    for j in range(samples):
        smp_length  = int((readdata[samples_pos + 4 + (j * 8)] << 24) | (readdata[samples_pos + 4 + (j * 8) + 1] << 16) | \
                          (readdata[samples_pos + 4 + (j * 8) + 2] << 8) | readdata[samples_pos + 4 + (j * 8) + 3])
        smp_pos  = int((readdata[samples_pos + (j * 8)] << 24) | (readdata[samples_pos + (j * 8) + 1] << 16) | \
                       (readdata[samples_pos + (j * 8) + 2] << 8) | readdata[samples_pos + (j * 8) + 3])
        m.write("0x%.06x: %d BYTES\n" % (smp_pos + samples_pos, smp_length))
else:
    m.write("NO SAMPLES\n")
in_row = 0;
while i < len(readdata):
    i_cmd = int(readdata[i])
    i += 1
    if i_cmd == 0x40:
        i_cmd_dat = int(readdata[i])
        i += 1
        if(i_cmd_dat == 0):
            i_cmd_dat = int(readdata[i])
            m.write("0x%.06x: NEW SPEED (0x40,0x00) 0x%.02x\n" % (i - 2 - header_size, i_cmd_dat))
            i += 1
            continue
        else:
            i_cmd_dat = int(readdata[i])
            m.write("0x%.06x: NEW TEMPO (0x40,0x01) 0x%.02x\n" % (i - 2 - header_size, i_cmd_dat))
            i += 1
            continue
        continue
    if i_cmd < 0x40:
        if i_cmd == 0:
            m.write("0x%.06x: ADPCM OFF\n" % (i - 1 - header_size))
            continue
        i_cmd_dat = int(readdata[i])
        i += 1
        if i_cmd == 1:
            m.write("0x%.06x: ADPCM SAMPLE 0x%.02x\n" % (i - 2 - header_size, i_cmd_dat))
            continue
        if i_cmd == 2:
            if i_cmd_dat == 3:
                m.write("0x%.06x: ADPCM PANNING CENTER\n" % (i - 2 - header_size))
                continue
            if i_cmd_dat == 1:
                m.write("0x%.06x: ADPCM PANNING LEFT\n" % (i - 2 - header_size))
                continue
            if i_cmd_dat == 2:
                m.write("0x%.06x: ADPCM PANNING RIGHT\n" % (i - 2 - header_size))
                continue
            continue
        if i_cmd == 3:
            if i_cmd_dat == 0:
                m.write("0x%.06x: ADPCM CLOCK FULL\n" % (i - 2 - header_size))
                continue
            if i_cmd_dat == 1:
                m.write("0x%.06x: ADPCM CLOCK HALF\n" % (i - 2 - header_size))
                continue
        if i_cmd == 4:
            if i_cmd_dat == 0:
                m.write("0x%.06x: ADPCM RATE 7.8kHz\n" % (i - 2 - header_size))
                continue
            if i_cmd_dat == 1:
                m.write("0x%.06x: ADPCM RATE 10.4kHz\n" % (i - 2 - header_size))
                continue
            if i_cmd_dat == 2:
                m.write("0x%.06x: ADPCM RATE 15.6kHz\n" % (i - 2 - header_size))
                continue
            continue
        continue
    if (i_cmd >= 0x41) and (i_cmd <= 0x7f):
        m.write("0x%.06x: YM (0x%.02x) SIZE: 0x%.02x (%d)\n" % (i - 1 - header_size, i_cmd, i_cmd - 0x40, i_cmd - 0x40)) 
        # write header (0x41..0x7f)
        # read & write data pairs
        m.write("          ")
        for j in range(i_cmd & 0x3f):
            i_cmd_dat = int(readdata[i])
            m.write("0x%.02x " % i_cmd_dat)
            i += 1
            i_cmd_dat = int(readdata[i])
            m.write("0x%.02x " % i_cmd_dat)
            if((j + 1) != (i_cmd & 0x3f)):
                if((j + 1) % 16 == 0):
                    m.write("\n")
                    m.write("          ")
            i += 1
        m.write("\n")
        continue
    if (i_cmd >= 0x80):
        if i_cmd == 0x81:
            m.write("0x%.06x: NEW ROW (0x%.02x)\n" % (i - 1 - header_size, i_cmd))
            in_row = 1
            continue
        if i_cmd == 0x82:
            m.write("0x%.06x: END OF ROW (0x%.02x)\n" % (i - 1 - header_size, i_cmd))
            in_row = 0
            continue
        if i_cmd == 0x83:
            m.write("0x%.06x: EMPTY ROW (0x%.02x)\n" % (i - 1 - header_size, i_cmd))
            continue
        else:
            m.write("0x%.06x: END (0x%.02x)\n" % (i - 1 - header_size, i_cmd))
            break
        continue
m.close()
print(" done.")
