# PBS Server 服务接口定义与功能分析

## 概述

本文档分析 TORQUE (PBS) 系统中 `pbs_server` 服务的接口定义和功能。PBS Server 是资源管理系统的核心组件，负责接收客户端请求、管理作业队列、协调资源分配并与调度器交互。

## 核心架构

### 1. 主要组件

- **主服务进程** (`pbsd_main.c`) - 服务器启动和主循环
- **请求处理器** (`process_request.c`, `incoming_request.c`) - 处理客户端请求
- **接口定义** (`libpbs.h`, `batch_request.h`) - API 结构和协议定义
- **请求处理器** (`req_*.c`) - 特定请求类型的处理函数
- **服务器功能** (`svr_*.c`) - 核心服务器功能模块

### 2. 网络架构

PBS Server 使用基于 TCP 的客户端-服务器架构：
- **DIS 协议** (Data Interchange Service) - 自定义二进制协议
- **多线程处理** - 支持并发请求处理
- **Unix 域套接字支持** - 本地通信优化

## 接口定义

### 1. 批处理请求类型

根据 `pbs_batchreqtype_db.h`，系统支持以下主要请求类型：

#### 作业管理请求
```c
PBS_BATCH_QueueJob       // 提交作业
PBS_BATCH_QueueJob2      // 提交作业 (v2)
PBS_BATCH_DeleteJob      // 删除作业
PBS_BATCH_HoldJob        // 暂停作业
PBS_BATCH_ReleaseJob     // 释放作业
PBS_BATCH_ModifyJob      // 修改作业
PBS_BATCH_MoveJob        // 移动作业
PBS_BATCH_RunJob         // 运行作业
PBS_BATCH_SignalJob      // 向作业发送信号
PBS_BATCH_Rerun          // 重新运行作业
```

#### 状态查询请求
```c
PBS_BATCH_StatusJob      // 查询作业状态
PBS_BATCH_StatusQue      // 查询队列状态
PBS_BATCH_StatusSvr      // 查询服务器状态
PBS_BATCH_StatusNode     // 查询节点状态
PBS_BATCH_SelectJobs     // 选择作业
```

#### 系统管理请求
```c
PBS_BATCH_Manager        // 管理器操作
PBS_BATCH_Shutdown       // 关闭服务器
PBS_BATCH_Connect        // 建立连接
PBS_BATCH_Disconnect     // 断开连接
```

### 2. 请求结构定义

#### 基本请求结构 (`batch_request`)
```c
struct batch_request {
    list_link           rq_link;        // 请求链表链接
    int                 rq_type;        // 请求类型
    int                 rq_perm;        // 用户权限
    int                 rq_fromsvr;     // 是否来自其他服务器
    int                 rq_conn;        // 套接字连接
    long                rq_time;        // 请求创建时间
    char                rq_user[PBS_MAXUSER+1];     // 用户名
    char                rq_host[PBS_MAXHOSTNAME+1]; // 主机名
    struct batch_reply  rq_reply;       // 回复区域
    union indep_request rq_ind;         // 请求特定数据
};
```

#### 特定请求结构示例

**作业提交请求** (`rq_queuejob`)
```c
struct rq_queuejob {
    char   rq_job[PBS_MAXCLTJOBID+1];   // 作业ID
    char   rq_destin[PBS_MAXDEST+1];    // 目标队列
    tlist_head rq_attr;                 // 作业属性列表
};
```

**作业状态请求** (`rq_status`)
```c
struct rq_status {
    char       rq_id[PBS_MAXSVRJOBID+1]; // 对象ID
    tlist_head rq_attr;                   // 属性列表
};
```

### 3. 回复结构定义

