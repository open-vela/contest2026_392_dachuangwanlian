1. 解压SDK (decompress sdk)

tar hxvf bes-aos-nuttx_*.tar.gz
tar hxvf bes-aos-ddk_*.tar.gz -C framework/services/

2.参考rtos/nuttx/README.md 安装必要依赖工具，比如arm-none-eabi-gcc等 (Install build tools)

example:
sudo apt install make gcc g++ bison libgmp3-dev byacc libmpfr-dev gperf libmpc-dev flex gdb automake libncurses-dev libgmp-dev curl genromfs
sudo apt install gcc-arm-none-eabi

3. 编译&烧录 (build and bron)

3.1 1600 chip example
$./build.sh boards/best1600_ep/aos_evb/configs/ap -j8
$./build.sh boards/best1600_ep/aos_evb/configs/cp -j8
$./build.sh boards/best1600_ep/aos_evb/configs/sensor/ -j8

$.\prebuild\m1\dldtool.exe 23 .\prebuild\m1\programmer.bin -M rtos\nuttx\nuttx_ap.bin --pgm-rate 2000000
$.\prebuild\m1\dldtool.exe 23 .\prebuild\m1\programmer.bin -M rtos\nuttx\nuttx_cp.bin --pgm-rate 2000000
$.\prebuild\m1\dldtool.exe 23 .\prebuild\m1\programmer.bin --addr 0x2cf00000 -m 0x2cf00000/0xbe57ec1c rtos\nuttx\nuttx_sensor.bin --pgm-rate 2000000

3.2 1502x chip example
$./build.sh boards/best1502x_ep/evb/configs/ap_smp -j8
$./build.sh boards/best1502x_ep/evb/configs/sensor/ -j8

$.\prebuild\m1\dldtool.exe 23 .\prebuild\programmer1502x.bin -M rtos\nuttx\nuttx_ap.bin -M rtos\nuttx\nuttx_sensor.bin --pgm-rate 2000000

3.3 1306 chip example
$./build.sh boards/best1306_ep/evb/configs/ap_smp -j8

$.\prebuild\m1\dldtool.exe 23 .\prebuild\programmer1306.bin -M rtos\nuttx\nuttx_ap.bin 2000000

3.4 2003 chip example
$./build.sh boards/best2003_ep/aos_evb_ax4d/configs/ap -j
$./build.sh boards/best2003_ep/aos_evb_ax4d/configs/audio/  -j

$.\prebuild\m0\dldtool.exe 23 .\prebuild\m0\programmer2003.bin -M rtos\nuttx\nuttx_ap.bin --addr 0x2C800000 rtos\nuttx\nuttx_a7.bin --pgm-rate 2000000

更多文档可参考
1. doc/目录
2. online doc
https://nuttx.apache.org/docs/latest/index.html
https://cwiki.apache.org/confluence/display/NUTTX/Wiki