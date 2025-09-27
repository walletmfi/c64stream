#!/usr/bin/env python3
import dpkt

# VIC-II 16-color palette (just for reference)
vic_colors = [
    ' ',  # 0: Black
    '.',  # 1: White
    'R',  # 2: Red
    'C',  # 3: Cyan
    'P',  # 4: Purple
    'G',  # 5: Green
    'B',  # 6: Blue
    'Y',  # 7: Yellow
    'O',  # 8: Orange
    'N',  # 9: Brown
    'r',  # A: Light Red
    'g',  # B: Dark Grey
    'm',  # C: Medium Grey
    'l',  # D: Light Green
    'L',  # E: Light Blue
    'E',  # F: Light Grey
]

def print_packet(payload):
    """Print 4 scanlines of VIC-II pixels from a single packet."""
    # Skip the first 12 bytes (header)
    pixel_data = payload[12:]
    
    # Each nibble = one pixel, 2 pixels per byte
    for line in range(4):  # 4 lines per packet
        line_pixels = []
        for byte in pixel_data[line*96:(line+1)*96]:  # 384 pixels / 2 = 192 bytes per line? Check below
            high = byte >> 4
            low = byte & 0xF
            line_pixels.append(vic_colors[high])
            line_pixels.append(vic_colors[low])
        print(''.join(line_pixels))

def main():
    pcap_file = 'boot_packets.pcap'
    with open(pcap_file, 'rb') as f:
        pcap = dpkt.pcap.Reader(f)
        for ts, buf in pcap:
            eth = dpkt.ethernet.Ethernet(buf)
            if isinstance(eth.data, dpkt.ip.IP):
                ip = eth.data
                if isinstance(ip.data, dpkt.udp.UDP):
                    udp = ip.data
                    if len(udp.data) >= 12:
                        print_packet(udp.data)
                        input("Press Enter for next packet...")

if __name__ == "__main__":
    main()

