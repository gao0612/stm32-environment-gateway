# GitHub展示与发布

已准备：README架构图、版本记录、第三方来源、自动构建工作流、本机测试和硬件验收表。工作流只编译/测试，不下载板卡或部署服务；GitHub线上首次运行结果目前未知，不放假的绿色通过徽章。

需要你提供的真实材料：一张整机接线照片、正常显示照片、断网后DISK增长和恢复补传视频、实际验收CSV。可使用历史实物照片，但应标注拍摄时的版本，不当作1.1固件测试证据。

提交前：检查secrets.h已忽略；填写测试结果；核对README平台和版本；保留来源许可证。目录已存在Git仓库，无需重复初始化。

```powershell
git status --short
git add .
git diff --cached --stat
git commit -m "Improve gateway diagnostics, buffering and reconnect"
# 验收通过后再打稳定版本标签；先不要将未验收版本称为稳定版。
# git tag v1.1.0
```

目标仓库：[gao0612/stm32-environment-gateway](https://github.com/gao0612/stm32-environment-gateway)。

更新工程时，在自己的仓库提交并推送；不要推送到参考作者的仓库。将链接放到简历前，检查未登录时网页也能打开、架构图能显示、源代码可下载、Actions 日志可查看。首次发布的实际检查见[发布验证](发布验证.md)。
