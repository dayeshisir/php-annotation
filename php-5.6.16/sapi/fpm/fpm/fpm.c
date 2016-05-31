
	/* $Id: fpm.c,v 1.23 2008/07/20 16:38:31 anight Exp $ */
	/* (c) 2007,2008 Andrei Nigmatulin */

#include "fpm_config.h"

#include <stdlib.h> /* for exit */

#include "fpm.h"
#include "fpm_children.h"
#include "fpm_signals.h"
#include "fpm_env.h"
#include "fpm_events.h"
#include "fpm_cleanup.h"
#include "fpm_php.h"
#include "fpm_sockets.h"
#include "fpm_unix.h"
#include "fpm_process_ctl.h"
#include "fpm_conf.h"
#include "fpm_worker_pool.h"
#include "fpm_scoreboard.h"
#include "fpm_stdio.h"
#include "fpm_log.h"
#include "zlog.h"

struct fpm_globals_s fpm_globals = {
	.parent_pid = 0, 
	.argc = 0,
	.argv = NULL,
	.config = NULL,
	.prefix = NULL,
	.pid = NULL,
	.running_children = 0,
	.error_log_fd = 0,
	.log_level = 0,
	.listening_socket = 0,
	.max_requests = 10,
	.is_child = 0,
	.test_successful = 0,
	.heartbeat = 0,
	.run_as_root = 0,
	.force_stderr = 0,
	.send_config_pipe = {0, 0},
};

int fpm_init(int argc, char **argv, char *config, char *prefix, char *pid, int test_conf, int run_as_root, int force_daemon, int force_stderr) /* {{{ */
{
	fpm_globals.argc = argc;
	fpm_globals.argv = argv;

	// 拷贝配置文件
	if (config && *config) {
		fpm_globals.config = strdup(config);
	}

	// 设置全局变量
	fpm_globals.prefix = prefix;
	fpm_globals.pid = pid;
	fpm_globals.run_as_root = run_as_root;
	fpm_globals.force_stderr = force_stderr;

	if (
		0 > fpm_php_init_main()           || // 使用fpm_cleanup_add注册回收资源的回调函数，fpm_cleanup_add多处都会用到，用来主动注册特定的析构函数
	    0 > fpm_stdio_init_main()         || // 标准输入输出重定向到空设备，相当于关掉
	    0 > fpm_conf_init_main(test_conf, force_daemon) || // 初始化配置文件
	    0 > fpm_unix_init_main()          || // 主要就是初始化linux系统维度的一些诸如最大的fd、页面大小等参数；另外，根据参数,判断是否初始化守护进程。
		// 在守护进程模式下，会进行folk，然后前台进程会等待新folk出的后台进程执行其他初始化,两进程之间用管道通信,老进程读管道,新进程写管道.
		// 当新进程执行完成后,会向管道写数据,老进程收数据后判断,然后退出
	    0 > fpm_scoreboard_init_main()    || // 初始化scoreboard(可以理解为状态榜),采用共享内存的方式，记录每个worker的运行状态，以方便fpm主进程统计slow request等信息
	    0 > fpm_pctl_init_main()          || // 保存启动时的命令行参数，用于后续的进程控制，比如reload的时候需要这些参数
	    0 > fpm_env_init_main()           || // 环境相关，目前就是可以通过命令行参数修改主进程的名字
	    0 > fpm_signals_init_main()       || // 注册相关的信号回调函数
	    0 > fpm_children_init_main()      || // 主要是为了实现紧急重启功能而申请一块内存，记录子进程非正常(SIGSEGV or SIGBUS)退出的时间队列
	    0 > fpm_sockets_init_main()       || // 为每个pool监听指定的socket
	    0 > fpm_worker_pool_init_main()   || // 初始化worker pool
	    0 > fpm_event_init_main()) { // 初始化事件

		if (fpm_globals.test_successful) {
			exit(FPM_EXIT_OK);
		} else {
			zlog(ZLOG_ERROR, "FPM initialization failed");
			return -1;
		}
	}

	if (0 > fpm_conf_write_pid()) {
		zlog(ZLOG_ERROR, "FPM initialization failed");
		return -1;
	}

	fpm_stdio_init_final();
	return 0;
}
/* }}} */

/*	children: return listening socket
	parent: never return */
int fpm_run(int *max_requests) /* {{{ */
{
	// fpm worker进程池
	struct fpm_worker_pool_s *wp;
	
	int tmp_count = 1;
	/* create initial children in all pools */
	for (wp = fpm_worker_all_pools; wp; wp = wp->next) {
		int is_parent;
		zlog(ZLOG_NOTICE, "pa -> fpm_run worker loop, current worker count : [%d]", tmp_count++);

		// 创建children初始化
		// 1.添加事件
		// 2.folk子进程
		is_parent = fpm_children_create_initial(wp);

		if (!is_parent) {
			goto run_child;
		}

		/* handle error */
		// 异常处理
		if (is_parent == 2) {
			fpm_pctl(FPM_PCTL_STATE_TERMINATING, FPM_PCTL_ACTION_SET);
			fpm_event_loop(1);
		}
	}

	/* run event loop forever */
	// 父进程进入事件死循环
	fpm_event_loop(0);

run_child: /* only workers reach this point */

	// 回收资源
	fpm_cleanups_run(FPM_CLEANUP_CHILD);

	*max_requests = fpm_globals.max_requests;
	// 返回监听的socket,默认都是0
	return fpm_globals.listening_socket;
}
/* }}} */

