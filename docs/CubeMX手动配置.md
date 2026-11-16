> 更新：配置入口、SPI2/IWDG统一初始化和保护脚本已经接通。日常操作以[安全生成说明](CubeMX生成与工程维护.md)为准；下面保留参数对照，旧的“暂不生成/手动初始化”描述不再代表当前实现。

# CubeMX逐项手动配置（F407ZGT6）

本次仅写此说明，未替你修改.ioc、Core生成文件或统一引脚代码。当前代码仍可按原CMake构建。

## 1 建立配置副本

不要直接向现有gateway目录生成。复制docs/cubemx_base.ioc到新的临时文件夹，用CubeMX打开，确认MCU=STM32F407ZGTx。第一次生成到另一个临时目录，完成下列设置后对比外设参数。

## 2 系统与时钟

- SYS：Debug选择Serial Wire，释放PB3/PA15；Timebase Source选择TIM6，FreeRTOS仍使用SysTick。
- RCC：HSE选择Crystal/Ceramic Resonator（板载8MHz晶振）；开启LSI供IWDG使用。若实际板是外部有源时钟才选择Bypass。
- Clock Configuration：PLLM=8，PLLN=336，PLLP=2，PLLQ=7；SYSCLK/AHB=168MHz，APB1=42MHz、APB2=84MHz。TIM2输入84MHz。PLLQ=7对应48MHz域；本工程未使用USB，但不要照抄旧.ioc的84MHz“48MHz域”显示。

## 3 通信外设

|项目|选择与参数|引脚|
|---|---|---|
|USART3|Asynchronous，9600，8位，无校验，1停止位，无流控|PD8 TX、PD9 RX|
|SPI1/W5500|Master，全双工，8位，MSB，Mode0，Software NSS，分频32=2.625MHz|PA5 SCK、PA6 MISO、PA7 MOSI|
|SPI3/屏幕|Master，全双工，8位，MSB，Mode0，Software NSS，分频8=5.25MHz|PB3 SCK、PB5 MOSI；MISO不用，不接屏幕|
|SPI2/Flash|Master，全双工，8位，MSB，Mode0，Software NSS，分频4=10.5MHz|PB13 SCK、PB14 MISO、PB15 MOSI|

Mode0表示CPOL Low、CPHA 1 Edge；关闭SPI硬件CRC。SPI3如CubeMX默认强制分配PB4为MISO，可保持引脚不接外设，但避免与其他用途冲突；当前源码只初始化SCK/MOSI。

## 4 GPIO：标签与初始电平

|引脚|模式/上拉|建议标签|初始电平|
|---|---|---|---|
|PA4|Output Push Pull|W5500_CS|High|
|PC4|Output Push Pull|W5500_RST|High|
|PD10|Output Push Pull|RS485_DE|Low（接收）|
|PD11|Output Push Pull|LCD_CS|High|
|PD12|Output Push Pull|LCD_DC|Low|
|PD13|Output Push Pull|LCD_BL|Low，初始化完再亮|
|PB12|Output Push Pull|FLASH_CS|High|
|PC13|Output Push Pull|USER_LED|按原板LED极性|
|PA15|External Interrupt Falling、Pull-up|USER_KEY|输入|

Output可设High speed；SPI复用脚Very High。WP/HOLD接3.3V；普通数字接口必须共地。

## 5 DMA与中断

- USART3_RX：DMA1 Stream1 Channel4，Peripheral to Memory，Circular；外设不递增、内存递增，Byte/Byte，High priority，FIFO Disable。
- SPI3_TX：DMA1 Stream5 Channel0，Memory to Peripheral，Normal；外设不递增、内存递增，Byte/Byte，Low priority，FIFO Disable。
- NVIC启用USART3、DMA1_Stream1、DMA1_Stream5、EXTI15_10，中断抢占优先级均为6，子优先级0。优先级分组4。
- SysTick、PendSV为15。TIM6按原HAL timebase生成配置；不要把HAL毫秒计数改为和RTOS争用SysTick。
- CMSIS任务通知使用RTOS API，中断数值优先级不能高于MAX_SYSCALL阈值（本工程5）。用6与现有工程一致。
- 检查生成的USART/DMA/EXTI中断函数分别调用相应HAL处理函数，保留现有接收IDLE路径。循环DMA的HT/TC与IDLE回调不能因重生成丢失。