#### 基本回复结构 (`batch_reply`)
```c
struct batch_reply {
    int brp_code;       // 返回码
    int brp_auxcode;    // 辅助返回码
    int brp_choice;     // 联合体判别器
    union {
        char                brp_jid[PBS_MAXSVRJOBID+1];
        struct brp_select  *brp_select;    // 选择回复
        tlist_head          brp_status;    // 状态回复
        struct brp_cmdstat *brp_statc;     // 命令状态回复
        struct {
            size_t   brp_txtlen;
            char    *brp_str;
        } brp_txt;                         // 文本回复
    };
};
```

## API 函数

### 1. 客户端 API 函数 (PBSD_*)

根据 `libpbs.h`，主要 API 函数包括：

#### 作业管理 API
```c
// 提交作业
char *PBSD_queuejob(int c, int *err, const char *jobid, 
                    const char *dest, struct attropl *attr, char *extend);

// 删除作业  
int PBSD_deljob(int connect, char *jobid, char *extend);

// 修改作业
int PBSD_alterjob(int connect, char *jobid, struct attrl *attrib, char *extend);

// 暂停/释放作业
int PBSD_holdjob(int connect, char *jobid, char *extend);
int PBSD_rlsjob(int connect, char *jobid, char *extend);

// 信号作业
int PBSD_sig_put(int connect, const char *jobid, const char *signal, char *extend);
```

#### 状态查询 API
```c
// 查询状态
struct batch_status *PBSD_status(int c, int function, int *err, 
                                 char *id, struct attrl *attrib, char *extend);

// 选择作业
struct batch_status *PBSD_selstat(int c, struct attropl *sel_list, 
                                 struct attrl *attrib, char *extend);
```

#### 连接管理 API
```c
// 建立连接
int pbs_connect(char *server);

// 断开连接  
int pbs_disconnect(int connect);
```

### 2. 服务器内部处理函数

#### 请求分发函数 (`dispatch_request`)
```c
int dispatch_request(int sfds, batch_request *request);
```

该函数根据请求类型分发到相应的处理函数：
- `req_quejob()` - 处理作业提交
- `req_deletejob()` - 处理作业删除
- `req_holdjob()` - 处理作业暂停
- `req_stat()` - 处理状态查询
- 等等...

## 服务器主要功能

### 1. 服务器启动和初始化

**主函数流程** (`pbsd_main.c`):
1. 解析命令行参数
2. 检查运行权限 (需要 root)
3. 检查网络端口可用性
4. 初始化服务器属性
5. 设置信号处理
6. 后台化进程 (可选)
7. 初始化网络通信
8. 进入主循环

### 2. 主服务循环

**主循环** (`main_loop()`) 负责：
- 启动接受连接线程
- 启动路由重试线程
- 监控线程状态
- 处理定时任务
- 维护节点状态
- 与调度器交互

### 3. 请求处理流程

1. **接收连接** (`start_process_pbs_server_port`)
   - 监听客户端连接
   - 为每个连接创建处理线程

2. **协议识别** (`get_protocol_type`)
   - 识别 PBS 批处理协议
   - 处理超时情况

3. **请求读取** (`read_request_from_socket`)
   - 从套接字读取请求数据
   - 解码为内部请求结构
   - 进行权限验证

4. **请求分发** (`dispatch_request`)
   - 根据请求类型调用相应处理函数
   - 记录请求日志

5. **回复发送** (`reply_send`)
   - 将处理结果编码并发送回客户端

#### 典型请求处理示例

**作业提交请求处理** (`req_quejob`):
```c
int req_quejob(batch_request *preq, int version)
{
    // 1. 获取和验证作业ID
    if ((rc = get_job_id(preq, resc_access_perm, created_here, jobid)) != PBSE_NONE)
        return(rc);
    
    // 2. 检查作业是否已存在
    if (job_exists(jobid.c_str()) == true) {
        req_reject(PBSE_JOBEXIST, 0, preq, NULL, NULL);
        return(PBSE_JOBEXIST);
    }
    
    // 3. 获取目标队列
    pque = get_queue_for_job(preq->rq_ind.rq_queuejob.rq_destin, rc);
    
    // 4. 创建作业对象
    pj = job_alloc();
    
    // 5. 设置作业属性
    // 6. 验证作业参数
    // 7. 将作业加入队列
    // 8. 发送回复
}
```

