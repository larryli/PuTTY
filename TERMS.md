# TERMS.md — PuTTY 中文版翻译术语表

## 总则

1. **专有名词不翻译**：协议、算法、产品名、文件名格式等保持英文原样
   - **含下划线的标识符（函数名/符号名/阶段标签）一律保留原样**，如 `ssh_init`、`createkey`、`rsa_verify`、`winadj`；只译其后的描述部分（`rsa_verify 失败` ✓，`SSH 初始化` ✗）
   - 协议常量与消息名（`SSH_MSG_*`、`FXP_*`、`PLUGIN_*`、`SSH2_MSG_*`、`REALPATH`）及命令行选项（`--help`、`-batch`、`-unsafe`）保留英文
2. 快捷键助记符沿用英文原文的 `(&X)` 形式，勿随意更改加速键字母
3. 同一概念全项目统一译法（见下表）
4. 界面文案使用中文全角标点（，。：；？）；命令行 usage 文本保持原文风格
5. 格式符与占位符原样保留：`%s`、`%d`、`&T`（时间）、`&H`（主机名）、`&P`（端口）
6. 数字、单位、版本号不翻译

## 不翻译的词（保持英文）

| 类别 | 词汇 |
|---|---|
| 协议 | SSH、SSH-1、SSH-2、Telnet、Rlogin、SUPDUP、SCP、SFTP、HTTP、SOCKS |
| 算法 | RSA、DSA、ECDSA、EdDSA、Ed25519、AES、AES-GCM、ChaCha20、Arcfour、SHA-256、MD5 |
| 密钥格式 | PPK、OpenSSH、PGP |
| 产品/组件 | PuTTY、PuTTYgen、Pageant、PSCP、PSFTP、Plink、Pterm、WinSCP |
| 其他 | X11、GSSAPI、IPv4、IPv6、CA、API、URL、UTF-8、xterm、ssh.com |
| 协议/数据包/参数 | TIS、CryptoCard、Kerberos、SSPI、SECUR32.DLL、DEK-Info、Proc-Type、bcrypt、kdf、KEXINIT、FXP_*、SSH_FXP_*、SSH_MSG_*、PLUGIN_*、REALPATH |
| 攻击/缺陷名 | Terrapin、winadj、IGNORE bug |

## 通用界面词汇

| English | 中文 | 说明 |
|---|---|---|
| OK | 确定 | 对话框默认按钮 |
| Cancel | 取消 | |
| Apply | 应用 | |
| Close | 关闭 | |
| Browse | 浏览 | |
| Help | 帮助 | |
| About | 关于 | |
| Accept | 接受 | 主机密钥确认对话框 |
| Connect Once | 只连接一次 | 主机密钥确认对话框 |
| More info | 更多信息 | |
| View Licence | 查看许可证 | |
| Visit Web Site | 访问网站 | |
| Default Settings | 默认设置 | |
| Yes / No | 是 / 否 | |
| On / Off | 开 / 关 | |
| Auto | 自动 | |
| Always / Never | 总是 / 从不 | |
| None | 无 | |
| Normal | 常规 | |
| Delete | 删除 | 按钮助记符为 `(&D)` |
| Remove | 删除 | 按钮助记符为 `(&R)`（与 Delete 同词不同键） |
| Load | 载入 | 不用“加载” |
| Save | 保存 | |
| Add | 增加 | |
| Copy | 复制 | |
| Paste | 粘贴 | |
| Copy All to Clipboard | 复制所有内容到剪贴板 | |
| Clear Scrollback | 清除回滚 |
| Reset Terminal | 重置终端 | |
| Full Screen | 全屏 | |
| Event Log | 事件日志记录 | |
| New Session | 新会话 | |
| Duplicate Session | 复制会话 | |
| Saved Sessions | 保存的会话 | |
| Change Settings | 修改设置 | |
| Restart Session | 重启会话 | |
| Session | 会话 | |

## 设置面板类别树（`config.c`）

| English | 中文 |
|---|---|
| Session / Logging | 会话 / 日志记录 |
| Terminal | 终端 |
| Terminal / Keyboard | 终端 / 键盘 |
| Terminal / Bell | 终端 / 响铃 |
| Terminal / Features | 终端 / 特性 |
| Window | 窗口 |
| Window / Appearance | 窗口 / 外观 |
| Window / Behaviour | 窗口 / 行为 |
| Window / Translation | 窗口 / 转换 |
| Window / Selection | 窗口 / 选择 |
| Window / Selection / Copy | 窗口 / 选择 / 复制 |
| Window / Colours | 窗口 / 颜色 |
| Connection | 连接 |
| Connection / Data | 连接 / 数据 |
| Connection / Proxy | 连接 / 代理 |
| Connection / SSH | 连接 / SSH |
| Connection / SSH / Kex | 连接 / SSH / 密钥交换 |
| Connection / SSH / Host keys | 连接 / SSH / 主机密钥 |
| Connection / SSH / Cipher | 连接 / SSH / 加密 |
| Connection / SSH / Auth | 连接 / SSH / 认证 |
| Connection / SSH / Auth / Certificate | 连接 / SSH / 认证 / 证书 |
| Connection / SSH / Auth / GSSAPI | 连接 / SSH / 认证 / GSSAPI |
| Connection / SSH / TTY | 连接 / SSH / TTY |
| Connection / SSH / X11 | 连接 / SSH / X11 |
| Connection / SSH / Tunnels | 连接 / SSH / 隧道 |
| Connection / Serial | 连接 / 串口 |
| Connection / Telnet | 连接 / Telnet |
| Connection / Rlogin | 连接 / Rlogin |
| Connection / SUPDUP | 连接 / SUPDUP |