## 6 TIM2和IWDG

- TIM2：Internal Clock、向上计数、Prescaler=83、Period=4294967295，不启用定时更新中断；84MHz/(83+1)=1MHz，每计数1微秒。
- IWDG：Prescaler=256、Reload=499，名义32kHz LSI下约4秒，实际随LSI偏差变化。不要勾选IWDG硬件自动启动选项字节。
- 当前App/Drivers/app_watchdog.c自己初始化IWDG。CubeMX若生成MX_IWDG_Init，先不要把它插到main中重复调用；保留Gateway_StartTasks中的统一启动位置。

## 7 FreeRTOS

- Middleware选择FreeRTOS，CMSIS_V2，动态分配heap_4，Tick=1000Hz，TOTAL_HEAP_SIZE=32768；保留栈溢出检查2及栈水位查询。
- 不要在CubeMX里再重复创建sensor/network/screen等业务任务。现有App/Tasks负责创建。
- 若CubeMX强制生成defaultTask，将其删除逻辑/退出处理放入用户区，不能把一整套任务重复创建两次。
- 将gateway.h放在freertos.c的USER CODE BEGIN Includes里；在MX_FREERTOS_Init内合适的USER CODE BEGIN RTOS_THREADS区调用一次Gateway_StartTasks()。核对生成前后具体标记名称。

## 8 生成后怎样合并

先开启Keep User Code when re-generating，外设生成独立.c/.h。对比临时生成的Core，人工合并，不覆盖App、库、CMake和链接脚本。main中顺序为外设初始化→Gateway_Init→osKernelInitialize→创建任务→osKernelStart。

SPI2特别注意：当前w25q64.c使用自己的static flash_spi，且手动配置SPI2时钟/引脚。你有两个选择：A只在.ioc记录SPI2，暂不把生成的MX_SPI2_Init接入main，保留当前驱动；B后续改驱动使用生成的hspi2、由MX_SPI2_Init唯一初始化，并删除驱动内重复GPIO/HAL_SPI_Init。不能两套句柄同时操作同一外设。本轮推荐A，待你配置完成再做B的代码迁移。

片内Flash链接分区仍保持768KB程序＋扇区10/11参数区。不要用新生成的默认1MB链接脚本覆盖STM32F407ZGTx_FLASH.ld。

## 9 统一引脚表，留给你手动实施

目前Core/Inc/main.h已定义W5500/LCD/RS485等标签，Flash驱动仍直接用PB12。建议你自己创建App/Config/board_pins.h，只做下面这层映射：

```c
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#include "main.h"
#define FLASH_CS_PORT GPIOB
#define FLASH_CS_PIN  GPIO_PIN_12
/* 其他外设优先引用main.h现有标签，不复制出第二套数值。 */
#define SCREEN_CS_PORT LCD_CS_GPIO_Port
#define SCREEN_CS_PIN  LCD_CS_Pin
#endif
```

之后将w25q64.c片选操作里的GPIOB/GPIO_PIN_12替换成上述宏；SPI的复用引脚最终交给生成的MSP初始化。不要为了统一表把一个引脚在main.h和board_pins.h各写成不同的数字。本次没有代你创建此头文件或改引脚，便于你按步骤练习。

核对依据：现有外设源码、原板.ioc和STM32F407参考手册RM0090。CubeMX不同版本的菜单位置可能不同，参数名称和最终生成结果才是核对重点。
