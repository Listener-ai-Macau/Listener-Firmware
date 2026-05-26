# Physical WASD BLE HID validation

status=FAIL
port=COM5
expected_host_text='dwas'
captured_host_text=''
serial_error=''
missing_serial_lines=['WASD key press queued: source=key1.gpio45.d output=d', 'WASD key press queued: source=key2.gpio48.w output=w', 'WASD key press queued: source=key3.gpio47.a output=a', 'WASD key press queued: source=key4.gpio21.s output=s']

## Required serial lines
WASD key press queued: source=key1.gpio45.d output=d
WASD key press queued: source=key2.gpio48.w output=w
WASD key press queued: source=key3.gpio47.a output=a
WASD key press queued: source=key4.gpio21.s output=s

## Serial log
`
I (57043) audio_capture: frame captured count=5650 bytes=3616000I (302053) audio_capture: frame captured count=30150 bytes=19296000
I (302553) audio_capture: frame captured count=30200 bytes=19328000
I (303053) audio_capture: frame captured count=30250 bytes=19360000
I (303553) audio_capture: frame captured count=30300 bytes=19392000
I (304053) audio_capture: frame captured count=30350 bytes=19424000
I (304553) audio_capture: frame captured count=30400 bytes=19456000
I (305053) audio_capture: frame captured count=30450 bytes=19488000
I (305553) audio_capture: frame captured count=30500 bytes=19520000
I (306053) audio_capture: frame captured count=30550 bytes=19552000
I (306553) audio_capture: frame captured count=30600 bytes=19584000
I (307053) audio_capture: frame captured count=30650 bytes=19616000
I (307553) audio_capture: frame captured count=30700 bytes=19648000
I (308053) audio_capture: frame captured count=30750 bytes=19680000
I (308553) audio_capture: frame captured count=30800 bytes=19712000
I (309053) audio_capture: frame captured count=30850 bytes=19744000
I (309553) audio_capture: frame captured count=30900 bytes=19776000
I (310053) audio_capture: frame captured count=30950 bytes=19808000
I (310553) audio_capture: frame captured count=31000 bytes=19840000
I (311053) audio_capture: frame captured count=31050 bytes=19872000
I (311553) audio_capture: frame captured count=31100 bytes=19904000
I (312053) audio_capture: frame captured count=31150 bytes=19936000
I (312553) audio_capture: frame captured count=31200 bytes=19968000
I (313053) audio_capture: frame captured count=31250 bytes=20000000
I (313553) audio_capture: frame captured count=31300 bytes=20032000
I (314053) audio_capture: frame captured count=31350 bytes=20064000
I (314553) audio_capture: frame captured count=31400 bytes=20096000
I (315053) audio_capture: frame captured count=31450 bytes=20128000
I (315553) audio_capture: frame captured count=31500 bytes=20160000
I (316053) audio_capture: frame captured count=31550 bytes=20192000
I (316553) audio_capture: frame captured count=31600 bytes=20224000
I (317053) audio_capture: frame captured count=31650 bytes=20256000
I (317553) audio_capture: frame captured count=31700 bytes=20288000
I (318053) audio_capture: frame captured count=31750 bytes=20320000
I (318553) audio_capture: frame captured count=31800 bytes=20352000
I (319053) audio_capture: frame captured count=31850 bytes=20384000
I (319553) audio_capture: frame captured count=31900 bytes=20416000
I (320053) audio_capture: frame captured count=31950 bytes=20448000
I (320553) audio_capture: frame captured count=32000 bytes=20480000
I (321053) audio_capture: frame captured count=32050 bytes=20512000
I (321553) audio_capture: frame captured count=32100 bytes=20544000
I (322053) audio_capture: frame captured count=32150 bytes=20576000
I (322553) audio_capture: frame captured count=32200 bytes=20608000
I (323053) audio_capture: frame captured count=32250 bytes=20640000
I (323553) audio_capture: frame captured count=32300 bytes=20672000
I (324053) audio_capture: frame captured count=32350 bytes=20704000
I (324553) audio_capture: frame captured count=32400 bytes=20736000
I (325053) audio_capture: frame captured count=32450 bytes=20768000
I (325553) audio_capture: frame captured count=32500 bytes=20800000
I (326053) audio_capture: frame captured count=32550 bytes=20832000
I (326553) audio_capture: frame captured count=32600 bytes=20864000
I (327053) audio_capture: frame captured count=32650 bytes=20896000
I (327553) audio_capture: frame captured count=32700 bytes=20928000
I (328053) audio_capture: frame captured count=32750 bytes=20960000
I (328553) audio_capture: frame captured count=32800 bytes=20992000
I (329053) audio_capture: frame captured count=32850 bytes=21024000
I (329553) audio_capture: frame captured count=32900 bytes=21056000
I (330053) audio_capture: frame captured count=32950 bytes=21088000
I (330553) audio_capture: frame captured count=33000 bytes=21120000
I (331053) audio_capture: frame captured count=33050 bytes=21152000
I (331553) audio_capture: frame captured count=33100 bytes=21184000
I (332053) audio_capture: frame captured count=33150 bytes=21216000
I (332553) audio_capture: frame captured count=33200 bytes=21248000
I (333053) audio_capture: frame captured count=33250 bytes=21280000
I (333553) audio_capture: frame captured count=33300 bytes=21312000
I (334053) audio_capture: frame captured count=33350 bytes=21344000
I (334553) audio_capture: frame captured count=33400 bytes=21376000
I (335053) audio_capture: frame captured count=33450 bytes=21408000
I (335553) audio_capture: frame captured count=33500 bytes=21440000
I (336053) audio_capture: frame captured count=33550 bytes=21472000
I (336553) audio_capture: frame captured count=33600 bytes=21504000
I (337053) audio_capture: frame captured count=33650 bytes=21536000
I (337553) audio_capture: frame captured count=33700 bytes=21568000
I (338053) audio_capture: frame captured count=33750 bytes=21600000
I (338553) audio_capture: frame captured count=33800 bytes=21632000
I (339053) audio_capture: frame captured count=33850 bytes=21664000
I (339553) audio_capture: frame captured count=33900 bytes=21696000
I (340053) audio_capture: frame captured count=33950 bytes=21728000
I (340553) audio_capture: frame captured count=34000 bytes=21760000
I (341053) audio_capture: frame captured count=34050 bytes=21792000
I (341553) audio_capture: frame captured count=34100 bytes=21824000
I (342053) audio_capture: frame captured count=34150 bytes=21856000
I (342553) audio_capture: frame captured count=34200 bytes=21888000
I (343053) audio_capture: frame captured count=34250 bytes=21920000
I (343553) audio_capture: frame captured count=34300 bytes=21952000
I (344053) audio_capture: frame captured count=34350 bytes=21984000
I (344553) audio_capture: frame captured count=34400 bytes=22016000
I (345053) audio_capture: frame captured count=34450 bytes=22048000
I (345553) audio_capture: frame captured count=34500 bytes=22080000
I (346053) audio_capture: frame captured count=34550 bytes=22112000
I (346553) audio_capture: frame captured count=34600 bytes=22144000
I (347053) audio_capture: frame captured count=34650 bytes=22176000
I (347553) audio_capture: frame captured count=34700 bytes=22208000
I (348053) audio_capture: frame captured count=34750 bytes=22240000
I (348553) audio_capture: frame captured count=34800 bytes=22272000
I (349053) audio_capture: frame captured count=34850 bytes=22304000
I (349553) audio_capture: frame captured count=34900 bytes=22336000
I (350053) audio_capture: frame captured count=34950 bytes=22368000
I (350553) audio_capture: frame captured count=35000 bytes=22400000
I (351053) audio_capture: frame captured count=35050 bytes=22432000
I (351553) audio_capture: frame captured count=35100 bytes=22464000
I (352053) audio_capture: frame captured count=35150 bytes=22496000
I (352553) audio_capture: frame captured count=35200 bytes=22528000
I (353053) audio_capture: frame captured count=35250 bytes=22560000
I (353553) audio_capture: frame captured count=35300 bytes=22592000
I (354053) audio_capture: frame captured count=35350 bytes=22624000
I (354553) audio_capture: frame captured count=35400 bytes=22656000
I (355053) audio_capture: frame captured count=35450 bytes=22688000
I (355553) audio_capture: frame captured count=35500 bytes=22720000
I (356053) audio_capture: frame captured count=35550 bytes=22752000
I (356553) audio_capture: frame captured count=35600 bytes=22784000
I (357053) audio_capture: frame captured count=35650 bytes=22816000
I (357553) audio_capture: frame captured count=35700 bytes=22848000
I (358053) audio_capture: frame captured count=35750 bytes=22880000
I (358553) audio_capture: frame captured count=35800 bytes=22912000
I (359053) audio_capture: frame captured count=35850 bytes=22944000
I (359553) audio_capture: frame captured count=35900 bytes=22976000
I (360053) audio_capture: frame captured count=35950 bytes=23008000
I (360553) audio_capture: frame captured count=36000 bytes=23040000
I (360683) ble_hid: battery level=90 voltage_mv=4082 raw=2366 reason=periodic
I (361053) audio_capture: frame captured count=36050 bytes=23072000
I (361553) audio_capture: frame captured count=36100 bytes=23104000
I (362053) audio_capture: frame captured count=36150 bytes=23136000
I (362553) audio_capture: frame captured count=36200 bytes=23168000
I (363053) audio_capture: frame captured count=36250 bytes=23200000
I (363553) audio_capture: frame captured count=36300 bytes=23232000
I (364053) audio_capture: frame captured count=36350 bytes=23264000

`
