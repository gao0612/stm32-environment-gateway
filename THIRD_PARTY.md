# 第三方组件和参考资料

- 参考功能：zgd 网关 V2.0，MIT，Copyright (c) 2026 zgd。链接和适配说明见 docs/来源与差异.md。
- ST STM32F4 HAL、生成的启动/外设代码：保留文件头；HAL的 LICENSE.txt 说明独立分发时适用 BSD-3-Clause。其他ST文件按自己的文件头。
- Arm CMSIS：保留 Apache-2.0 标识和各文件头。
- FreeRTOS Kernel 10.3.1：MIT，许可全文保存在内核源码文件头。
- LVGL：MIT，见 Middlewares/lvgl/LICENCE.txt。
- WIZnet ioLibrary：见 ioLibrary_Driver/license.txt，保留源码文件头。
- MQTT实现核对依据：[OASIS MQTT3.1.1规范](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)。本仓库未复制规范正文。

根目录MIT只覆盖本工程的应用整理/新增代码，不替代上述第三方许可证。对组件进行二进制分发时仍需保留对应许可和版权告知。
