# CUDA 圆周率计算器

这是一个以 CUDA 为唯一数值后端的圆周率终端程序。界面由 Rust + ratatui 重写，计算内核仍使用原有 C++/CUDA 引擎，两者通过动态加载的 C ABI 连接。

程序以现代深色 TUI 展示设备列表、实时进度、结果区、CPU/内存/GPU 资源仪表和吞吐量曲线。启动时会枚举全部 CUDA 设备；没有 NVIDIA GPU、驱动不可用、GPU 未透传或 CUDA 初始化失败时，界面会明确显示“GPU 不可用”和具体错误。

## 支持范围

- Windows 10 / Windows 11 x64
- Ubuntu 22.04+ x64，Debian 12+ x64
- NVIDIA CUDA GPU 与对应驱动

不构建或发布 ARM 版本。Linux 发布包由 Ubuntu 22.04 x64 构建，面向 Ubuntu 22.04+ 与 Debian 12+；Windows 发布包为 x64 ZIP。

## 计算模式

程序提供两个 CUDA 计算模式，可用 `m` 在未运行时切换。

### 精确位数

精确模式使用 Machin 公式：

```text
pi / 4 = 4 * atan(1 / 5) - atan(1 / 239)
```

每个反正切项使用基数 `10^4` 的定点向量表示。初始化、级数项更新、进位、借位、Machin 合并和最终缩放均在 CUDA 内核中执行。

当前精度范围是小数点后 10 到 10,000 位，默认目标为 10,000 位。

### 蒙特卡洛验证

蒙特卡洛模式使用 CUDA 线程在单位正方形内生成均匀随机点，以落在四分之一圆内的比例估计 Pi。Pi 估计值和 95% 置信区间也由 CUDA 内核计算。

默认目标为 1 亿样本，支持 100 万到 40 亿样本。

## 使用发布包

从仓库的预发布 Release 下载与系统对应的资产后，**将可执行文件与引擎动态库放在同一目录**，再从交互式终端运行：

```bash
./cuda-pie-calculator
```

Windows 可在 PowerShell 或 Windows Terminal 中运行：

```powershell
cuda-pie-calculator.exe
```

发布包包含以下文件：

- Linux：`cuda-pie-calculator` + `libcuda_pie_engine.so`
- Windows：`cuda-pie-calculator.exe` + `cuda_pie_engine.dll`

密钥：

- `s`：开始 CUDA 计算
- `m`：在精确位数与蒙特卡洛模式间切换（任务未运行时）
- `p`：暂停或继续
- `c`：取消当前 CUDA 任务
- `+` / `-`：精确模式以 100 位调整精度；蒙特卡洛模式以 1,000 万样本调整目标
- `上/下方向键` 或 `j` / `k`：切换 CUDA 设备
- `[` / `]` 或 `PageUp` / `PageDown`：滚动结果区域
- `q`：退出

## 自动发布

推送到 `main` 或 `master` 会触发 GitHub Actions：

1. 在每个平台先通过 CMake 构建 CUDA 引擎动态库。
2. 再通过 Cargo 构建 Rust ratatui TUI。
3. 将二者打包为 `cuda-pie-calculator-linux-amd64.tar.gz` 与 `cuda-pie-calculator-windows-amd64.zip`。
4. 删除已有 Release，创建以工作流运行号和提交 SHA 自动生成标签的预发布 Release。

仓库只保留一条 Release，不需手工维护版本号。项目不要求在没有 GPU 的机器上构建或运行；没有 CUDA 设备时，运行程序只会给出 GPU 不可用诊断，绝不会以 CPU 继续计算。
