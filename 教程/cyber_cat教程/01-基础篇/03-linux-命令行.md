# 1.3 Linux 命令行基础

> 最后更新：2026-04-11  
> 适用版本：Cyber Cat Phase 4/5

---

## 本章目标

完成本章后，你将：
- [ ] 掌握 Linux 文件和目录操作命令
- [ ] 能够使用文本编辑器查看和修改文件
- [ ] 理解进程和服务管理
- [ ] 掌握基础的 SSH 远程连接技能

---

## 1.3.1 文件与目录操作

###  pwd - 显示当前目录

```bash
pwd
# 输出示例：/home/username
```

### ls - 列出目录内容

```bash
# 列出当前目录内容
ls

# 详细信息列表
ls -l

# 显示隐藏文件
ls -a

# 人类可读的文件大小
ls -lh
```

**输出示例：**
```
drwxr-xr-x  5 user user 4096 Apr 11 10:00 cyber_cat/
-rw-r--r--  1 user user  253 Apr 11 09:00 README.md
```

### cd - 切换目录

```bash
# 进入目录
cd cyber_cat

# 返回上级目录
cd ..

# 返回 home 目录
cd ~

# 返回上一个目录
cd -
```

### mkdir - 创建目录

```bash
# 创建单个目录
mkdir new_folder

# 递归创建多级目录
mkdir -p parent/child/grandchild
```

### cp - 复制文件

```bash
# 复制文件
cp source.txt destination.txt

# 递归复制目录
cp -r source_dir/ dest_dir/

# 带确认复制（覆盖前询问）
cp -i source.txt destination.txt
```

### mv - 移动/重命名文件

```bash
# 移动文件
cp file.txt /path/to/destination/

# 重命名文件
mv old_name.txt new_name.txt

# 移动并重命名
mv source.txt /dest/new_name.txt
```

### rm - 删除文件

```bash
# 删除文件
rm file.txt

# 强制删除（不询问）
rm -f file.txt

# 递归删除目录（危险！）
rm -rf directory/
```

> **警告**：`rm -rf` 会永久删除文件，无法恢复。使用前务必确认路径！

### touch - 创建空文件

```bash
# 创建空文件
touch new_file.txt

# 更新文件时间戳
touch existing_file.txt
```

### 路径说明

| 路径类型 | 示例 | 说明 |
|---------|------|------|
| 绝对路径 | `/home/user/file.txt` | 从根目录开始的完整路径 |
| 相对路径 | `./file.txt` | 相对于当前目录的路径 |
| home 目录 | `~/file.txt` | 等价于 `/home/username/file.txt` |
| 上级目录 | `../file.txt` | 上一级目录中的文件 |

---

## 1.3.2 文件编辑与查看

### cat - 查看文件内容

```bash
# 查看整个文件
cat filename.txt

# 显示行号
cat -n filename.txt

# 查看多个文件
cat file1.txt file2.txt
```

### less - 分页查看文件

```bash
# 分页查看大文件
less large_file.log

# 操作键：
#   空格/PgDn - 向下翻页
#   b/PgUp    - 向上翻页
#   /keyword  - 搜索
#   q         - 退出
```

### head/tail - 查看文件开头/结尾

```bash
# 查看前 10 行
head filename.txt

# 查看前 20 行
head -n 20 filename.txt

# 查看后 10 行
tail filename.txt

# 实时查看日志（追踪模式）
tail -f /var/log/syslog
```

### nano - 简易文本编辑器

```bash
# 用 nano 打开文件
nano filename.txt

# 常用快捷键：
#   Ctrl+O - 保存
#   Ctrl+X - 退出
#   Ctrl+K - 剪切行
#   Ctrl+U - 粘贴
```

### vim - 强大文本编辑器（推荐学习）

```bash
# 用 vim 打开文件
vim filename.txt

# 模式说明：
#   普通模式（默认）- 移动光标、删除、复制等
#   插入模式（按 i） - 编辑文本
#   命令模式（按 :） - 保存、退出等命令

# 常用命令：
#   i        - 进入插入模式
#   Esc      - 退出插入模式
#   :w       - 保存
#   :q       - 退出
#   :wq      - 保存并退出
#   :q!      - 强制退出不保存
#   dd       - 删除当前行
#   yy       - 复制当前行
#   p        - 粘贴
#   /keyword - 搜索
```

> **提示**：新手建议先用 nano，熟练后学习 vim 可以大幅提高编辑效率。

---

## 1.3.3 进程与服务管理

### ps - 查看进程

```bash
# 查看当前用户的进程
ps

# 查看所有进程
ps aux

# 查找特定进程
ps aux | grep python
```

### top/htop - 实时进程监控

