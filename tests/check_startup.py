from pathlib import Path
import re
r=Path(__file__).resolve().parents[1]
def read(n):return (r/n).read_text(encoding='utf-8-sig')
main=read('Core/Src/main.c');rtos=read('Core/Src/freertos.c')
assert main.count('AppWatchdog_CaptureReset();')==1
assert main.index('AppWatchdog_CaptureReset();')<main.index('HAL_Init();')
assert main.count('MX_SPI2_Init();')==1
assert 'MX_IWDG_Init();' not in main
assert main.count('Gateway_Init();')==1 and rtos.count('Gateway_StartTasks();')==1
assert 'osThreadExit();' in rtos
flash=read('App/Drivers/w25q64.c')
assert '&hspi2' in flash and 'HAL_SPI_Init(' not in flash and 'static SPI_HandleTypeDef' not in flash
assert read('App/Drivers/app_watchdog.c').count('MX_IWDG_Init();')==1
assert 'cmake/gateway_flash.ld' in read('cmake/gcc-arm-none-eabi.cmake')
ld=read('cmake/gateway_flash.ld')
assert '0x080C0000' in ld and '0x080E0000' in ld and '768K' in ld
assert not re.search(r'[A-Za-z]:[/\\]',read('cmake/stm32cubemx/CMakeLists.txt'))
ioc=read('gateway.ioc')
for value in ['PB12.GPIO_Label=FLASH_CS','PB13.Signal=SPI2_SCK','PB14.Signal=SPI2_MISO','PB15.Signal=SPI2_MOSI','IWDG.Reload=499']:
 assert value in ioc,value
print('PASS generated startup ownership, user entry points and protected Flash partition')
