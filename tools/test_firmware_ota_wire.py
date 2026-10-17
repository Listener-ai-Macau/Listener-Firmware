"""Exercise the production portable DOVZ parser, with host zlib/SHA adapters.

This does not certify ESP ROM speed or hardware acceptance. Use --output for
build evidence and optionally --image for the actual canonical OTA artifact.
"""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import random
import struct
import zlib
from setuptools._distutils.ccompiler import new_compiler
from setuptools._distutils.sysconfig import customize_compiler

p = argparse.ArgumentParser()
p.add_argument('--output', required=True, type=Path)
p.add_argument('--image', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
a.output.mkdir(parents=True, exist_ok=True)
bridge = a.output / 'bridge.c'
bridge.write_text('''#include <stdlib.h>
#include "firmware_ota_wire.h"
__declspec(dllexport) void *make_wire(void) { return calloc(1,sizeof(firmware_ota_wire_t)); }
__declspec(dllexport) void free_wire(void *p) { free(p); }
__declspec(dllexport) unsigned raw_written(firmware_ota_wire_t *p) { return p->raw_written; }
__declspec(dllexport) unsigned wire_received(firmware_ota_wire_t *p) { return p->wire_received; }
__declspec(dllexport) bool init_wire(firmware_ota_wire_t *p, unsigned w, unsigned r,
 unsigned char *i, unsigned char *o, firmware_ota_wire_driver_t *d) {
 return firmware_ota_wire_init(p,w,r,i,o,*d,NULL); }
__declspec(dllexport) bool push_wire(firmware_ota_wire_t *p,const unsigned char *b,size_t n) {
 return firmware_ota_wire_write(p,b,n); }
__declspec(dllexport) bool end_wire(firmware_ota_wire_t *p) { return firmware_ota_wire_finish(p); }
''')
compiler = new_compiler()
customize_compiler(compiler)
objects = compiler.compile([str(bridge), str(root/'components/firmware_ota/firmware_ota_wire.c')],
    include_dirs=[str(root/'components/firmware_ota/include')], output_dir=str(a.output),
    extra_postargs=['/W4', '/WX', '/Od'])
compiler.link_shared_object(objects, str(a.output/'wire.dll'))
lib = c.CDLL(str(a.output/'wire.dll'))
lib.make_wire.restype = c.c_void_p
lib.free_wire.argtypes = [c.c_void_p]
lib.init_wire.argtypes = [c.c_void_p, c.c_uint, c.c_uint, c.c_void_p, c.c_void_p, c.c_void_p]
lib.init_wire.restype = c.c_bool
lib.push_wire.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t]
lib.push_wire.restype = c.c_bool
lib.end_wire.argtypes = [c.c_void_p]
lib.end_wire.restype = c.c_bool
for name in ['raw_written', 'wire_received']:
    getattr(lib, name).argtypes = [c.c_void_p]
    getattr(lib, name).restype = c.c_uint

inflate_t = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p, c.c_size_t)
update_t = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.c_void_p, c.c_size_t)
finish_t = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.c_void_p)
class Driver(c.Structure):
    _fields_ = [('inflate', inflate_t), ('update', update_t), ('finish', finish_t), ('write', update_t)]

def encode(raw):
    result = bytearray(struct.pack('<4sBBHII32s', b'DOVZ', 1, 1, 48, len(raw), 16384,
                                   hashlib.sha256(raw).digest()))
    for offset in range(0, len(raw), 16384):
        block = raw[offset:offset+16384]
        packed = zlib.compress(block, 6)
        body, kind = (packed, 1) if len(packed) < len(block) else (block, 0)
        result += struct.pack('<HHB3x', len(body), len(block), kind) + body
    return bytes(result)