**状态查询请求处理**:
```c
switch (request->rq_type) {
    case PBS_BATCH_StatusJob:
        rc = req_stat_job(request);
        break;
    case PBS_BATCH_StatusQue:
        rc = req_stat_que(request);
        break;
    case PBS_BATCH_StatusSvr:
        rc = req_stat_svr(request);
        break;
    case PBS_BATCH_StatusNode:
        rc = req_stat_node(request);
        break;
}
```

### 4. 权限和安全

**访问控制**:
- 基于主机的 ACL 控制
- Unix 域套接字认证
- 用户权限验证
- 管理员权限检查

**权限标志**:
```c
#define ATR_DFLAG_USRD   0x01    // 用户读权限
#define ATR_DFLAG_USWR   0x02    // 用户写权限  
#define ATR_DFLAG_OPRD   0x04    // 操作员读权限
#define ATR_DFLAG_OPWR   0x08    // 操作员写权限
#define ATR_DFLAG_MGRD   0x10    // 管理员读权限
#define ATR_DFLAG_MGWR   0x20    // 管理员写权限
#define ATR_DFLAG_SvWR   0x40    // 服务器写权限
```

## 网络通信协议

### 1. DIS 协议

PBS 使用自定义的 DIS (Data Interchange Service) 协议：
- **二进制格式** - 高效的数据传输
- **平台无关** - 处理字节序问题
- **类型安全** - 内置类型检查
- **协议版本** - `PBS_BATCH_PROT_VER = 2`

### 2. 默认端口配置

```c
#define PBS_BATCH_SERVICE_PORT      15001  // 服务器主端口
#define PBS_MOM_SERVICE_PORT        15002  // MOM 服务端口
#define PBS_MANAGER_SERVICE_PORT    15003  // 管理器端口
#define PBS_SCHEDULER_SERVICE_PORT  15004  // 调度器端口
#define TRQ_AUTHD_SERVICE_PORT      15005  // 认证服务端口
```

### 3. 连接管理

**连接类型**:
```c
enum conn_type {
    Primary,           // 主连接
    FromClientDIS,     // 来自客户端的 DIS 连接
    ToServerDIS        // 到服务器的 DIS 连接
};
```

**连接状态跟踪**:
```c
struct connection {
    enum conn_type  cn_active;      // 连接状态
    int             cn_sock;        // 套接字描述符
    unsigned short  cn_socktype;    // 套接字类型
    unsigned short  cn_authen;      // 认证状态
    time_t          cn_lasttime;    // 最后活动时间
    unsigned long   cn_addr;        // 客户端地址
    pthread_mutex_t *cn_mutex;      // 连接互斥锁
};
```

### 4. 消息编码/解码

**编码函数**:
```c
// 作业相关编码
int encode_DIS_JobId(struct tcp_chan *chan, char *jobid);
int encode_DIS_QueueJob(struct tcp_chan *chan, const char *jid, 
                        const char *dest, struct attropl *attr);
int encode_DIS_RunJob(struct tcp_chan *chan, char *jid, 
                      char *where, unsigned int resch);

// 管理相关编码
int encode_DIS_Manage(struct tcp_chan *chan, int cmd, int objt, 
                      const char *objname, struct attropl *attr);
int encode_DIS_Status(struct tcp_chan *chan, char *objid, struct attrl *attr);

// 信号和控制编码
int encode_DIS_SignalJob(struct tcp_chan *chan, const char *jid, const char *sig);
int encode_DIS_ShutDown(struct tcp_chan *chan, int manner);
```

