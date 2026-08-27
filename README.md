# CUDA Pi Calculator

这是一个以 CUDA 为唯一数值后端的圆周率终端程序。它在一个全屏 TUI 中显示计算出的 `3.14...`，不会把全部结果直接刷到普通控制台；底部会持续显示任务状态、CPU 与内存采样，以及可用时的 NVIDIA GPU 利用率、显存和温度。

程序**不会**使用 CPU 计算圆周率，也没有 CPU 回退路径。启动时会先调用 CUDA 枚举设备：没有 NVIDIA GPU、驱动不可用、GPU 没有透传到虚拟机，或 CUDA 初始化失败时，界面会明确显示 `GPU unavailable` 和具体驱动错误，开始计算操作会保持禁用状态。

## 支持范围

- Windows 10 / Windows 11 x64
- Ubuntu 22.04+ x64
- Debian 12+ x64
- NVIDIA CUDA GPU 与对应驱动

不构建或发布 ARM 版本。Linux 发布包由 Ubuntu 22.04 x64 构建，面向 Ubuntu 22.04+ 与 Debian 12+；Windows 发布包为 x64 ZIP。

## 计算方式

核心使用 Machin 公式：

```text
pi / 4 = 4 * atan(1 / 5) - atan(1 / 239)
```

每个反正切项使用基数 `10^4` 的定点向量表示。初始化、级数项更新、进位、借位、Machin 合并和最终缩放均在 CUDA 内核中执行。主机端只负责任务控制、TUI 绘制、系统负载采样，以及在完成后把 GPU 结果格式化为显示文本。

当前精度范围是小数点后 10 到 10,000 位。GPU 内核使用额外保护位，输出前会截去保护位。

## 使用发布包

从仓库的唯一预发布 Release 下载与系统对应的资产后运行：

```bash
./cuda-pie-calculator
```

Windows 可在 PowerShell 或 Windows Terminal 中运行 `cuda-pie-calculator.exe`。程序需要交互式终端；Windows 10/11 会启用 Virtual Terminal 模式来显示 TUI。

按键：

- `s`：开始 CUDA 计算
- `p`：暂停或继续
- `c`：取消当前 CUDA 任务
- `+` / `-`：以 100 位调整精度
- `j` / `k` 或上下方向键：滚动结果区域
- `q`：退出

## 本地构建

需要 CMake 3.24+、支持 C++20 的编译器和 CUDA Toolkit 12.x。构建时可按实际 GPU 调整 `CMAKE_CUDA_ARCHITECTURES`。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

本项目不要求在没有 GPU 的机器上构建或运行。没有 CUDA 设备时，运行程序只会给出 GPU 不可用诊断，绝不会以 CPU 继续计算。

## 自动发布

推送到 `main` 或 `master` 会触发 GitHub Actions：

1. 在 CUDA Toolkit 环境中分别构建 Linux x64 与 Windows x64 二进制。
2. 上传 `cuda-pie-calculator-linux-amd64.tar.gz` 与 `cuda-pie-calculator-windows-amd64.zip`。
3. 删除仓库中的已有 Release，创建一个以工作流运行号和提交 SHA 自动生成标签的预发布 Release。

因此仓库始终只保留一条 Release，不需要手工维护应用版本号。
