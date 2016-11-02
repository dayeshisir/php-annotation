/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2015 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Dmitry Stogov <dmitry@zend.com>                             |
   +----------------------------------------------------------------------+
*/

/* $Id: fastcgi.h 272370 2008-12-31 11:15:49Z sebastian $ */

/* FastCGI protocol */
/*中文协议内容:http://andylin02.iteye.com/blog/648412*/

// 讲解很清楚
// http://www.tuicool.com/articles/7nmiYvQ
// http://blog.chinaunix.net/uid-380521-id-2412484.html

#define FCGI_VERSION_1 1

#define FCGI_MAX_LENGTH 0xffff

#define FCGI_KEEP_CONN  1

// 说明我们对php-fpm发起请求时，我们想让php-fpm为我们扮演什么角色(做什么，或理解为杂么做)
typedef enum _fcgi_role {
	FCGI_RESPONDER	= 1,    // php-fpm接受我们的http所关联的信息，并产生个响应
	FCGI_AUTHORIZER	= 2,    // php-fpm会对我们的请求进行认证，认证通过的其会返回响应，认证不通过则关闭请求
	FCGI_FILTER     = 3     // 过滤请求中的额外数据流，并产生过滤后的http响应
} fcgi_role;


/**
 * 来自:http://blog.csdn.net/Shreck66/article/details/50355729
type值	具体含义
1	在与php-fpm建立连接之后发送的第一个消息中的type值就得为1，用来表明此消息为请求开始的第一个消息
2	异常断开与php-fpm的交互
3	在与php-fpm交互中所发的最后一个消息中type值为此，以表明交互的正常结束
4	在交互过程中给php-fpm传递环境参数时，将type设为此，以表明消息中包含的数据为某个name-value对
5	web服务器将从浏览器接收到的POST请求数据(表单提交等)以消息的形式发给php-fpm,这种消息的type就得设为5
6	php-fpm给web服务器回的正常响应消息的type就设为6
7	php-fpm给web服务器回的错误响应设为7
*/
typedef enum _fcgi_request_type {
	FCGI_BEGIN_REQUEST		=  1, /* [in]                              */
	FCGI_ABORT_REQUEST		=  2, /* [in]  (not supported)             */
	FCGI_END_REQUEST		=  3, /* [out]                             */
	FCGI_PARAMS			=  4, /* [in]  environment variables       */
	FCGI_STDIN			=  5, /* [in]  post data                   */
	FCGI_STDOUT			=  6, /* [out] response                    */
	FCGI_STDERR			=  7, /* [out] errors                      */
	FCGI_DATA			=  8, /* [in]  filter data (not supported) */
	FCGI_GET_VALUES			=  9, /* [in]                              */
	FCGI_GET_VALUES_RESULT	= 10          /* [out]                             */
} fcgi_request_type;

typedef enum _fcgi_protocol_status {
	FCGI_REQUEST_COMPLETE	= 0,      // 请求的正常结束
	FCGI_CANT_MPX_CONN	= 1,      // 拒绝新请求。这发生在Web服务器通过一条线路向应用发送并发的请求时，后者被设计为每条线路每次处理一个请求
	FCGI_OVERLOADED		= 2,      // 拒绝新请求。这发生在应用用完某些资源时，例如数据库连接
	FCGI_UNKNOWN_ROLE	= 3       // 拒绝新请求。这发生在Web服务器指定了一个应用不能识别的角色时
} dcgi_protocol_status;


typedef struct _fcgi_header {
	unsigned char version;          // FastCGI协议版本
	unsigned char type;             // 标识FastCGI记录类型，也就是记录执行的一般职能
	unsigned char requestIdB1;      // 标识记录所属的FastCGI请求
	unsigned char requestIdB0;      // 标识记录所属的FastCGI请求 requestId = requestIdB1 << 8 + requestIdB0
	unsigned char contentLengthB1;  // 记录的contentData组件的字节数
	unsigned char contentLengthB0;  // 记录的contentData组件的字节数  contentLength = contentLengthB1 << 8 + contentLengthB0
	unsigned char paddingLength;    // 填充字节的长度
	unsigned char reserved;         // 保留字节
} fcgi_header;

// 发起请求的消息体,对应消息头type=1的情形
typedef struct _fcgi_begin_request {
	unsigned char roleB1;           // web服务器所期望php-fpm扮演的角色, 详情见fcgi_role标注
	unsigned char roleB0;
	unsigned char flags;            // 标识处理完后是否关闭链接,1:长链接,既不关闭,接下来还会继续处理; 为0则在每次请求处理结束之后关闭连接 
	unsigned char reserved[5];      // 保留
} fcgi_begin_request;

// php一次请求会当做一个record来表示
// 请求发起record记录如下,包含 [消息头 + 消息体]
typedef struct _fcgi_begin_request_rec {
	fcgi_header hdr;
	fcgi_begin_request body;
} fcgi_begin_request_rec;

// 请求结束的消息体,对应消息头type=3的情形
typedef struct _fcgi_end_request {
	unsigned char appStatusB3;      // 结束状态，0为正常
	unsigned char appStatusB2;
	unsigned char appStatusB1;
	unsigned char appStatusB0;
	unsigned char protocolStatus;   // 协议状态
	unsigned char reserved[3];
} fcgi_end_request;

// php一次请求会当做一个record来表示
// 请求结束record记录如下,包含 [消息头 + 消息体]
typedef struct _fcgi_end_request_rec {
	fcgi_header hdr;
	fcgi_end_request body;
} fcgi_end_request_rec;

/* FastCGI client API */

typedef struct _fcgi_request {
	int            listen_socket;
#ifdef _WIN32
	int            tcp;
#endif
	int            fd;
	int            id;
	int            keep;
	int            closed;

	int            in_len;
	int            in_pad;

	fcgi_header   *out_hdr;
	unsigned char *out_pos;
	unsigned char  out_buf[1024*8];
	unsigned char  reserved[sizeof(fcgi_end_request_rec)];

	HashTable     *env;
} fcgi_request;

int fcgi_init(void);
void fcgi_shutdown(void);
void fcgi_init_request(fcgi_request *req, int listen_socket);
int fcgi_accept_request(fcgi_request *req);
int fcgi_finish_request(fcgi_request *req, int force_close);

void fcgi_set_in_shutdown(int);
void fcgi_set_allowed_clients(char *);
void fcgi_close(fcgi_request *req, int force, int destroy);

char* fcgi_getenv(fcgi_request *req, const char* var, int var_len);
char* fcgi_putenv(fcgi_request *req, char* var, int var_len, char* val);

int fcgi_read(fcgi_request *req, char *str, int len);

ssize_t fcgi_write(fcgi_request *req, fcgi_request_type type, const char *str, int len);
int fcgi_flush(fcgi_request *req, int close);

void fcgi_set_mgmt_var(const char * name, size_t name_len, const char * value, size_t value_len);
void fcgi_free_mgmt_var_cb(void * ptr);

const char *fcgi_get_last_client_ip();

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
