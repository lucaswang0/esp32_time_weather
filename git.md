## Git 分支远程操作指南

------

## 一、推送本地分支到远端

### 1. 首次推送（设置上游分支）

bash

```
# 推送并建立追踪关系
git push -u origin 分支名

# 示例
git push -u origin feature/ota-update
```



### 2. 后续推送（已设置上游）

bash

```
# 直接推送
git push

# 或指定远程仓库
git push origin 分支名
```



### 3. 推送所有本地分支

bash

```
# 推送所有分支
git push --all origin

# 推送当前分支及关联
git push origin --all
```



------

## 二、拉取远端分支到本地

### 方法一：拉取并自动创建本地分支（推荐）

bash

```
# 拉取远程分支并创建本地分支
git checkout -b 本地分支名 origin/远程分支名

# 示例（远程有 feature/ota，拉取到本地 ota）
git checkout -b ota origin/feature/ota
```



### 方法二：先拉取再切换（两步法）

bash

```
# 1. 获取远程所有分支信息
git fetch origin

# 2. 切换到远程分支（会自动创建本地分支）
git checkout -b 本地分支名 origin/远程分支名

# 或简化（Git 2.23+）
git switch -c 本地分支名 origin/远程分支名
```



### 方法三：自动创建同名分支

bash

```
# 如果本地没有同名分支
git checkout 远程分支名
# Git 会自动创建同名的本地分支并切换到它
```



### 方法四：只拉取不切换

bash

```
# 获取远程分支信息
git fetch origin

# 查看远程分支列表
git branch -r

# 拉取指定远程分支
git fetch origin 远程分支名
```



------

## 三、删除分支

### 删除本地分支

bash

```
# 删除已合并的分支（安全）
git branch -d 分支名

# 强制删除未合并的分支
git branch -D 分支名

# 示例
git branch -d feature/ota-update
```



### 删除远程分支

bash

```
# 方法一
git push origin --delete 分支名

# 方法二
git push origin :分支名

# 示例
git push origin --delete feature/ota-update
```



### 删除远程分支的本地追踪引用

bash

```
# 清理已删除远程分支的本地追踪
git remote prune origin

# 查看哪些远程分支已被删除
git remote prune origin --dry-run
```



------

## 四、完整操作示例

### 场景一：开发新功能并推送到远程

bash

```
# 1. 创建本地分支
git checkout -b feature/new-function

# 2. 开发...
git add .
git commit -m "添加新功能"

# 3. 推送到远程（首次）
git push -u origin feature/new-function

# 4. 后续推送
git push
```



### 场景二：拉取同事创建的远程分支

bash

```
# 1. 查看远程分支
git branch -r
# origin/main
# origin/feature/new-function

# 2. 拉取到本地
git checkout -b new-function origin/feature/new-function

# 3. 或直接切换（自动创建）
git checkout feature/new-function
```



### 场景三：功能完成，删除分支

bash

```
# 1. 合并到主分支
git checkout main
git merge feature/new-function

# 2. 推送到远程
git push

# 3. 删除远程分支
git push origin --delete feature/new-function

# 4. 删除本地分支
git branch -d feature/new-function
```



------

## 五、常用命令速查

| 操作                     | 命令                                   |
| :----------------------- | :------------------------------------- |
| 查看本地分支             | `git branch`                           |
| 查看远程分支             | `git branch -r`                        |
| 查看所有分支             | `git branch -a`                        |
| 推送分支到远程           | `git push -u origin 分支名`            |
| 拉取远程分支到本地       | `git checkout -b 本地名 origin/远程名` |
| 删除本地分支             | `git branch -d 分支名`                 |
| 删除远程分支             | `git push origin --delete 分支名`      |
| 更新远程分支列表         | `git fetch origin`                     |
| 清理已删除的远程分支引用 | `git remote prune origin`              |
| 查看分支追踪关系         | `git branch -vv`                       |

------

## 六、注意事项

### ⚠️ 推送前

bash

```
# 先拉取最新代码，避免冲突
git pull origin main
```



### ⚠️ 删除前

bash

```
# 确认分支已合并
git branch --merged

# 查看哪些分支未合并
git branch --no-merged
```



### ⚠️ 强制推送（谨慎使用）

bash

```
# 覆盖远程分支（危险！）
git push -f origin 分支名
# 或
git push --force origin 分支名
```



------

## 七、常见问题解决

### 问题1：推送被拒绝（远程有新提交）

bash

```
# 先拉取再推送
git pull origin 分支名
git push origin 分支名
```



### 问题2：本地分支名与远程不同

bash

```
# 推送并设置上游
git push -u origin 本地分支名:远程分支名

# 示例
git push -u origin feature/ota:ota-update
```



### 问题3：删除远程分支后，本地仍能看到

bash

```
# 清理缓存
git remote prune origin

# 或
git fetch --prune origin
```



### 问题4：拉取远程分支时冲突

bash

```
# 使用 rebase 方式拉取
git pull --rebase origin 分支名

# 或先 fetch 再手动解决
git fetch origin
git merge origin/分支名
```



------

## 八、推荐工作流

bash

```
# 1. 开发前同步
git checkout main
git pull origin main

# 2. 创建功能分支
git checkout -b feature/xxx

# 3. 开发并推送
git add .
git commit -m "描述"
git push -u origin feature/xxx

# 4. 合并到主分支
git checkout main
git merge feature/xxx

# 5. 推送并清理
git push
git branch -d feature/xxx
git push origin --delete feature/xxx
```



------

## 九、配置别名（可选）

bash

```
# 简化命令
git config --global alias.co checkout
git config --global alias.br branch
git config --global alias.st status
git config --global alias.ps push
git config --global alias.pl pull
git config --global alias.ft fetch
git config --global alias.prune "remote prune origin"

# 使用别名
git co -b feature/new
git ps -u origin feature/new
git br -d feature/new
```
