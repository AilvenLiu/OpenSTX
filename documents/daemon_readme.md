在MacOS上使用`launchd`将Python程序配置为守护进程并设置开机自启，可以通过创建和配置`.plist`文件来实现。

### 步骤1：准备Conda环境和Python程序

首先，确保您的Conda环境已经正确配置并能通过命令行激活。验证`get_realtime_data.py`脚本在目标路径下可以正常运行。

```bash
# 激活Conda环境
source /Users/mac/miniconda3/bin/activate quant

# 执行Python脚本
python /Users/mac/proj/quant_trading/data/get_realtime_data.py
```

### 步骤2：编写Shell脚本用于激活Conda环境

由于`launchd`不能直接识别Conda环境，我们需要编写一个Shell脚本来激活环境并运行Python脚本。

#### 创建Shell脚本

在项目目录中创建一个Shell脚本文件`run_realtime_data.sh`：

```bash
touch /Users/mac/proj/quant_trading/run_realtime_data.sh
```

#### 编辑Shell脚本

编辑`run_realtime_data.sh`，内容如下：

```bash
#!/bin/bash

# Activate the conda environment
source /Users/mac/miniconda3/bin/activate quant

# Navigate to the project directory
cd /Users/mac/proj/quant_trading

# Execute the Python script
python /Users/mac/proj/quant_trading/data/get_realtime_data.py
```

#### 赋予执行权限

```bash
chmod +x /Users/mac/proj/quant_trading/run_realtime_data.sh
```

### 步骤3：创建`launchd`配置文件

创建一个.plist文件来定义守护进程的配置。

#### 创建`plist`文件

在`/Library/LaunchDaemons`目录下创建一个新的.plist文件，例如`com.yourusername.realtime.plist`：

```bash
sudo touch /Library/LaunchDaemons/com.yourusername.realtime.plist
```

#### 编辑`plist`文件

编辑`com.yourusername.realtime.plist`，内容如下：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.yourusername.realtime</string>
    <key>ProgramArguments</key>
    <array>
        <string>/bin/bash</string>
        <string>/Users/mac/proj/quant_trading/run_realtime_data.sh</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>WorkingDirectory</key>
    <string>/Users/mac/proj/quant_trading</string>
    <key>StandardOutPath</key>
    <string>/Users/mac/proj/quant_trading/realtime_data.out</string>
    <key>StandardErrorPath</key>
    <string>/Users/mac/proj/quant_trading/realtime_data.err</string>
</dict>
</plist>
```

### 配置项说明

- **Label**：定义此守护进程的唯一标识符。
- **ProgramArguments**：指定要执行的命令，包括激活Conda环境的Shell脚本。
- **RunAtLoad**：设置为`true`，确保在系统启动时加载此守护进程。
- **KeepAlive**：设置为`true`，以确保程序在退出后自动重启。
- **WorkingDirectory**：设置为项目目录。
- **StandardOutPath**和**StandardErrorPath**：指定标准输出和错误输出文件的位置，用于日志记录。

### 步骤4：加载和启动`launchd`服务

```bash
# 加载并启动launchd服务
sudo launchctl load /Library/LaunchDaemons/com.yourusername.realtime.plist

# 验证服务状态
sudo launchctl list | grep yourusername.realtime
```

- `launchctl load`命令用于加载并启动配置的守护进程。
- 使用`launchctl list`查看守护进程的状态。

### 步骤5：验证和调试

- 确保在MacOS启动后，守护进程会自动启动。
- 检查日志文件`/Users/mac/proj/quant_trading/realtime_data.out`和`/Users/mac/proj/quant_trading/realtime_data.err`以查看输出和错误信息。
- 在进行调试时，可以通过`launchctl unload`命令卸载守护进程。

```bash
# 卸载守护进程（用于调试或更改配置）
sudo launchctl unload /Library/LaunchDaemons/com.yourusername.realtime.plist
```