results = []
def check(name, raw, wire=None, step=443, valid=True, stop=None, fail_callback=None,
          declared_wire=None, declared_raw=None):
    wire = encode(raw) if wire is None else bytes(wire)
    digest = hashlib.sha256()
    output = bytearray()
    @inflate_t
    def inflate(_, src, n, dst, m):
        if fail_callback == 'inflate': return False
        try:
            decoder = zlib.decompressobj()
            block = decoder.decompress(c.string_at(src, n), m+1)
            if len(block) != m or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
                return False
            c.memmove(dst, block, m)
            return True
        except zlib.error:
            return False
    @update_t
    def update(_, src, n):
        digest.update(c.string_at(src, n))
        return fail_callback != 'hash_update'
    @finish_t
    def finish(_, dst):
        c.memmove(dst, digest.digest(), 32)
        return fail_callback != 'hash_finish'
    @update_t
    def write(_, src, n):
        if fail_callback == 'write': return False
        output.extend(c.string_at(src, n))
        return True
    driver = Driver(inflate, update, finish, write)
    state = lib.make_wire()
    input_buffer, output_buffer = c.create_string_buffer(16384), c.create_string_buffer(16384)
    try:
        accepted = lib.init_wire(state, len(wire) if declared_wire is None else declared_wire,
            len(raw) if declared_raw is None else declared_raw, input_buffer, output_buffer, c.byref(driver))
        if accepted:
            for offset in range(0, len(wire) if stop is None else stop, step):
                end = min(offset+step, len(wire) if stop is None else stop)
                if not lib.push_wire(state, wire[offset:end], end-offset):
                    accepted = False
                    # A partial failure cannot be retried into a different parser state.
                    assert not lib.push_wire(state, b'x', 1)
                    break
            accepted = lib.end_wire(state) and accepted
        assert bool(accepted) == valid, name
        if valid:
            assert output == raw and lib.raw_written(state) == len(raw), name
            assert lib.wire_received(state) == len(wire), name
            assert lib.end_wire(state), name
            assert not lib.push_wire(state, b'x', 1), name
        else:
            assert not lib.end_wire(state), name
        results.append({'name': name, 'passed': True, 'accept': bool(accepted)})
    finally:
        lib.free_wire(state)

rng = random.Random(7)
raw = b'Listener independent block ' * 780 + rng.randbytes(16384) + b'last' * 300
wire = encode(raw)
for step in [1, 7, 48, 443, 500, 16384, len(wire)]:
    check(f'mixed blocks fragmented {step}', raw, step=step)
for pos in [0, 4, 5, 6, 8, 12, 16, 48, 50, 52, 53, 54, 55]:
    broken = bytearray(wire); broken[pos] ^= 1
    check(f'corrupt header byte {pos}', raw, broken, valid=False)
broken = bytearray(wire); first_size = struct.unpack_from('<H', wire, 48)[0]
broken[56+first_size-1] ^= 1
check('bad compressed Adler', raw, broken, valid=False)
check('trailing bytes', raw, wire+b'x', valid=False)
for stop in [0, 1, 47, 48, 49, 55, 56, len(wire)-1]:
    check(f'truncated {stop}', raw, wire, stop=stop, valid=False)
for cb in ['inflate', 'hash_update', 'hash_finish', 'write']:
    check('failed '+cb, raw, valid=False, fail_callback=cb)
for n in [0, 0x600001, 0xffffffff]:
    check(f'raw size invalid {n}', raw, valid=False, declared_raw=n)
for n in [0, 56, 0xffffffff]:
    check(f'wire size invalid {n}', raw, valid=False, declared_wire=n)
if a.image:
    image = a.image.read_bytes()
    check('canonical actual firmware 443-byte wire fragments', image)
    check('canonical actual firmware irregular block boundaries', image, step=499)
record = {'productionParser': str(root/'components/firmware_ota/firmware_ota_wire.c'),
          'parserSha256': hashlib.sha256((root/'components/firmware_ota/firmware_ota_wire.c').read_bytes()).hexdigest(),
          'checks': results, 'count': len(results), 'allPassed': True,
          'scope': 'Production portable parser with Python zlib/SHA callbacks; ESP ROM, device timing, GATT lifecycle not claimed'}
(a.output/'result.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
print(json.dumps({'count':len(results), 'allPassed':True}))
