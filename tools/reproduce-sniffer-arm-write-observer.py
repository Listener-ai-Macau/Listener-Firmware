"""Minimal saved-ELF diagnostic; no radio or product is opened.

Unicorn2.x global MEM_WRITE observation changes this Thumb IT fragment's
subsequent CRC decision. Keep an artifact, not an inferred hardware fault.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,sys
p=argparse.ArgumentParser();p.add_argument('--workspace',type=Path,default=Path(__file__).resolve().parent);a=p.parse_args()
R=a.workspace.resolve();sys.path.insert(0,str(R/'arm-emulator-libs'))
import unicorn
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_WRITE,UC_HOOK_CODE,UC_HOOK_MEM_READ
from unicorn.arm_const import UC_CPU_ARM_CORTEX_M4,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_LR,UC_ARM_REG_SP,UC_ARM_REG_R10
from elftools.elf.elffile import ELFFile
elf=R/'source-rxdiag128/build/nrf52840_xxaa.out'
assert hashlib.sha256(elf.read_bytes()).hexdigest()=='05ceaf0472c2df3c8b2088e7fdc141ad900c81ac483b166856fe5277926f1181'
results=[]
for kind in ['none','write','write_code','write_read_code']:
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
 for base,size in [(0,0x100000),(0x20000000,0x40000),(0x40000000,0x200000)]:u.mem_map(base,size)
 with elf.open('rb') as f:
  e=ELFFile(f)
  for sec in e.iter_sections():
   if sec['sh_size'] and sec['sh_flags']&2 and sec['sh_type']!='SHT_NOBITS':u.mem_write(sec['sh_addr'],sec.data())
 for reg,value in [(UC_ARM_REG_R0,0x20032000),(UC_ARM_REG_R1,0x200010f8),(UC_ARM_REG_R2,83),(UC_ARM_REG_LR,0),(UC_ARM_REG_SP,0x2003d000)]:u.reg_write(reg,value)
 u.mem_write(0x2003213e,b'\0');u.mem_write(0x40001400,b'\1\0\0\0')
 if 'write' in kind:u.hook_add(UC_HOOK_MEM_WRITE,lambda *args:None)
 if 'code' in kind:u.hook_add(UC_HOOK_CODE,lambda *args:None)
 if 'read' in kind:u.hook_add(UC_HOOK_MEM_READ,lambda *args:None,begin=0x40001400,end=0x40001403)
 u.emu_start(0xa703,0xa728,count=100)
 results.append(dict(observer=kind,crcValidity=u.reg_read(UC_ARM_REG_R10)))
report=dict(at=datetime.datetime.now().astimezone().isoformat(),emulatorVersion=unicorn.__version__,
 elfSha256=hashlib.sha256(elf.read_bytes()).hexdigest(),cases=results,hardwareOperations=0,
 observerChangesResult=results[0]['crcValidity']!=results[1]['crcValidity'],
 scope='Identical actual ARM instruction fragment and register/MMIO fixtures; only no-op observer hooks differ. Not an on-device CRC result, throughput result, or general invalidation of all historical replays.')
(R/'arm-write-observer-reproducer.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
