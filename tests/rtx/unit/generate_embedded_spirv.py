#/*
#* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
#*
#* Permission is hereby granted, free of charge, to any person obtaining a
#* copy of this software and associated documentation files (the "Software"),
#* to deal in the Software without restriction, including without limitation
#* the rights to use, copy, modify, merge, publish, distribute, sublicense,
#* and/or sell copies of the Software, and to permit persons to whom the
#* Software is furnished to do so, subject to the following conditions:
#*
#* The above copyright notice and this permission notice shall be included in
#* all copies or substantial portions of the Software.
#*
#* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
#* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#* DEALINGS IN THE SOFTWARE.
#*/
"""Generate mixed-width shader words to verify Python/C++ codec compatibility."""
import importlib.util
from pathlib import Path
import struct
import sys

spec = importlib.util.spec_from_file_location('shader_xxd', sys.argv[1])
codec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codec)
words = [0x07230203, 0x00010500, 0, 512, 0]
widths = [0, 1, 255, 256, 65535, 65536, 16777215, 16777216, 4294967295]
words.extend(widths[i % len(widths)] for i in range(197))
raw = struct.pack('<' + 'I' * len(words), *words)
Path(sys.argv[2]).write_bytes(raw)
Path(sys.argv[3]).write_bytes(codec.compress_spirv(raw))
