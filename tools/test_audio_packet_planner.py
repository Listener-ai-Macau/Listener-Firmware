"""Round-trip the production packet planner and codec, including small ATT budgets."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import wave
from test_power_source_filter import ROOT, function


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path)
    parser.add_argument('--wav', type=Path)
    args = parser.parse_args()
    source = (args.source or ROOT/'ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c').read_text(encoding='utf-8')
    platform = ROOT/'third_party/denzic-platform/audio/embedded/c'
    constants = '\n'.join(re.findall(r'^#define BLE_AUDIO_STREAM_LOSSLESS_RICE_\w+[^\n]+',source,re.M))
    code = '''
#include <stdint.h>
#include <string.h>
#include "denzic_audio_lossless_v1.h"
#ifdef _MSC_VER
// Production uses the explicit uint16_t ~1U mask accepted by the ESP compiler.
#pragma warning(disable:4310)
#endif
static uint8_t s_transport_lossless_rice_version;
static uint32_t probes;
static bool ble_audio_stream_session_lossless_rice_enabled(void) { return true; }
static bool ble_audio_stream_encode_lossless_rice(const uint8_t *p,uint16_t n,uint8_t v,uint8_t *out,uint16_t cap,uint16_t *written) {
    probes++; return denzic_audio_lossless_v1_encode(p,n,v,out,cap,written);
}
'''
    code += constants+'\n'+function(source,'ble_audio_stream_plan_session_audio_packet')
    code += '''
#ifdef _WIN32
__declspec(dllexport)
#endif
int validate(const uint8_t *pcm,uint32_t length,uint16_t budget,uint8_t version,uint32_t *stats) {
    s_transport_lossless_rice_version=version;probes=0;
    stats[0]=0;stats[1]=0;stats[2]=0;stats[3]=0;
    uint32_t offset=0;
    while(offset<length) {
        uint32_t remaining=length-offset;
        uint32_t batch_left=1920-offset%1920;
        if(remaining>batch_left)remaining=batch_left;
        uint8_t encoded[512],decoded[512],flags;
        const uint8_t *wire;
        uint16_t wire_bytes,pcm_bytes;
        uint32_t before=probes;
        if(!ble_audio_stream_plan_session_audio_packet(pcm+offset,(uint16_t)remaining,budget,encoded,budget,&wire,&wire_bytes,&pcm_bytes,&flags))return 1;
        if(!pcm_bytes || pcm_bytes>remaining || wire_bytes>budget)return 2;
        size_t decoded_bytes=0;
        if(flags) {
            if(denzic_audio_lossless_v1_decode(wire,wire_bytes,pcm_bytes,decoded,sizeof(decoded),&decoded_bytes)!=DENZIC_AUDIO_LOSSLESS_V1_DECODE_OK)return 3;
            if(decoded_bytes!=pcm_bytes || memcmp(decoded,pcm+offset,pcm_bytes))return 4;
        } else if(wire_bytes!=pcm_bytes || memcmp(wire,pcm+offset,pcm_bytes))return 5;
        stats[0]++;stats[2]+=wire_bytes;
        if(probes-before>stats[3])stats[3]=probes-before;
        offset+=pcm_bytes;
    }
    stats[1]=probes;
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='listener-packet-test-', ignore_cleanup_errors=True) as directory:
        tmp=Path(directory);(tmp/'test.c').write_text(code,encoding='utf-8')
        includes=str(platform/'include');implementation=str(platform/'src/denzic_audio_lossless_v1.c')
        if os.name=='nt':
            vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
            vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-property','installationPath'],text=True).strip()
            vcvars=Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
            (tmp/'build.cmd').write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /LD /O2 /std:c11 /W4 /WX /I"{includes}" test.c "{implementation}" /link /out:test.dll\n',encoding='utf-8')
            subprocess.run(['cmd.exe','/d','/c',str(tmp/'build.cmd')],cwd=tmp,check=True)
            library=ctypes.CDLL(str(tmp/'test.dll'))
        else:
            subprocess.run([shutil.which('cc') or 'cc','-std=c11','-O2','-shared','-fPIC','-I'+includes,'test.c',implementation,'-o','test.so'],cwd=tmp,check=True)
            library=ctypes.CDLL(str(tmp/'test.so'))
        validate=library.validate
        validate.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint16,ctypes.c_uint8,ctypes.POINTER(ctypes.c_uint32)]
        pcm=bytes((i*73+i//7)%256 for i in range(19200))+bytes(19200)
        if args.wav:
            with wave.open(str(args.wav)) as reader:
                assert reader.getnchannels()==1 and reader.getsampwidth()==2
                pcm=reader.readframes(reader.getnframes())
        buffer=ctypes.create_string_buffer(pcm)
        for version in [1,2,3]:
            for budget in [20,224,480]:
                stats=(ctypes.c_uint32*4)()
                assert validate(buffer,len(pcm),budget,version,stats)==0, (version,budget)
                if not args.source: assert stats[3]<=8, (version,budget,list(stats))
                print(json.dumps(dict(version=version,budget=budget,pcm_bytes=len(pcm),packets=stats[0],encoder_probes=stats[1],wire_bytes=stats[2],max_probes_per_packet=stats[3])))
        # Explicitly unload before Windows removes the temporary DLL.
        if os.name=='nt':
            import _ctypes
            _ctypes.FreeLibrary(library._handle)


if __name__=='__main__':
    main()
