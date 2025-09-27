#!/usr/bin/env python3
import socket
import select

# VIC colors: one character per color
vic_colors = [
    ' ', '.', 'R', 'C', 'P', 'G', 'B', 'Y',
    'O', 'N', 'r', 'g', 'm', 'l', 'L', 'E'
]

UDP_IP = "0.0.0.0"
UDP_PORT = 11000

SCREEN_HEIGHT = 272
MIDDLE_LINE = SCREEN_HEIGHT // 2  # 136
BYTES_PER_LINE = 384 // 2  # 384 pixels, 2 pixels per byte
LINES_PER_PACKET = 4

def print_middle_line(payload):
    """Extract and print the middle line from a VIC UDP packet"""
    if len(payload) < 12:
        return

    line_num_raw = payload[4] + (payload[5] << 8)
    line_num = line_num_raw & 0x7FFF

    # Only print if this packet contains the middle line
    if line_num <= MIDDLE_LINE < line_num + LINES_PER_PACKET:
        line_index = MIDDLE_LINE - line_num
        pixel_data = payload[12:]
        start = line_index * BYTES_PER_LINE
        end = start + BYTES_PER_LINE

        if end > len(pixel_data):
            return

        line_bytes = pixel_data[start:end]
        line_pixels = []
        for b in line_bytes:
            line_pixels.append(vic_colors[b >> 4])
            line_pixels.append(vic_colors[b & 0xF])

        print(''.join(line_pixels))

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))

    try:
        while True:
            # Wait up to 1 second for a packet
            ready = select.select([sock], [], [], 1.0)
            if ready[0]:
                data, _ = sock.recvfrom(2048)
                if len(data) >= 12:
                    print_middle_line(data)
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()