**解码函数**:
```c
int decode_DIS_JobId(struct tcp_chan *chan, char *jobid);
int decode_DIS_replyCmd(struct tcp_chan *chan, struct batch_reply *reply);
```

### 5. 协议超时设置

```c
time_t pbs_incoming_tcp_timeout = PBS_TCPINTIMEOUT;  // 传入连接超时
const int SHORT_TIMEOUT = 5;                        // 短超时时间
```

## 配置和属性

### 1. 服务器属性

服务器支持多种可配置属性 (参见 `pbs_server_attributes.7`)：
- `server_state` - 服务器状态
- `scheduling` - 调度状态
- `log_events` - 日志事件掩码
- `mail_from` - 邮件发送地址
- `resources_max` - 最大资源限制

### 2. 日志系统

**日志级别**:
- 0-7 级别详细程度
- 支持多种事件类型记录
- 可配置日志轮转

## 高可用性支持

PBS Server 支持高可用性模式：
- **主备切换** - 自动故障转移
- **锁文件机制** - 防止多实例运行
- **状态同步** - 服务器间状态同步

## 总结

PBS Server 提供了一个完整的批处理作业管理服务，具有以下特点：

1. **模块化设计** - 清晰的功能分离
2. **可扩展性** - 支持多种请求类型扩展
3. **高性能** - 多线程并发处理
4. **安全性** - 完善的权限控制机制
5. **可靠性** - 高可用性和错误处理
6. **互操作性** - 标准化的协议接口

这种设计使得 PBS Server 能够有效地管理大规模计算集群的作业调度和资源分配。

## 附录 A: 完整请求类型目录

以下是 PBS Server 支持的所有请求类型的完整列表：

### 核心作业管理
| 请求类型 | 编号 | 功能描述 | 对应处理函数 |
|---------|------|----------|-------------|
| PBS_BATCH_Connect | 0 | 建立客户端连接 | - |
| PBS_BATCH_QueueJob | 1 | 提交作业到队列 | `req_quejob()` |
| PBS_BATCH_QueueJob2 | 29 | 提交作业到队列 (v2) | `req_quejob()` |
| PBS_BATCH_JobCred | 2 | 作业认证信息 | `req_jobcredential()` |
| PBS_BATCH_jobscript | 3 | 上传作业脚本 | `req_jobscript()` |
| PBS_BATCH_jobscript2 | 31 | 上传作业脚本 (v2) | `req_jobscript()` |
| PBS_BATCH_RdytoCommit | 4 | 准备提交作业 | `req_rdytocommit()` |
| PBS_BATCH_Commit | 5 | 提交作业 | `req_commit()` |
| PBS_BATCH_Commit2 | 30 | 提交作业 (v2) | `req_commit2()` |

### 作业操作控制
| 请求类型 | 编号 | 功能描述 | 对应处理函数 |
|---------|------|----------|-------------|
| PBS_BATCH_DeleteJob | 6 | 删除作业 | `req_deletejob()` |
| PBS_BATCH_HoldJob | 7 | 暂停作业 | `req_holdjob()` |
| PBS_BATCH_ReleaseJob | 13 | 释放作业 | `req_releasejob()` |
| PBS_BATCH_ModifyJob | 11 | 修改作业属性 | `req_modifyjob()` |
| PBS_BATCH_MoveJob | 12 | 移动作业到其他队列 | `req_movejob()` |
| PBS_BATCH_RunJob | 15 | 强制运行作业 | `req_runjob()` |
| PBS_BATCH_Rerun | 14 | 重新运行作业 | `req_rerunjob()` |
| PBS_BATCH_SignalJob | 18 | 向作业发送信号 | `req_signaljob()` |
| PBS_BATCH_AsySignalJob | 60 | 异步信号作业 | `req_signaljob()` |

