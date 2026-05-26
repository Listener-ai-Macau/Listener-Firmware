# Physical WASD BLE HID validation

status=FAIL
port=COM5
expected_host_text='dwas'
captured_host_text=''
serial_error=''
missing_raw_sources=['WASD key raw transition: source=key1.gpio45.d', 'WASD key raw transition: source=key2.gpio48.w', 'WASD key raw transition: source=key3.gpio47.a', 'WASD key raw transition: source=key4.gpio21.s']
missing_serial_lines=['WASD key press queued: source=key1.gpio45.d output=d', 'WASD key press queued: source=key2.gpio48.w output=w', 'WASD key press queued: source=key3.gpio47.a output=a', 'WASD key press queued: source=key4.gpio21.s output=s']

## Required raw GPIO transition sources
WASD key raw transition: source=key1.gpio45.d
WASD key raw transition: source=key2.gpio48.w
WASD key raw transition: source=key3.gpio47.a
WASD key raw transition: source=key4.gpio21.s

## Required serial lines
WASD key press queued: source=key1.gpio45.d output=d
WASD key press queued: source=key2.gpio48.w output=w
WASD key press queued: source=key3.gpio47.a output=a
WASD key press queued: source=key4.gpio21.s output=s

## Serial log
```
I (487543) audio_capture: frame captured count=48700 bytes=31168I (723553) audio_capture: frame captured count=72300 bytes=46272000
I (724053) audio_capture: frame captured count=72350 bytes=46304000
I (724553) audio_capture: frame captured count=72400 bytes=46336000
I (725053) audio_capture: frame captured count=72450 bytes=46368000
I (725553) audio_capture: frame captured count=72500 bytes=46400000
I (726053) audio_capture: frame captured count=72550 bytes=46432000
I (726553) audio_capture: frame captured count=72600 bytes=46464000
I (727053) audio_capture: frame captured count=72650 bytes=46496000
I (727553) audio_capture: frame captured count=72700 bytes=46528000
I (728053) audio_capture: frame captured count=72750 bytes=46560000
I (728553) audio_capture: frame captured count=72800 bytes=46592000
I (729053) audio_capture: frame captured count=72850 bytes=46624000
I (729553) audio_capture: frame captured count=72900 bytes=46656000
I (730053) audio_capture: frame captured count=72950 bytes=46688000
I (730553) audio_capture: frame captured count=73000 bytes=46720000
I (731053) audio_capture: frame captured count=73050 bytes=46752000
I (731553) audio_capture: frame captured count=73100 bytes=46784000
I (732053) audio_capture: frame captured count=73150 bytes=46816000
I (732553) audio_capture: frame captured count=73200 bytes=46848000
I (733053) audio_capture: frame captured count=73250 bytes=46880000
I (733553) audio_capture: frame captured count=73300 bytes=46912000
I (734053) audio_capture: frame captured count=73350 bytes=46944000
I (734553) audio_capture: frame captured count=73400 bytes=46976000
I (735053) audio_capture: frame captured count=73450 bytes=47008000
I (735553) audio_capture: frame captured count=73500 bytes=47040000
I (736053) audio_capture: frame captured count=73550 bytes=47072000
I (736553) audio_capture: frame captured count=73600 bytes=47104000
I (737053) audio_capture: frame captured count=73650 bytes=47136000
I (737553) audio_capture: frame captured count=73700 bytes=47168000
I (738053) audio_capture: frame captured count=73750 bytes=47200000
I (738553) audio_capture: frame captured count=73800 bytes=47232000
I (739053) audio_capture: frame captured count=73850 bytes=47264000
I (739553) audio_capture: frame captured count=73900 bytes=47296000
I (740053) audio_capture: frame captured count=73950 bytes=47328000
I (740553) audio_capture: frame captured count=74000 bytes=47360000
I (741053) audio_capture: frame captured count=74050 bytes=47392000
I (741553) audio_capture: frame captured count=74100 bytes=47424000
I (742053) audio_capture: frame captured count=74150 bytes=47456000
I (742553) audio_capture: frame captured count=74200 bytes=47488000
I (743053) audio_capture: frame captured count=74250 bytes=47520000
I (743553) audio_capture: frame captured count=74300 bytes=47552000
I (744053) audio_capture: frame captured count=74350 bytes=47584000
I (744553) audio_capture: frame captured count=74400 bytes=47616000
I (745053) audio_capture: frame captured count=74450 bytes=47648000
I (745553) audio_capture: frame captured count=74500 bytes=47680000
I (746053) audio_capture: frame captured count=74550 bytes=47712000
I (746553) audio_capture: frame captured count=74600 bytes=47744000
I (747053) audio_capture: frame captured count=74650 bytes=47776000
I (747553) audio_capture: frame captured count=74700 bytes=47808000
I (748053) audio_capture: frame captured count=74750 bytes=47840000
I (748553) audio_capture: frame captured count=74800 bytes=47872000
I (749053) audio_capture: frame captured count=74850 bytes=47904000
I (749553) audio_capture: frame captured count=74900 bytes=47936000
I (750053) audio_capture: frame captured count=74950 bytes=47968000
I (750553) audio_capture: frame captured count=75000 bytes=48000000
I (751053) audio_capture: frame captured count=75050 bytes=48032000
I (751553) audio_capture: frame captured count=75100 bytes=48064000
I (752053) audio_capture: frame captured count=75150 bytes=48096000
I (752553) audio_capture: frame captured count=75200 bytes=48128000
I (753053) audio_capture: frame captured count=75250 bytes=48160000
I (753553) audio_capture: frame captured count=75300 bytes=48192000
I (754053) audio_capture: frame captured count=75350 bytes=48224000
I (754553) audio_capture: frame captured count=75400 bytes=48256000
I (755053) audio_capture: frame captured count=75450 bytes=48288000
I (755553) audio_capture: frame captured count=75500 bytes=48320000
I (756053) audio_capture: frame captured count=75550 bytes=48352000
I (756553) audio_capture: frame captured count=75600 bytes=48384000
I (757053) audio_capture: frame captured count=75650 bytes=48416000
I (757553) audio_capture: frame captured count=75700 bytes=48448000
I (758053) audio_capture: frame captured count=75750 bytes=48480000
I (758553) audio_capture: frame captured count=75800 bytes=48512000
I (759053) audio_capture: frame captured count=75850 bytes=48544000
I (759553) audio_capture: frame captured count=75900 bytes=48576000
I (760053) audio_capture: frame captured count=75950 bytes=48608000
I (760553) audio_capture: frame captured count=76000 bytes=48640000
I (761053) audio_capture: frame captured count=76050 bytes=48672000
I (761553) audio_capture: frame captured count=76100 bytes=48704000
I (762053) audio_capture: frame captured count=76150 bytes=48736000
I (762553) audio_capture: frame captured count=76200 bytes=48768000
I (763053) audio_capture: frame captured count=76250 bytes=48800000
I (763553) audio_capture: frame captured count=76300 bytes=48832000
I (764053) audio_capture: frame captured count=76350 bytes=48864000
I (764553) audio_capture: frame captured count=76400 bytes=48896000
I (765053) audio_capture: frame captured count=76450 bytes=48928000
I (765553) audio_capture: frame captured count=76500 bytes=48960000
I (766053) audio_capture: frame captured count=76550 bytes=48992000
I (766553) audio_capture: frame captured count=76600 bytes=49024000
I (767053) audio_capture: frame captured count=76650 bytes=49056000
I (767553) audio_capture: frame captured count=76700 bytes=49088000
I (768053) audio_capture: frame captured count=76750 bytes=49120000
I (768553) audio_capture: frame captured count=76800 bytes=49152000
I (769053) audio_capture: frame captured count=76850 bytes=49184000
I (769553) audio_capture: frame captured count=76900 bytes=49216000
I (770053) audio_capture: frame captured count=76950 bytes=49248000
I (770553) audio_capture: frame captured count=77000 bytes=49280000
I (771053) audio_capture: frame captured count=77050 bytes=49312000
I (771553) audio_capture: frame captured count=77100 bytes=49344000
I (772053) audio_capture: frame captured count=77150 bytes=49376000
I (772553) audio_capture: frame captured count=77200 bytes=49408000
I (773053) audio_capture: frame captured count=77250 bytes=49440000
I (773553) audio_capture: frame captured count=77300 bytes=49472000
I (774053) audio_capture: frame captured count=77350 bytes=49504000
I (774553) audio_capture: frame captured count=77400 bytes=49536000
I (775053) audio_capture: frame captured count=77450 bytes=49568000
I (775553) audio_capture: frame captured count=77500 bytes=49600000
I (776053) audio_capture: frame captured count=77550 bytes=49632000
I (776553) audio_capture: frame captured count=77600 bytes=49664000
I (777053) audio_capture: frame captured count=77650 bytes=49696000
I (777553) audio_capture: frame captured count=77700 bytes=49728000
I (778053) audio_capture: frame captured count=77750 bytes=49760000
I (778553) audio_capture: frame captured count=77800 bytes=49792000
I (779053) audio_capture: frame captured count=77850 bytes=49824000
I (779553) audio_capture: frame captured count=77900 bytes=49856000
I (780053) audio_capture: frame captured count=77950 bytes=49888000
I (780553) audio_capture: frame captured count=78000 bytes=49920000
I (780683) ble_hid: battery level=96 voltage_mv=4156 raw=2411 reason=periodic
I (781053) audio_capture: frame captured count=78050 bytes=49952000
I (781553) audio_capture: frame captured count=78100 bytes=49984000
I (782053) audio_capture: frame captured count=78150 bytes=50016000
I (782553) audio_capture: frame captured count=78200 bytes=50048000
I (783053) audio_capture: frame captured count=78250 bytes=50080000
I (783553) audio_capture: frame captured count=78300 bytes=50112000
I (784053) audio_capture: frame captured count=78350 bytes=50144000
I (784553) audio_capture: frame captured count=78400 bytes=50176000
I (785053) audio_capture: frame captured count=78450 bytes=50208000
I (785553) audio_capture: frame captured count=78500 bytes=50240000
I (786053) audio_capture: frame captured count=78550 bytes=50272000
I (786553) audio_capture: frame captured count=78600 bytes=50304000
I (787053) audio_capture: frame captured count=78650 bytes=50336000
I (787553) audio_capture: frame captured count=78700 bytes=50368000
I (788053) audio_capture: frame captured count=78750 bytes=50400000
I (788553) audio_capture: frame captured count=78800 bytes=50432000
I (789053) audio_capture: frame captured count=78850 bytes=50464000
I (789553) audio_capture: frame captured count=78900 bytes=50496000
I (790053) audio_capture: frame captured count=78950 bytes=50528000
I (790553) audio_capture: frame captured count=79000 bytes=50560000
I (791053) audio_capture: frame captured count=79050 bytes=50592000
I (791553) audio_capture: frame captured count=79100 bytes=50624000
I (792053) audio_capture: frame captured count=79150 bytes=50656000
I (792553) audio_capture: frame captured count=79200 bytes=50688000
I (793053) audio_capture: frame captured count=79250 bytes=50720000
I (793553) audio_capture: frame captured count=79300 bytes=50752000
I (794053) audio_capture: frame captured count=79350 bytes=50784000
I (794553) audio_capture: frame captured count=79400 bytes=50816000
I (795053) audio_capture: frame captured count=79450 bytes=50848000
I (795553) audio_capture: frame captured count=79500 bytes=50880000
I (796053) audio_capture: frame captured count=79550 bytes=50912000
I (796553) audio_capture: frame captured count=79600 bytes=50944000
I (797053) audio_capture: frame captured count=79650 bytes=50976000
I (797553) audio_capture: frame captured count=79700 bytes=51008000
I (798053) audio_capture: frame captured count=79750 bytes=51040000
I (798553) audio_capture: frame captured count=79800 bytes=51072000
I (799053) audio_capture: frame captured count=79850 bytes=51104000
I (799553) audio_capture: frame captured count=79900 bytes=51136000
I (800053) audio_capture: frame captured count=79950 bytes=51168000
I (800553) audio_capture: frame captured count=80000 bytes=51200000
I (801053) audio_capture: frame captured count=80050 bytes=51232000
I (801553) audio_capture: frame captured count=80100 bytes=51264000
I (802053) audio_capture: frame captured count=80150 bytes=51296000
I (802553) audio_capture: frame captured count=80200 bytes=51328000
I (803053) audio_capture: frame captured count=80250 bytes=51360000
I (803553) audio_capture: frame captured count=80300 bytes=51392000
I (804053) audio_capture: frame captured count=80350 bytes=51424000
I (804553) audio_capture: frame captured count=80400 bytes=51456000
I (805053) audio_capture: frame captured count=80450 bytes=51488000
I (805553) audio_capture: frame captured count=80500 bytes=51520000
I (806053) audio_capture: frame captured count=80550 bytes=51552000
I (806553) audio_capture: frame captured count=80600 bytes=51584000
I (807053) audio_capture: frame captured count=80650 bytes=51616000
I (807553) audio_capture: frame captured count=80700 bytes=51648000
I (808053) audio_capture: frame captured count=80750 bytes=51680000
I (808553) audio_capture: frame captured count=80800 bytes=51712000
I (809053) audio_capture: frame captured count=80850 bytes=51744000
I (809553) audio_capture: frame captured count=80900 bytes=51776000
I (810053) audio_capture: frame captured count=80950 bytes=51808000
I (810553) audio_capture: frame captured count=81000 bytes=51840000
I (811053) audio_capture: frame captured count=81050 bytes=51872000
I (811553) audio_capture: frame captured count=81100 bytes=51904000
I (812053) audio_capture: frame captured count=81150 bytes=51936000
I (812553) audio_capture: frame captured count=81200 bytes=51968000
I (813053) audio_capture: frame captured count=81250 bytes=52000000
I (813553) audio_capture: frame captured count=81300 bytes=52032000
I (814053) audio_capture: frame captured count=81350 bytes=52064000
I (814553) audio_capture: frame captured count=81400 bytes=52096000
I (815053) audio_capture: frame captured count=81450 bytes=52128000
I (815553) audio_capture: frame captured count=81500 bytes=52160000
I (816053) audio_capture: frame captured count=81550 bytes=52192000
I (816553) audio_capture: frame captured count=81600 bytes=52224000
I (817053) audio_capture: frame captured count=81650 bytes=52256000
I (817553) audio_capture: frame captured count=81700 bytes=52288000
I (818053) audio_capture: frame captured count=81750 bytes=52320000
I (818553) audio_capture: frame captured count=81800 bytes=52352000
I (819053) audio_capture: frame captured count=81850 bytes=52384000
I (819553) audio_capture: frame captured count=81900 bytes=52416000
I (820053) audio_capture: frame captured count=81950 bytes=52448000
I (820553) audio_capture: frame captured count=82000 bytes=52480000
I (821053) audio_capture: frame captured count=82050 bytes=52512000
I (821553) audio_capture: frame captured count=82100 bytes=52544000
I (822053) audio_capture: frame captured count=82150 bytes=52576000
I (822553) audio_capture: frame captured count=82200 bytes=52608000
I (823053) audio_capture: frame captured count=82250 bytes=52640000
I (823553) audio_capture: frame captured count=82300 bytes=52672000
I (824053) audio_capture: frame captured count=82350 bytes=52704000
I (824553) audio_capture: frame captured count=82400 bytes=52736000
I (825053) audio_capture: frame captured count=82450 bytes=52768000
I (825553) audio_capture: frame captured count=82500 bytes=52800000
I (826053) audio_capture: frame captured count=82550 bytes=52832000
I (826553) audio_capture: frame captured count=82600 bytes=52864000
I (827053) audio_capture: frame captured count=82650 bytes=52896000
I (827553) audio_capture: frame captured count=82700 bytes=52928000
I (828053) audio_capture: frame captured count=82750 bytes=52960000
I (828553) audio_capture: frame captured count=82800 bytes=52992000
I (829053) audio_capture: frame captured count=82850 bytes=53024000
I (829553) audio_capture: frame captured count=82900 bytes=53056000
I (830053) audio_capture: frame captured count=82950 bytes=53088000
I (830553) audio_capture: frame captured count=83000 bytes=53120000
I (831053) audio_capture: frame captured count=83050 bytes=53152000
I (831553) audio_capture: frame captured count=83100 bytes=53184000
I (832053) audio_capture: frame captured count=83150 bytes=53216000
I (832553) audio_capture: frame captured count=83200 bytes=53248000
I (833053) audio_capture: frame captured count=83250 bytes=53280000
I (833553) audio_capture: frame captured count=83300 bytes=53312000
I (834053) audio_capture: frame captured count=83350 bytes=53344000
I (834553) audio_capture: frame captured count=83400 bytes=53376000
I (835053) audio_capture: frame captured count=83450 bytes=53408000
I (835553) audio_capture: frame captured count=83500 bytes=53440000
I (836053) audio_capture: frame captured count=83550 bytes=53472000
I (836553) audio_capture: frame captured count=83600 bytes=53504000
I (837053) audio_capture: frame captured count=83650 bytes=53536000
I (837553) audio_capture: frame captured count=83700 bytes=53568000
I (838053) audio_capture: frame captured count=83750 bytes=53600000
I (838553) audio_capture: frame captured count=83800 bytes=53632000
I (839053) audio_capture: frame captured count=83850 bytes=53664000
I (839553) audio_capture: frame captured count=83900 bytes=53696000
I (840053) audio_capture: frame captured count=83950 bytes=53728000
I (840553) audio_capture: frame captured count=84000 bytes=53760000
I (840683) ble_hid: battery level=95 voltage_mv=4136 raw=2399 reason=periodic
I (841053) audio_capture: frame captured count=84050 bytes=53792000
I (841553) audio_capture: frame captured count=84100 bytes=53824000
I (842053) audio_capture: frame captured count=84150 bytes=53856000
I (842553) audio_capture: frame captured count=84200 bytes=53888000
I (843053) audio_capture: frame captured count=84250 bytes=53920000
I (843553) audio_capture: frame captured count=84300 bytes=53952000
I (844053) audio_capture: frame captured count=84350 bytes=53984000
I (844553) audio_capture: frame captured count=84400 bytes=54016000
I (845053) audio_capture: frame captured count=84450 bytes=54048000
I (845553) audio_capture: frame captured count=84500 bytes=54080000

```
