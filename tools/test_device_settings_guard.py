"""Check that the settings-preservation guard rejects real forbidden regressions.
Mutations exist only in ignored test copies, never in product source files.
"""
import json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'tools/verify_device_settings_static.ps1').read_text(encoding='utf-8-sig')
copies=root/'.cache/device-settings-guard-counterexamples';copies.mkdir(parents=True,exist_ok=True)
cases=[
 ('nvs_erase_rejected','$firmwareOta = Read-RepoFile "components/firmware_ota/firmware_ota.c"','\n$firmwareOta += "`nvoid bad(void) { nvs_flash_erase(); }"','must not reset'),
 ('other_partition_erase_rejected','$firmwareOta = Read-RepoFile "components/firmware_ota/firmware_ota.c"','\n$firmwareOta += "`nvoid bad(void) { esp_partition_erase_range(other_partition, 0, 4096); }"','must not reset'),
 ('revision_only_ble_rejected','$bleAudio = Read-RepoFile "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c"',"\n$bleAudio = $bleAudio.Replace('device_settings_format_ble_snapshot(', 'denzic_device_control_v1_format_settings_revision(')",'complete authoritative device snapshot'),
]
observations=[]
for label,marker,mutation,expected in cases:
 assert source.count(marker)==1
 candidate=source.replace(marker,marker+mutation,1)
 path=copies/(label+'.ps1');path.write_text(candidate,encoding='utf-8')
 result=subprocess.run(['pwsh','-NoProfile','-File',str(path),'-RepoRoot',str(root)],capture_output=True,text=True,timeout=20)
 text=result.stdout+result.stderr
 assert result.returncode!=0 and expected in text,(label,result.returncode,text)
 observations.append({'counterexample':label,'rejected':True})
print(json.dumps({'scope':__doc__,'checks':observations,'passed':True}))