```bash
# 启动 top
top

# 退出 top: q

# 更友好的 htop（需要安装）
htop
```

**top 输出说明：**
```
top - 10:00:00 up 1 day,  2 users,  load average: 0.5, 0.8, 0.9
Tasks: 123 total,   1 running, 122 sleeping
%Cpu(s):  5.0 us,  2.0 sy,  0.0 ni, 93.0 id
KiB Mem :  8000000 total,  2000000 free
```

### kill - 终止进程

```bash
# 终止进程（需要知道 PID）
kill 1234

# 强制终止
kill -9 1234

# 按进程名终止
pkill python
```

### systemctl - 管理系统服务

```bash
# 查看服务状态
systemctl status cybercat-backend

# 启动服务
systemctl start cybercat-backend

# 停止服务
systemctl stop cybercat-backend

# 重启服务
systemctl restart cybercat-backend

# 开机自启
systemctl enable cybercat-backend

# 取消开机自启
systemctl disable cybercat-backend

# 查看所有服务
systemctl list-units --type=service
```

### journalctl - 查看系统日志

```bash
# 查看所有日志
journalctl

# 查看特定服务日志
journalctl -u cybercat-backend

# 实时追踪日志
journalctl -f

# 查看最近 100 行
journalctl -n 100
```

---

## 1.3.4 网络与 SSH

### ifconfig/ip - 查看网络接口

```bash
# 查看网络接口（旧命令）
ifconfig

# 查看网络接口（新命令）
ip addr show

# 简化输出
ip -br addr show
```

### ping - 测试网络连通性

```bash
# ping 一个主机
ping google.com

# 发送 5 个包后停止
ping -c 5 google.com
```

### curl - 发送 HTTP 请求

```bash
# GET 请求
curl https://api.example.com/data

# POST 请求
curl -X POST https://api.example.com/api \
  -H "Content-Type: application/json" \
  -d '{"key": "value"}'

# 下载文件
curl -O https://example.com/file.zip

# 显示响应头
curl -I https://example.com
```

### ssh - 远程登录

```bash
# 连接到远程服务器
ssh username@122.51.129.89

# 指定端口
ssh -p 2222 username@122.51.129.89

# 使用密钥文件
ssh -i ~/.ssh/id_rsa username@122.51.129.89
```

### scp - 安全复制文件

```bash
# 上传文件到服务器
scp file.txt username@122.51.129.89:/remote/path/

# 从服务器下载文件
scp username@122.51.129.89:/remote/path/file.txt ./

# 递归复制目录
scp -r local_dir/ username@122.51.129.89:/remote/path/
```

### SSH 密钥配置（免密码登录）

```bash
# 1. 生成 SSH 密钥
ssh-keygen -t ed25519 -C "your.email@example.com"

# 2. 复制公钥到服务器
ssh-copy-id username@122.51.129.89

# 3. 测试免密码登录
ssh username@122.51.129.89
```

### netstat/ss - 查看网络连接

```bash
# 查看所有监听端口
netstat -tlnp

# 或使用新命令
ss -tlnp
```

**输出示例：**
```
State    Recv-Q   Send-Q     Local Address:Port     Foreign Address:Port   Process
LISTEN   0        128              0.0.0.0:8000          0.0.0.0:*         users:(("uvicorn",pid=1234,fd=3))
```

---

## 本章小结

本章学习了 Linux 命令行基础：
- ✅ 文件与目录操作（ls, cd, cp, mv, rm）
- ✅ 文件编辑与查看（cat, less, nano, vim）
- ✅ 进程与服务管理（ps, top, systemctl, journalctl）
- ✅ 网络与 SSH（ssh, scp, curl）

## 下一步

继续学习 **[1.4 C++ 语言基础](04-cpp-基础.md)**，为 ESP32 固件开发打下基础。

---

## 实践练习

1. 在 home 目录创建 `test` 目录，进入后创建 `hello.txt` 文件
2. 用 `nano` 或 `vim` 编辑 `hello.txt`，写入 "Hello, Cyber Cat!"
3. 使用 `curl` 访问 `https://api.github.com` 查看返回
4. 使用 `ps aux | grep python` 查找 Python 进程
5. （有服务器）配置 SSH 密钥免密码登录

## 常见问题

**Q: vim 中如何退出？**  
A: 按 `Esc` 退出插入模式，输入 `:q` 退出，`:wq` 保存退出，`:q!` 强制退出不保存。

**Q: sudo 是什么？**  
A: `sudo` 以管理员权限运行命令。例如 `sudo apt install` 需要管理员权限。

**Q: 如何查看当前目录的磁盘占用？**  
A: 使用 `du -sh .` 查看当前目录总大小，`du -sh *` 查看各子目录大小。