## 网络与连接

| English | 中文 |
|---|---|
| Host Name (or IP address) | 主机名称 (或 IP 地址) |
| Port | 端口 |
| Username | 用户名 |
| Auto-login username | 自动登录用户名 |
| Local username | 本地用户名 |
| Proxy type | 代理类型 |
| Proxy address | 代理地址 |
| Connection type | 连接类型 |
| Serial line | 串行线路（类别名用“串口”） |
| Speed (baud) | 速度 (波特) |
| Flow control | 流控制 |
| Source port / destination | 源端口 / 目的地 |
| Local / remote port | 本地端口 / 远程端口 |
| Tunnels | 隧道 |
| Port forwarding | 端口转发 |

## SSH 与安全

| English | 中文 |
|---|---|
| Key | 密钥 |
| Private key | 私钥 |
| Public key | 公钥 |
| Key comment | 密钥注释 |
| Key passphrase | 密钥密码（确认框作“确认密码”） |
| Fingerprint | 指纹 |
| Host key | 主机密钥 |
| Channel | 通道 |
| Key exchange (Kex) | 密钥交换 |
| Cipher | 加密（类别名）；正文中“加密方法选择顺序” |
| Authentication | 认证 |
| Agent（Pageant） | 认证代理 / 代理 |
| Agent forwarding | 代理转发 |
| Compression | 压缩 |
| Certificate | 证书 |
| Certificate Authority (CA) | 证书颁发机构；“主机 CA”保留 CA |
| Encryption | 加密 |
| Group exchange | 组交换 |
| Modulus | 模数 |
| Rekey / key re-exchange | 密钥重交换 |
| Strict kex | 严格 kex |
| Auth helper plugin | 认证辅助插件 |
| Keyboard-interactive | 键盘交互 |
| Fallback host key | 回退主机密钥 |
| Transient host key cache | 临时主机密钥缓存 |
| Cross-certify | 交叉认证 |
| Credentials | 凭据 |
| Security context | 安全上下文 |
| MIC (message integrity code) | MIC |
| Banner message | 横幅消息 |
| Camouflage packets | 伪装数据包 |

## 密钥生成（PuTTYgen）

| English | 中文 |
|---|---|
| Generate key pair | 生成密钥对 |
| Load private key | 载入私钥 |
| Save private key | 保存私钥 |
| Import key | 导入密钥 |
| Export OpenSSH key | 导出 OpenSSH 密钥 |
| Key type | 生成密钥类型 |
| Number of bits in a generated key | 生成密钥的位数 |
| Curve (for ECDSA/EdDSA) | 用于生成密钥的曲线 |

## 终端与窗口

| English | 中文 |
|---|---|
| Terminal | 终端 |
| Font | 字体 |
| Colours | 颜色 |
| Cursor | 光标 |
| Clipboard | 剪贴板 |
| Wrap (auto wrap) | 换行（自动换行） |
| Scrollback | 回滚 |
| Scrollbar | 滚动条 |
| Bell | 响铃 |
| Window border | 窗口边框 |
| Columns / Rows | 列 / 行 |

## 命令行（`cmdgen.c` usage）

| English | 中文 |
|---|---|
| sessname | 会话名 |
| port | 端口 |
| user / username | 用户名 |
| key file | 密钥（文件） |
| enable Pageant | 启用 Pageant 认证代理 |
| interactive prompts | 交互提示 |
| compression | 压缩 |
| verbose messages | 详细信息 |
| version information | 版本信息 |
| log file | 日志文件 |

## 命令行与传输消息（CLI/协议层）

| English | 中文 |
|---|---|
| wildcard | 通配符 |
| multi-level wildcards | 多级通配符 |
| home directory | 主目录 |
| working directory | 工作目录 |
| local / remote | 本地 / 远程 |
| download / upload | 下载 / 上传 |
| resume (reget/reput) | 续传 |
| ETA | 剩余 |
| unsorted | 未排序 |
| compound pathname | 复合路径名 |
| principal | 主体 |
| critical options | 关键选项 |
| set-user-ID / set-group-ID bit | set-user-ID / set-group-ID 位 |
| sticky bit | sticky bit |
| octal permission | 八进制权限 |
| batch file | 批处理文件 |
| end of file / EOF | 文件结束 |
| short buffer | 缓冲区过短 |
| truncated | 被截断 |
| ill-formed / badly formatted | 格式错误 |
| camouflage packets | 伪装数据包 |
| length-padded | 长度填充后的 |
| unsupported / not supported | 不支持 |
| permission denied | 权限被拒绝 |
| no such file or directory | 没有这样的文件或目录 |
| FATAL ERROR | 致命错误 |
| ERROR（前缀） | 错误 |
| file format error | 文件格式错误 |
| MAC failed | MAC 校验失败 |
| wrong passphrase | 密钥密码错误 |
| ssh_init（阶段标签） | SSH 初始化 |
