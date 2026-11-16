# CubeMX配置与工程已接通
+
+当前唯一配置入口：工程根目录gateway.ioc。旧docs/cubemx_base.ioc仅留作历史参考。不要在旧的xuexi工程里生成后期望新工程自动更新。
+
+## 本次核对结果
+
+原复制件保留旧日期、没有SPI2/IWDG，复制本身正常，新增配置尚未保存进该文件。本次按已确认接线补齐SPI2：PB13/PB14/PB15，PB12=FLASH_CS、高电平；IWDG分频256、Reload499。保留现有W5500、屏幕、USART3、DMA和按键引脚；PLLQ设7，使48MHz域正确。用本机CubeMX6.11.1和F4 V1.28.3实际生成，不是只手写外设代码。
+
+## 日后具体怎样操作
+
+1. 关闭先前打开的旧配置窗口（不要把旧内存内容重新保存覆盖磁盘），重新打开Desktop/gateway/gateway.ioc。
+2. 在CubeMX中调整并按Ctrl+S保存。保存只修改ioc，不会改C文件。
+3. 在VS Code打开gateway整个文件夹，选择Terminal→Run Task→Regenerate and Build。
+4. 或在工程根目录的PowerShell执行：
+
+```powershell
+python tools/regenerate.py
+cmake --preset Debug
+cmake --build --preset Debug
+```
+
+如果CubeMX安装在其他位置，使用python tools/regenerate.py --cubemx "实际安装目录"。需要Python3；现有工程已使用Python执行测试。
+
+目前推荐这个安全入口，不直接使用CubeMX GUI的Generate Code向根目录覆盖。脚本在build/cubemx下生成临时工程，再合并Core用户区、修复旧CubeMX的绝对/重复路径问题。CubeMX6.11的Windows路径错误可能导致生成日志含sysmem/syscalls缺失提示；本工程保留原有这两个系统文件，并由实际编译检查它们可用。
+
+## 初始化只有一处
+
+- SPI2的句柄和引脚在Core/Src/spi.c，由main中的MX_SPI2_Init启动一次。w25q64.c只用hspi2通信和检查JEDEC，不再自己配置GPIO/SPI。
+- Flash片选来自生成的main.h标签FLASH_CS，经App/Config/board_pins.h引用，不再另写一套GPIOB/PIN12数值。
+- 看门狗参数在生成的Core/Src/iwdg.c，AppWatchdog_Start在任务创建完成时调用MX_IWDG_Init一次。生成器默认插入main的早期启动调用由合并脚本移除。
+- 复位原因采集在main的USER CODE BEGIN 1，早于HAL_Init；Gateway_Init在USER CODE BEGIN 2。
+- freertos.c恢复CubeMX完整框架，RTOS_THREADS用户区只调用一次Gateway_StartTasks。defaultTask是CubeMX占位任务，启动后退出，不重复运行网关任务。
+- 栈溢出和malloc失败钩子进入Error_Handler，已启动的IWDG可复位。
+
+## 不会被新生成工程替换的内容
+
+App、LVGL、WIZnet等库继续使用现有版本。cmake/project.cmake保存应用构建清单，根CMakeLists由它恢复；修改应用源文件清单时同时维护该文件。
+
+实际链接脚本改为cmake/gateway_flash.ld，仍然保留768KB程序区、扇区10/11的A/B参数区。根目录STM32F407ZGTx_FLASH.ld保留作对照，不作为唯一保护措施。不能用CubeMX默认1MB程序区替换参数分区。
+
+脚本每次合并前将待替换文件备份到build/regeneration_backup。Git里保留源代码，build里的生成日志和备份按.gitignore排除。
+
+## 验证范围
+
+已检查配置→实际生成→用户区合并→ARM编译，并再次运行安全生成入口验证回合。电脑端存储、MQTT、网络及新增启动检查一并运行。未自动烧录板卡，没有将此轮编译当作上板通过。
+