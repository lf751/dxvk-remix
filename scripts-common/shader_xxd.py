import argparse
import os
import struct


def compress_spirv(data):
    """Encode SPIR-V words using two-bit byte counts and a packed byte stream."""
    if len(data) % 4:
        raise ValueError('SPIR-V size must be a multiple of four')
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    masks = bytearray()
    payload = bytearray()
    for start in range(0, len(words), 32):
        mask = 0
        for index, word in enumerate(words[start:start + 32]):
            count = max(1, (word.bit_length() + 7) // 8)
            mask |= (count - 1) << (2 * index)
            payload.extend(word.to_bytes(count, 'little'))
        masks.extend(struct.pack('<Q', mask))
    header = struct.pack('<IIII', 0x43565053, len(words), len(payload), 0)
    return header + masks + payload + bytes((-len(payload)) % 8)

def xxd_c_array(data, output_filename):
    # array name is the file name, extract filename without extension from basename
    output_basename = os.path.basename(output_filename)
    array_name = output_basename.removesuffix('.h')

    # calculate padding from byte array length
    length = len(data)
    padding = (4 - (length % 4)) % 4
    data += b'\x00' * padding

    with open(output_filename, 'w', encoding='utf-8') as f:
        f.write(f"const uint32_t {array_name}[] = {{")

        for i in range(0, len(data), 4):
            if (i // 4) % 8 == 0: # 8 columns
                f.write('\n    ')
            val = data[i] | (data[i+1] << 8) | (data[i+2] << 16) | (data[i+3] << 24)
            f.write(f'0x{val:08x}, ')

        f.write("\n};\n\n")
        f.write(f"const size_t {array_name}_sizeInBytes = {length};\n")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert SPV to C header')
    parser.add_argument('--input', '-i', required=True)
    parser.add_argument('--output', '-o', required=True)
    parser.add_argument('--compress', action='store_true', help='Use the DXVK SPIR-V word codec')
    args = parser.parse_args()
    with open(args.input, 'rb') as input_file:
        data = input_file.read()
    if args.compress:
        data = compress_spirv(data)
    xxd_c_array(data, args.output)
