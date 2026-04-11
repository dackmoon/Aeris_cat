# 1.2 Git 版本控制基础

> 最后更新：2026-04-11  
> 适用版本：Cyber Cat Phase 4/5

---

## 本章目标

完成本章后，你将：
- [ ] 理解 Git 版本控制的基本概念
- [ ] 能够安装和配置 Git
- [ ] 掌握常用的 Git 命令
- [ ] 理解分支管理策略
- [ ] 能够解决简单的代码冲突

---

## 1.2.1 Git 安装与配置

### 安装 Git

**Windows:**
1. 访问 https://git-scm.com/download/win
2. 下载并运行安装程序
3. 使用默认选项安装即可

**macOS:**
```bash
# 使用 Homebrew 安装
brew install git

# 或使用 Xcode 命令行工具
xcode-select --install
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt update
sudo apt install git
```

### 验证安装

```bash
git --version
# 输出示例：git version 2.42.0
```

### 配置用户信息

安装完成后，首先配置你的用户名和邮箱：

```bash
# 配置全局用户名
git config --global user.name "Your Name"

# 配置全局邮箱
git config --global user.email "your.email@example.com"

# 验证配置
git config --list
```

> **为什么需要配置邮箱？** Git 使用邮箱来标识提交者，GitHub/GitLab 等平台也通过邮箱关联你的提交记录。

---

## 1.2.2 仓库与分支管理

### 什么是 Git 仓库？

Git 仓库是存储项目代码和版本历史的地方，分为两种：

| 类型 | 说明 |
|------|------|
| **工作区 (Working Directory)** | 你实际编辑代码的目录 |
| **暂存区 (Staging Area)** | 准备提交的文件缓存区 |
| **本地仓库 (Local Repository)** | 存储在 `.git` 目录中的完整版本历史 |
| **远程仓库 (Remote Repository)** | 托管在 GitHub/GitLab 等平台的仓库 |

### 克隆远程仓库

```bash
# 克隆仓库到本地
git clone <仓库 URL>

# 示例
git clone https://github.com/username/cyber_cat.git
```

### 查看仓库状态

```bash
# 查看工作区和暂存区状态
git status

# 查看提交历史
git log

# 查看简洁的提交历史
git log --oneline --graph
```

### 分支基础

**分支是什么？** 分支是 Git 的一条开发线，允许你独立开发新功能而不影响主代码。

```bash
# 查看所有分支
git branch

# 创建新分支
git branch feature-new-expression

# 切换分支
git checkout feature-new-expression

# 创建并切换到新分支（推荐）
git checkout -b feature-new-expression

# 查看当前分支
git branch --show-current
```

### 分支命名规范

本项目推荐使用以下分支命名规范：

| 前缀 | 用途 | 示例 |
|------|------|------|
| `feature/` | 新功能 | `feature/angry-expression` |
| `bugfix/` | 修复 bug | `bugfix/wifi-reconnect` |
| `docs/` | 文档更新 | `docs/readme-update` |
| `refactor/` | 重构代码 | `refactor/audio-pipeline` |

---

## 1.2.3 常用 Git 命令

### 添加文件

```bash
# 添加单个文件到暂存区
git add 文件名

# 添加所有修改的文件
git add .

# 交互式添加（推荐）
git add -p
```

### 提交更改

```bash
# 提交暂存区的文件
git commit -m "提交信息"

# 提交所有修改的文件（跳过 add）
git commit -am "提交信息"
```

### 推送和拉取

```bash
# 推送到远程仓库
git push origin 分支名

# 首次推送时设置上游分支
git push -u origin 分支名

# 从远程拉取最新代码
git pull origin 分支名
```

### 撤销操作

```bash
# 撤销暂存区的文件（保留修改）
git reset HEAD 文件名

# 撤销工作区的修改（危险！会丢失未提交的修改）
git checkout -- 文件名

# 撤销最近一次提交（保留修改）
git reset --soft HEAD~1

# 撤销最近一次提交（丢弃修改）
git reset --hard HEAD~1
```

### 常用命令速查表

| 命令 | 说明 |
|------|------|
| `git status` | 查看仓库状态 |
| `git add <文件>` | 添加文件到暂存区 |
| `git commit -m "消息"` | 提交更改 |
| `git push` | 推送到远程 |
| `git pull` | 从远程拉取 |
| `git branch` | 查看/创建分支 |
| `git checkout <分支>` | 切换分支 |
| `git log` | 查看提交历史 |
| `git diff` | 查看差异 |

---

## 1.2.4 解决代码冲突

### 什么是代码冲突？

当两个开发者修改了同一个文件的同一部分代码，Git 无法自动合并时，就会产生冲突。

### 冲突示例

```
<<<<<<< HEAD
void setExpression(CAT_HAPPY);
=======
void setExpression(CAT_ANGRY);
>>>>>>> feature/angry-expression
```

### 解决冲突的步骤

**步骤 1：识别冲突**

```bash
git status
# 显示：both modified: 文件名
```

**步骤 2：打开冲突文件**

找到 `<<<<<<<`、`=======`、`>>>>>>>` 标记的区域。

**步骤 3：决定保留哪些代码**

- 保留 `HEAD`（当前分支）的代码
- 保留对方分支的代码
- 或者合并两者的修改

**步骤 4：标记冲突已解决**

```bash
# 编辑文件后，标记为已解决
git add 文件名

# 完成合并提交
git commit -m "解决合并冲突"
```

### 避免冲突的最佳实践

1. **经常拉取远程代码**：`git pull` 保持本地最新
2. **小步提交**：每次提交改动小，减少冲突概率
3. **及时同步**：开始工作前先 `git pull`
4. **沟通协作**：团队成员间沟通各自修改的文件

---

## 本章小结

本章学习了 Git 版本控制的基础：
- ✅ Git 安装与配置
- ✅ 仓库和分支管理
- ✅ 常用 Git 命令
- ✅ 代码冲突解决

## 下一步

继续学习 **[1.3 Linux 命令行基础](03-linux-命令行.md)**，掌握服务器操作技能。

---

## 实践练习

1. 克隆项目仓库到本地
2. 创建一个名为 `feature/test-branch` 的新分支
3. 修改 `README.md` 文件，添加一行内容
4. 提交更改并推送到远程
5. 切换回主分支，验证修改不在主分支

## 常见问题

**Q: `git push` 提示需要设置上游分支？**  
A: 使用 `git push -u origin 分支名` 设置上游分支。

**Q: 不小心提交了错误的文件怎么办？**  
A: 使用 `git reset --soft HEAD~1` 撤销提交，然后重新提交正确的文件。

**Q: 如何查看某个文件的修改历史？**  
A: 使用 `git log -- 文件名` 查看该文件的所有提交记录。