### 状态查询
| 请求类型 | 编号 | 功能描述 | 对应处理函数 |
|---------|------|----------|-------------|
| PBS_BATCH_StatusJob | 19 | 查询作业状态 | `req_stat_job()` |
| PBS_BATCH_StatusQue | 20 | 查询队列状态 | `req_stat_que()` |
| PBS_BATCH_StatusSvr | 21 | 查询服务器状态 | `req_stat_svr()` |
| PBS_BATCH_StatusNode | 58 | 查询节点状态 | `req_stat_node()` |
| PBS_BATCH_SelectJobs | 16 | 选择作业 | `req_selectjobs()` |
| PBS_BATCH_SelStat | 51 | 选择状态 | `req_selstat()` |
| PBS_BATCH_SelStatAttr | 64 | 选择状态属性 | `req_selstatattr()` |

### 系统管理
| 请求类型 | 编号 | 功能描述 | 对应处理函数 |
|---------|------|----------|-------------|
| PBS_BATCH_Manager | 9 | 管理器操作 | `req_manager()` |
| PBS_BATCH_Shutdown | 17 | 关闭服务器 | `req_shutdown()` |
| PBS_BATCH_ModifyNode | 66 | 修改节点属性 | `req_modifynode()` |
| PBS_BATCH_ChangePowerState | 65 | 改变电源状态 | `req_powerstate()` |

### 高级功能
| 请求类型 | 编号 | 功能描述 | 对应处理函数 |
|---------|------|----------|-------------|
| PBS_BATCH_LocateJob | 8 | 定位作业位置 | `req_locate()` |
| PBS_BATCH_TrackJob | 22 | 跟踪作业 | `req_track()` |
| PBS_BATCH_MessJob | 10 | 向作业发送消息 | `req_message()` |
| PBS_BATCH_Rescq | 24 | 资源查询 | `req_rescquery()` |
| PBS_BATCH_RegistDep | 52 | 注册依赖关系 | `req_register()` |

### 文件操作
| 请求类型 | 编号 | 功能描述 | 对应处理函数 |
|---------|------|----------|-------------|
| PBS_BATCH_StageIn | 48 | 文件暂存输入 | `req_stagein()` |
| PBS_BATCH_ReturnFiles | 53 | 返回文件 | `req_returnfiles()` |
| PBS_BATCH_CopyFiles | 54 | 复制文件 | `req_copyfiles()` |
| PBS_BATCH_DelFiles | 55 | 删除文件 | `req_delfiles()` |
| PBS_BATCH_MvJobFile | 57 | 移动作业文件 | `req_mvjobfile()` |

### 认证和连接
| 请求类型 | 编号 | 功能描述 | 对应处理函数 |
|---------|------|----------|-------------|
| PBS_BATCH_AuthenUser | 49 | 用户认证 | `req_authenuser()` |
| PBS_BATCH_AltAuthenUser | 61 | 备用用户认证 | `req_altauthenuser()` |
| PBS_BATCH_Disconnect | 59 | 断开连接 | - |

### 专用功能
| 请求类型 | 编号 | 功能描述 | 对应处理函数 |
|---------|------|----------|-------------|
| PBS_BATCH_JobObit | 56 | 作业讣告 | `req_jobobit()` |
| PBS_BATCH_GpuCtrl | 62 | GPU 控制 | `req_gpuctrl()` |
| PBS_BATCH_DeleteReservation | 63 | 删除 ALPS 预留 | `req_deletereservation()` |

## 附录 B: 错误代码参考

PBS Server 使用标准化的错误代码系统 (PBSE_*) 来表示不同的错误条件。主要错误类别包括：

- **PBSE_NONE** (0) - 成功
- **PBSE_BADHOST** - 无效主机
- **PBSE_BADCRED** - 认证失败  
- **PBSE_PERM** - 权限不足
- **PBSE_JOBNOTFOUND** - 作业不存在
- **PBSE_QUENOTFOUND** - 队列不存在
- **PBSE_SYSTEM** - 系统错误
- **PBSE_INTERNAL** - 内部错误