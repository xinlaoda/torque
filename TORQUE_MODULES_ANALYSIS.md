# TORQUE项目模块分析

## 项目概述

TORQUE (Terascale Open-source Resource and Queue Manager) 是一个基于原始PBS (Portable Batch System) 的开源资源管理和作业队列系统。通过分析configure.ac和相关Makefile.am文件，可以确定该项目将构造以下主要模块：

## 主要模块构成

### 1. 服务器组件 (Server Components)
**条件编译**: `INCLUDE_SERVER`
- **server/**: 主服务器守护进程
- **scheduler.***: 调度器实现
  - `scheduler.basl/`: BASL调度器
  - `scheduler.cc/`: C++调度器（包含多个样例实现）
    - `samples/cray_t3e/`: Cray T3E集群调度器
    - `samples/dec_cluster/`: DEC集群调度器
    - `samples/fifo/`: FIFO调度器
    - `samples/msic_cluster/`: MSIC集群调度器
    - `samples/sgi_origin/`: SGI Origin调度器
    - `samples/umn_cluster/`: UMN集群调度器
  - `scheduler.tcl/`: TCL调度器

### 2. MOM组件 (Machine Oriented Mini-server)
**条件编译**: `INCLUDE_MOM`
- **resmom/**: 资源监控守护进程（主要MOM组件）
  - `linux/`: Linux平台特定实现
  - `cygwin/`: Cygwin平台实现
  - `darwin/`: macOS平台实现
  - `solaris7/`: Solaris 7平台实现
- **momctl/**: MOM控制工具
- **mom_rcp/**: MOM远程复制功能（可选组件，`INCLUDE_MOM_RCP`）

### 3. 客户端组件 (Client Components)
**条件编译**: `INCLUDE_CLIENTS`
- **daemon_client/**: 守护进程客户端
- **cmds/**: 命令行工具集
  - `qsub`: 作业提交
  - `qstat`: 作业状态查询
  - `qdel`: 作业删除
  - `qalter`: 作业修改
  - `qhold`: 作业挂起
  - `qrls`: 作业释放
  - `qrun`: 作业运行
  - `qmgr`: 队列管理
  - `pbsnodes`: 节点管理
  - `pbsdsh`: 分布式shell
  - 其他PBS命令工具
- **tools/**: 工具集
  - `xpbsmon/`: X Window监控工具
- **momctl/**: MOM控制（也用作客户端工具）

### 4. 图形用户界面组件 (GUI Components)
**条件编译**: `INCLUDE_GUI`
- **gui/**: 图形用户界面
  - `Ccode/`: C代码实现
- **tools/**: 相关GUI工具

### 5. 核心库组件 (Core Libraries)
位于 `lib/` 目录下：
- **Libattr/**: 属性处理库
- **Libcmds/**: 命令工具库
- **Libcsv/**: CSV文件处理库
- **Libdis/**: 数据交换库
- **Libifl/**: 接口库
- **Liblog/**: 日志处理库
- **Libnet/**: 网络通信库
- **Libpbs/**: PBS核心库
- **Libsite/**: 站点特定功能库
- **Libutils/**: 通用工具库

### 6. 认证和安全组件
**条件编译**: `INCLUDE_PAM`
- **pam/**: PAM (Pluggable Authentication Modules) 模块

### 7. API组件
**条件编译**: `INCLUDE_DRMAA`
- **drmaa/**: DRMAA (Distributed Resource Management Application API) 实现
  - `src/`: DRMAA源代码

### 8. 测试组件
**条件编译**: `HAVE_CHECK`
- **test/**: 单元测试框架
  - 包含各种模块的测试用例
  - `torque_test_lib/`: 测试库

### 9. 文档组件
- **doc/**: 文档和手册页
  - `man1/`: 用户命令手册页
  - `man3/`: 库函数手册页
  - `man7/`: 杂项手册页
  - `man8/`: 系统管理命令手册页

### 10. 构建和安装支持
- **buildutils/**: 构建工具和脚本
- **contrib/**: 贡献组件
  - `blcr/`: Berkeley Lab Checkpoint/Restart支持
  - `init.d/`: 系统初始化脚本
  - `systemd/`: systemd服务配置

### 11. 头文件和接口
- **include/**: 公共头文件和接口定义

## 安装目标

项目提供了多种安装目标：
- `install_mom`: 安装MOM组件
- `install_server`: 安装服务器组件
- `install_clients`: 安装客户端组件
- `install_gui`: 安装GUI组件
- `install_devel`: 安装开发文件
- `install_lib`: 安装库文件
- `install_pam`: 安装PAM模块
- `install_drmaa`: 安装DRMAA API

## RPM包结构

根据torque.spec.in文件，TORQUE项目构建为以下RPM包：

### 主要包
1. **torque**: 基础包，包含核心库和基本功能
2. **torque-server**: 服务器组件
   - 包含pbs_server守护进程
   - 作业队列管理
   - 资源分配
3. **torque-client**: 客户端和MOM组件
   - 包含pbs_mom守护进程
   - 客户端命令工具（qsub, qstat, qdel等）
   - 节点资源监控
4. **torque-scheduler**: 调度器组件
   - FIFO调度器实现
   - 调度策略管理

### 可选包
5. **torque-devel**: 开发文件
   - 头文件和开发库
   - 用于开发TORQUE扩展
6. **torque-gui**: 图形用户界面（可选）
   - xpbs图形界面
   - xpbsmon监控工具
7. **torque-drmaa**: DRMAA API实现（可选）
   - 分布式资源管理应用程序API

### 可选功能特性
通过条件编译启用：
- **BLCR支持**: Berkeley Lab Checkpoint/Restart
- **CPUset支持**: CPU集合隔离
- **Cgroups支持**: 控制组资源管理
- **NUMA支持**: 非统一内存访问优化
- **PAM集成**: 可插拔认证模块
- **Munge认证**: 消息认证
- **调试模式**: 开发调试支持

## 总结

TORQUE项目通过模块化设计，构造出一个完整的分布式资源管理系统，主要包括：

### 核心架构
1. **服务器端 (torque-server)**: 
   - 中央资源管理和作业调度
   - 队列管理和策略执行
2. **客户端 (torque-client)**: 
   - 作业提交和管理工具
   - 用户界面命令
3. **节点端 (torque-client中的MOM)**: 
   - 计算节点资源监控
   - 作业执行环境管理
4. **调度器 (torque-scheduler)**:
   - 作业调度策略实现
   - 资源分配算法

### 扩展功能
5. **开发支持 (torque-devel)**: API和库文件
6. **图形界面 (torque-gui)**: 可视化管理工具  
7. **标准API (torque-drmaa)**: 兼容DRMAA标准

### 技术特性
- **多平台支持**: Linux、Cygwin、macOS、Solaris
- **可选特性**: 根据需求启用高级功能
- **模块化部署**: 灵活的组件安装选择

这种设计允许在不同规模和需求的环境中灵活部署，从小型工作站到大型超级计算机集群都能适应。