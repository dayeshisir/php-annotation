/*
   +----------------------------------------------------------------------+
   | Zend Engine                                                          |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998-2014 Zend Technologies Ltd. (http://www.zend.com) |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        | 
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
   | Authors: Andi Gutmans <andi@zend.com>                                |
   |          Zeev Suraski <zeev@zend.com>                                |
   +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "zend.h"
#include "zend_globals.h"

/**
* @desc 同一个桶内的节点链接起来，采用的是头插法
* @param element   : 待插入的节点
* @param list_head : 插入的桶表头 
*/
#define CONNECT_TO_BUCKET_DLLIST(element, list_head)		\
	(element)->pNext = (list_head);							\
	(element)->pLast = NULL;								\
	if ((element)->pNext) {									\
		(element)->pNext->pLast = (element);				\
	}

/**
* @desc 插入节点时，链接桶之间的指针，采用的是尾插法
* @param element ： 待插入的节点
* @param ht      :  hash结构
*/
#define CONNECT_TO_GLOBAL_DLLIST(element, ht)				\
	(element)->pListLast = (ht)->pListTail;					\
	(ht)->pListTail = (element);							\
	(element)->pListNext = NULL;							\
	if ((element)->pListLast != NULL) {						\
		(element)->pListLast->pListNext = (element);		\
	}														\
	if (!(ht)->pListHead) {									\
		(ht)->pListHead = (element);						\
	}														\
	if ((ht)->pInternalPointer == NULL) {					\
		(ht)->pInternalPointer = (element);					\
	}

#if ZEND_DEBUG
#define HT_OK				0
#define HT_IS_DESTROYING	1
#define HT_DESTROYED		2
#define HT_CLEANING			3

/**
* @desc 测试用的“辅助”函数
*/
static void _zend_is_inconsistent(const HashTable *ht, const char *file, int line)
{
	if (ht->inconsistent==HT_OK) {
		return;
	}
	switch (ht->inconsistent) {
		case HT_IS_DESTROYING:
			zend_output_debug_string(1, "%s(%d) : ht=%p is being destroyed", file, line, ht);
			break;
		case HT_DESTROYED:
			zend_output_debug_string(1, "%s(%d) : ht=%p is already destroyed", file, line, ht);
			break;
		case HT_CLEANING:
			zend_output_debug_string(1, "%s(%d) : ht=%p is being cleaned", file, line, ht);
			break;
		default:
			zend_output_debug_string(1, "%s(%d) : ht=%p is inconsistent", file, line, ht);
			break;
	}
	zend_bailout();
}
#define IS_CONSISTENT(a) _zend_is_inconsistent(a, __FILE__, __LINE__);
#define SET_INCONSISTENT(n) ht->inconsistent = n;
#else
#define IS_CONSISTENT(a)
#define SET_INCONSISTENT(n)
#endif

/**
* @desc 递归包含开启的时候，递归调用3次就报错
*/
#define HASH_PROTECT_RECURSION(ht)														\
	if ((ht)->bApplyProtection) {														\
		if ((ht)->nApplyCount++ >= 3) {													\
			zend_error(E_ERROR, "Nesting level too deep - recursive dependency?");		\
		}																				\
	}

/**
* @desc 递归包含开启的时候，上个函数的反函数
*/
#define HASH_UNPROTECT_RECURSION(ht)													\
	if ((ht)->bApplyProtection) {														\
		(ht)->nApplyCount--;															\
	}

/**
* @desc 判断hash是否需要扩容了
*/
#define ZEND_HASH_IF_FULL_DO_RESIZE(ht)				\
	if ((ht)->nNumOfElements > (ht)->nTableSize) {	\
		zend_hash_do_resize(ht);					\
	}

/**
* @desc 重新hash占位符
*       注意这里的static，仅本文件可用哦
*/
static int zend_hash_do_resize(HashTable *ht);

/**
* @desc 给定值和值的长度，计算hash值
*       这里采用的是默认哈希函数
*/
ZEND_API ulong zend_hash_func(const char *arKey, uint nKeyLength)
{
	return zend_inline_hash_func(arKey, nKeyLength);
}

/**
* @desc  更新哈希表ht中p节点的数据为pData
* @param ht        : 哈希表结构
* @param p         : 待更新的节点
* @param pData     : 更新的值
* @param nDataSize : pData字节大小  
*/
#define UPDATE_DATA(ht, p, pData, nDataSize)											\
	// 如果pData是指针
	if (nDataSize == sizeof(void*)) {													\
		// 如果两个指针各有所指，释放掉pData
		if ((p)->pData != &(p)->pDataPtr) {												\
			pefree_rel((p)->pData, (ht)->persistent);									\
		}                                                                               \
		// 将数据存放在pDataPtr中，如头文件所述                                         \
		// 然后修正pData的指向 如果原先pData就是指向pDataPtr，那么是否意味着不需要再次修正呢？	\																			\
		memcpy(&(p)->pDataPtr, pData, sizeof(void *));									\
		(p)->pData = &(p)->pDataPtr;													\
	} else {                                                                            \	
	    // 进到这个分支 那么就不是指针                                                  \
	    // 如果原先是指针样式，就需要另辟空间存放数据了			                        \
	    // 这种情形pDataPtr是废弃的													    \
		if ((p)->pData == &(p)->pDataPtr) {												\
			(p)->pData = (void *) pemalloc_rel(nDataSize, (ht)->persistent);			\
			(p)->pDataPtr=NULL;															\
		} else {																		\
			(p)->pData = (void *) perealloc_rel((p)->pData, nDataSize, (ht)->persistent);	\
			/* (p)->pDataPtr is already NULL so no need to initialize it */				\
		}					                                                            \
		// 数据拷贝过来															\
		memcpy((p)->pData, pData, nDataSize);											\
	}

/**
* @desc 初始化一个结点的数据，详情可以参考UPDATE_DATA
*       此处不再赘述
*/
#define INIT_DATA(ht, p, pData, nDataSize);								\
	if (nDataSize == sizeof(void*)) {									\
		memcpy(&(p)->pDataPtr, pData, sizeof(void *));					\
		(p)->pData = &(p)->pDataPtr;									\
	} else {															\
		(p)->pData = (void *) pemalloc_rel(nDataSize, (ht)->persistent);\
		if (!(p)->pData) {												\
			pefree_rel(p, (ht)->persistent);							\
			return FAILURE;												\
		}																\
		memcpy((p)->pData, pData, nDataSize);							\
		(p)->pDataPtr=NULL;												\
	}

/**
* @desc 检查桶空间是否已经分配，没有分配的话，根据表空间的大小申请空间
*/
#define CHECK_INIT(ht) do {												\
	if (UNEXPECTED((ht)->nTableMask == 0)) {								\
		(ht)->arBuckets = (Bucket **) pecalloc((ht)->nTableSize, sizeof(Bucket *), (ht)->persistent);	\
		(ht)->nTableMask = (ht)->nTableSize - 1;						\
	}																	\
} while (0)
 
 // 空的桶长这个样子
static const Bucket *uninitialized_bucket = NULL;

/**
* @desc 初始化一个空的hash表结构
* @param ht            : hash表结构
* @param nSize         : 初始的桶空间大小
* @param pHashFunction : hash函数的函数指针
* @param pDestructor   : 节点释放空间的析构函数
* @param persisteng    : 是否常驻内存
*/
ZEND_API int _zend_hash_init(HashTable *ht, uint nSize, hash_func_t pHashFunction, dtor_func_t pDestructor, zend_bool persistent ZEND_FILE_LINE_DC)
{
	// 桶空间最小8个依据
	uint i = 3;

	SET_INCONSISTENT(HT_OK);

	// 防止上溢
	if (nSize >= 0x80000000) {
		/* prevent overflow */
		ht->nTableSize = 0x80000000;
	} else {
		// 查找最近的可以容纳当前大小的最小2的整数次方
		// 例如nSize=10，此处是要找到i=16
		while ((1U << i) < nSize) {
			i++;
		}
		ht->nTableSize = 1 << i;
	}

	// 笑而不语
	ht->nTableMask = 0;	/* 0 means that ht->arBuckets is uninitialized */
	ht->pDestructor = pDestructor;
	ht->arBuckets = (Bucket**)&uninitialized_bucket;
	ht->pListHead = NULL;
	ht->pListTail = NULL;
	ht->nNumOfElements = 0;
	ht->nNextFreeElement = 0;
	ht->pInternalPointer = NULL;
	ht->persistent = persistent;
	ht->nApplyCount = 0;
	ht->bApplyProtection = 1;
	return SUCCESS;
}

/**
* @desc 除了可以指定是否需要递归保护，其他操作与上个函数雷同
*/
ZEND_API int _zend_hash_init_ex(HashTable *ht, uint nSize, hash_func_t pHashFunction, dtor_func_t pDestructor, zend_bool persistent, zend_bool bApplyProtection ZEND_FILE_LINE_DC)
{
	int retval = _zend_hash_init(ht, nSize, pHashFunction, pDestructor, persistent ZEND_FILE_LINE_CC);

	ht->bApplyProtection = bApplyProtection;
	return retval;
}

/**
* @desc 设置ht的是否递归保护为bolApplyProtection，注：该功能默认是开启的
*/
ZEND_API void zend_hash_set_apply_protection(HashTable *ht, zend_bool bApplyProtection)
{
	ht->bApplyProtection = bApplyProtection;
}


/**
* @desc 往ht里面添加/修改key-value，只服务于关联数组，索引数组另有服务函数
* @param ht         : 哈希表
* @param arKey      : key 
* @param nKeyLength : key的长度
* @param pData      : value
* @param nDataSize  : value的长度
* @param pDest      : value为指针时的值
* @param flag       : 标识新增还是修改  
*/
ZEND_API int _zend_hash_add_or_update(HashTable *ht, const char *arKey, uint nKeyLength, void *pData, uint nDataSize, void **pDest, int flag ZEND_FILE_LINE_DC)
{
	ulong h;
	uint nIndex;
	Bucket *p;
#ifdef ZEND_SIGNALS
	TSRMLS_FETCH();
#endif

	IS_CONSISTENT(ht);

	// 异常情况，key长度为0 
	if (nKeyLength <= 0) {
#if ZEND_DEBUG
		ZEND_PUTS("zend_hash_update: Can't put in empty key\n");
#endif
		return FAILURE;
	}

	// 添加之前，确保hash表空间已经分配过了
	CHECK_INIT(ht);

	// 计算当前key的hash值
	h = zend_inline_hash_func(arKey, nKeyLength);
	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		// hash表中有该节点
		// 这地方分了两种情形（为什么分两种情况呢？？？？）
		if (p->arKey == arKey ||
			((p->h == h) && (p->nKeyLength == nKeyLength) && !memcmp(p->arKey, arKey, nKeyLength))) {
				if (flag & HASH_ADD) {
					return FAILURE;
				}
				HANDLE_BLOCK_INTERRUPTIONS();
#if ZEND_DEBUG
				if (p->pData == pData) {
					ZEND_PUTS("Fatal error in zend_hash_update: p->pData == pData\n");
					HANDLE_UNBLOCK_INTERRUPTIONS();
					return FAILURE;
				}
#endif
				// 更新value的时候先将原先的指针内存释放
				if (ht->pDestructor) {
					ht->pDestructor(p->pData);
				}
				// UPDATE_DATA不负责释放内存
				UPDATE_DATA(ht, p, pData, nDataSize);
				if (pDest) {
					*pDest = p->pData;
				}
				HANDLE_UNBLOCK_INTERRUPTIONS();
				return SUCCESS;
		}
		p = p->pNext;
	}
	
	// 执行到这里，操作只能是增加一个节点了
	// 如果arKey是字符串(???)
	// Zend/zend_string.h +37
	// #define IS_INTERNED(s) \
    //    (((s) >= CG(interned_strings_start)) && ((s) < CG(interned_strings_end)))
	if (IS_INTERNED(arKey)) {
		// 这里没有分配多余的空间
		// 也就是value是一个指针
		p = (Bucket *) pemalloc(sizeof(Bucket), ht->persistent);
		if (!p) {
			return FAILURE;
		}
		p->arKey = arKey;
	} else {
		p = (Bucket *) pemalloc(sizeof(Bucket) + nKeyLength, ht->persistent);
		if (!p) {
			return FAILURE;
		}
		// 注意这里的 p + 1 是跨过整个Bucket结构体空间
		// 结构体重的arKey存放的是他之后一个字节的地址
		// 而多分配的nKeyLength 存放的是value值
		p->arKey = (const char*)(p + 1);
		memcpy((char*)p->arKey, arKey, nKeyLength);
	}
	p->nKeyLength = nKeyLength;
	INIT_DATA(ht, p, pData, nDataSize);
	p->h = h;

	// 链入本桶内的双向链表中
	CONNECT_TO_BUCKET_DLLIST(p, ht->arBuckets[nIndex]);
	if (pDest) {
		*pDest = p->pData;
	}

	HANDLE_BLOCK_INTERRUPTIONS();

	// 链入全局双向链表中
	CONNECT_TO_GLOBAL_DLLIST(p, ht);
	ht->arBuckets[nIndex] = p;
	HANDLE_UNBLOCK_INTERRUPTIONS();

	// 没插入完成一个 判断是否需要扩容
	ht->nNumOfElements++;
	ZEND_HASH_IF_FULL_DO_RESIZE(ht);		/* If the Hash table is full, resize it */
	return SUCCESS;
}

ZEND_API int _zend_hash_quick_add_or_update(HashTable *ht, const char *arKey, uint nKeyLength, ulong h, void *pData, uint nDataSize, void **pDest, int flag ZEND_FILE_LINE_DC)
{
	uint nIndex;
	Bucket *p;
#ifdef ZEND_SIGNALS
	TSRMLS_FETCH();
#endif

	IS_CONSISTENT(ht);

	if (nKeyLength == 0) {
		return zend_hash_index_update(ht, h, pData, nDataSize, pDest);
	}

	CHECK_INIT(ht);
	nIndex = h & ht->nTableMask;
	
	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if (p->arKey == arKey ||
			((p->h == h) && (p->nKeyLength == nKeyLength) && !memcmp(p->arKey, arKey, nKeyLength))) {
				if (flag & HASH_ADD) {
					return FAILURE;
				}
				HANDLE_BLOCK_INTERRUPTIONS();
#if ZEND_DEBUG
				if (p->pData == pData) {
					ZEND_PUTS("Fatal error in zend_hash_update: p->pData == pData\n");
					HANDLE_UNBLOCK_INTERRUPTIONS();
					return FAILURE;
				}
#endif
				if (ht->pDestructor) {
					ht->pDestructor(p->pData);
				}
				UPDATE_DATA(ht, p, pData, nDataSize);
				if (pDest) {
					*pDest = p->pData;
				}
				HANDLE_UNBLOCK_INTERRUPTIONS();
				return SUCCESS;
		}
		p = p->pNext;
	}
	
	if (IS_INTERNED(arKey)) {
		p = (Bucket *) pemalloc(sizeof(Bucket), ht->persistent);
		if (!p) {
			return FAILURE;
		}
		p->arKey = arKey;
	} else {
		p = (Bucket *) pemalloc(sizeof(Bucket) + nKeyLength, ht->persistent);
		if (!p) {
			return FAILURE;
		}
		p->arKey = (const char*)(p + 1);
		memcpy((char*)p->arKey, arKey, nKeyLength);
	}

	p->nKeyLength = nKeyLength;
	INIT_DATA(ht, p, pData, nDataSize);
	p->h = h;
	
	CONNECT_TO_BUCKET_DLLIST(p, ht->arBuckets[nIndex]);

	if (pDest) {
		*pDest = p->pData;
	}

	HANDLE_BLOCK_INTERRUPTIONS();
	ht->arBuckets[nIndex] = p;
	CONNECT_TO_GLOBAL_DLLIST(p, ht);
	HANDLE_UNBLOCK_INTERRUPTIONS();

	ht->nNumOfElements++;
	ZEND_HASH_IF_FULL_DO_RESIZE(ht);		/* If the Hash table is full, resize it */
	return SUCCESS;
}


ZEND_API int zend_hash_add_empty_element(HashTable *ht, const char *arKey, uint nKeyLength)
{
	void *dummy = (void *) 1;

	return zend_hash_add(ht, arKey, nKeyLength, &dummy, sizeof(void *), NULL);
}

/**
* @desc 数字索引类型的key-value值添加
* @param ht        : 添加的hash表结构
* @param  h        : key 新增时 h 设置为0 更新时会透传上层的h
* @param pData     : value
* @param nDataSize ：value的长度
* @param pDest     : 用于指向下一个元素
* @param flag      : HASH_NEXT_INSERT or HASH_ADD    
*/
ZEND_API int _zend_hash_index_update_or_next_insert(HashTable *ht, ulong h, void *pData, uint nDataSize, void **pDest, int flag ZEND_FILE_LINE_DC)
{
	uint nIndex;
	Bucket *p;
#ifdef ZEND_SIGNALS
	TSRMLS_FETCH();
#endif

	IS_CONSISTENT(ht);
	CHECK_INIT(ht);

	// 如果是HASH_ADD，h为当前保留的最大下标
	// 如果是HASH_NEXT_INSERT, 两种情况 
	// 1、更新已有值，此时(h < nNextFreeElement)，定位到对应的元素，修改即可
	// 2、插入一个key-value对，此时 (h >= nNextFreeElement), 那么参考L515行注释
	if (flag & HASH_NEXT_INSERT) {
		h = ht->nNextFreeElement;
	}
	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if ((p->nKeyLength == 0) && (p->h == h)) {
			// 找到了要添加的value了，并且flag设置为添加（添加到XX之后或者直接添加）
			if (flag & HASH_NEXT_INSERT || flag & HASH_ADD) {
				return FAILURE;
			}
			HANDLE_BLOCK_INTERRUPTIONS();
#if ZEND_DEBUG
			if (p->pData == pData) {
				ZEND_PUTS("Fatal error in zend_hash_index_update: p->pData == pData\n");
				HANDLE_UNBLOCK_INTERRUPTIONS();
				return FAILURE;
			}
#endif
			if (ht->pDestructor) {
				ht->pDestructor(p->pData);
			}
			UPDATE_DATA(ht, p, pData, nDataSize);
			HANDLE_UNBLOCK_INTERRUPTIONS();
			// 当插入一个key-value对且key>nNextFreeElement时，及时修正nNextFreeElement值
			// 以保证下一个元素的下标是当前下标+1
			// 这个地方挺有意思的，例如当前已用下标情况是 0, 1, 2, 3, 9
			// 那么插入一个下标为8的元素，不更新nNextFreeElement 
			if ((long)h >= (long)ht->nNextFreeElement) {
				ht->nNextFreeElement = h < LONG_MAX ? h + 1 : LONG_MAX;
			}
			if (pDest) {
				*pDest = p->pData;
			}
			return SUCCESS;
		}
		p = p->pNext;
	}
	p = (Bucket *) pemalloc_rel(sizeof(Bucket), ht->persistent);
	if (!p) {
		return FAILURE;
	}
	p->arKey = NULL;
	p->nKeyLength = 0; /* Numeric indices are marked by making the nKeyLength == 0 */
	p->h = h;
	INIT_DATA(ht, p, pData, nDataSize);
	if (pDest) {
		*pDest = p->pData;
	}

	CONNECT_TO_BUCKET_DLLIST(p, ht->arBuckets[nIndex]);

	HANDLE_BLOCK_INTERRUPTIONS();
	ht->arBuckets[nIndex] = p;
	CONNECT_TO_GLOBAL_DLLIST(p, ht);
	HANDLE_UNBLOCK_INTERRUPTIONS();

	if ((long)h >= (long)ht->nNextFreeElement) {
		ht->nNextFreeElement = h < LONG_MAX ? h + 1 : LONG_MAX;
	}
	ht->nNumOfElements++;
	ZEND_HASH_IF_FULL_DO_RESIZE(ht);
	return SUCCESS;
}


static int zend_hash_do_resize(HashTable *ht)
{
	Bucket **t;
#ifdef ZEND_SIGNALS
	TSRMLS_FETCH();
#endif

	IS_CONSISTENT(ht);

	if ((ht->nTableSize << 1) > 0) {	/* Let's double the table size */
		t = (Bucket **) perealloc_recoverable(ht->arBuckets, (ht->nTableSize << 1) * sizeof(Bucket *), ht->persistent);
		if (t) {
			HANDLE_BLOCK_INTERRUPTIONS();
			ht->arBuckets = t;
			ht->nTableSize = (ht->nTableSize << 1);
			ht->nTableMask = ht->nTableSize - 1;
			zend_hash_rehash(ht);
			HANDLE_UNBLOCK_INTERRUPTIONS();
			return SUCCESS;
		}
		return FAILURE;
	}
	return SUCCESS;
}

ZEND_API int zend_hash_rehash(HashTable *ht)
{
	Bucket *p;
	uint nIndex;

	IS_CONSISTENT(ht);
	if (UNEXPECTED(ht->nNumOfElements == 0)) {
		return SUCCESS;
	}

	memset(ht->arBuckets, 0, ht->nTableSize * sizeof(Bucket *));
	p = ht->pListHead;
	while (p != NULL) {
		nIndex = p->h & ht->nTableMask;
		CONNECT_TO_BUCKET_DLLIST(p, ht->arBuckets[nIndex]);
		ht->arBuckets[nIndex] = p;
		p = p->pListNext;
	}
	return SUCCESS;
}

ZEND_API int zend_hash_del_key_or_index(HashTable *ht, const char *arKey, uint nKeyLength, ulong h, int flag)
{
	uint nIndex;
	Bucket *p;
#ifdef ZEND_SIGNALS
	TSRMLS_FETCH();
#endif

	IS_CONSISTENT(ht);

	if (flag == HASH_DEL_KEY) {
		h = zend_inline_hash_func(arKey, nKeyLength);
	}
	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if ((p->h == h) 
			 && (p->nKeyLength == nKeyLength)
			 && ((p->nKeyLength == 0) /* Numeric index (short circuits the memcmp() check) */
				 || !memcmp(p->arKey, arKey, nKeyLength))) { /* String index */
			HANDLE_BLOCK_INTERRUPTIONS();
			if (p == ht->arBuckets[nIndex]) {
				ht->arBuckets[nIndex] = p->pNext;
			} else {
				p->pLast->pNext = p->pNext;
			}
			if (p->pNext) {
				p->pNext->pLast = p->pLast;
			}
			if (p->pListLast != NULL) {
				p->pListLast->pListNext = p->pListNext;
			} else { 
				/* Deleting the head of the list */
				ht->pListHead = p->pListNext;
			}
			if (p->pListNext != NULL) {
				p->pListNext->pListLast = p->pListLast;
			} else {
				ht->pListTail = p->pListLast;
			}
			if (ht->pInternalPointer == p) {
				ht->pInternalPointer = p->pListNext;
			}
			ht->nNumOfElements--;
			if (ht->pDestructor) {
				ht->pDestructor(p->pData);
			}
			if (p->pData != &p->pDataPtr) {
				pefree(p->pData, ht->persistent);
			}
			pefree(p, ht->persistent);
			HANDLE_UNBLOCK_INTERRUPTIONS();
			return SUCCESS;
		}
		p = p->pNext;
	}
	return FAILURE;
}


ZEND_API void zend_hash_destroy(HashTable *ht)
{
	Bucket *p, *q;

	IS_CONSISTENT(ht);

	SET_INCONSISTENT(HT_IS_DESTROYING);

	p = ht->pListHead;
	while (p != NULL) {
		q = p;
		p = p->pListNext;
		if (ht->pDestructor) {
			ht->pDestructor(q->pData);
		}
		if (q->pData != &q->pDataPtr) {
			pefree(q->pData, ht->persistent);
		}
		pefree(q, ht->persistent);
	}
	if (ht->nTableMask) {
		pefree(ht->arBuckets, ht->persistent);
	}

	SET_INCONSISTENT(HT_DESTROYED);
}


ZEND_API void zend_hash_clean(HashTable *ht)
{
	Bucket *p, *q;

	IS_CONSISTENT(ht);

	p = ht->pListHead;

	if (ht->nTableMask) {
		memset(ht->arBuckets, 0, ht->nTableSize*sizeof(Bucket *));
	}
	ht->pListHead = NULL;
	ht->pListTail = NULL;
	ht->nNumOfElements = 0;
	ht->nNextFreeElement = 0;
	ht->pInternalPointer = NULL;

	while (p != NULL) {
		q = p;
		p = p->pListNext;
		if (ht->pDestructor) {
			ht->pDestructor(q->pData);
		}
		if (q->pData != &q->pDataPtr) {
			pefree(q->pData, ht->persistent);
		}
		pefree(q, ht->persistent);
	}
}

/* This function is used by the various apply() functions.
 * It deletes the passed bucket, and returns the address of the
 * next bucket.  The hash *may* be altered during that time, the
 * returned value will still be valid.
 */
static Bucket *zend_hash_apply_deleter(HashTable *ht, Bucket *p)
{
	Bucket *retval;
#ifdef ZEND_SIGNALS
	TSRMLS_FETCH();
#endif

	HANDLE_BLOCK_INTERRUPTIONS();

	// 调整该节点的左侧指向指针
	if (p->pLast) {
		p->pLast->pNext = p->pNext;
	} else {
		// 这里表名左侧是没有节点的，那么将头指针指向下一个节点即可
		uint nIndex;

		nIndex = p->h & ht->nTableMask;
		ht->arBuckets[nIndex] = p->pNext;
	}

	// 调整该节点的右侧指针
	if (p->pNext) {
		p->pNext->pLast = p->pLast;
	} else {
		/* Nothing to do as this list doesn't have a tail */
	}

	// 指向上一个桶链表的指针
	// 每一个节点都会这么设置 (???)
	if (p->pListLast != NULL) {
		p->pListLast->pListNext = p->pListNext;
	} else { 
		/* Deleting the head of the list */
		ht->pListHead = p->pListNext;
	}
	// 指向下一个桶链表的指针
	if (p->pListNext != NULL) {
		p->pListNext->pListLast = p->pListLast;
	} else {
		ht->pListTail = p->pListLast;
	}

	// 内部遍历的指针如果是当前指针，下移一个节点
	if (ht->pInternalPointer == p) {
		ht->pInternalPointer = p->pListNext;
	}

	// 节点个数减一
	ht->nNumOfElements--;
	HANDLE_UNBLOCK_INTERRUPTIONS();

	// 释放空间
	if (ht->pDestructor) {
		ht->pDestructor(p->pData);
	}
	if (p->pData != &p->pDataPtr) {
		pefree(p->pData, ht->persistent);
	}

	retval = p->pListNext;
	pefree(p, ht->persistent);

	return retval;
}


ZEND_API void zend_hash_graceful_destroy(HashTable *ht)
{
	Bucket *p;

	IS_CONSISTENT(ht);

	p = ht->pListHead;
	while (p != NULL) {
		p = zend_hash_apply_deleter(ht, p);
	}
	if (ht->nTableMask) {
		pefree(ht->arBuckets, ht->persistent);
	}

	SET_INCONSISTENT(HT_DESTROYED);
}

ZEND_API void zend_hash_graceful_reverse_destroy(HashTable *ht)
{
	Bucket *p;

	IS_CONSISTENT(ht);

	p = ht->pListTail;
	while (p != NULL) {
		zend_hash_apply_deleter(ht, p);
		p = ht->pListTail;
	}

	if (ht->nTableMask) {
		pefree(ht->arBuckets, ht->persistent);
	}

	SET_INCONSISTENT(HT_DESTROYED);
}

/* This is used to recurse elements and selectively delete certain entries 
 * from a hashtable. apply_func() receives the data and decides if the entry 
 * should be deleted or recursion should be stopped. The following three 
 * return codes are possible:
 * ZEND_HASH_APPLY_KEEP   - continue
 * ZEND_HASH_APPLY_STOP   - stop iteration
 * ZEND_HASH_APPLY_REMOVE - delete the element, combineable with the former
 */

ZEND_API void zend_hash_apply(HashTable *ht, apply_func_t apply_func TSRMLS_DC)
{
	Bucket *p;

	IS_CONSISTENT(ht);

	HASH_PROTECT_RECURSION(ht);
	p = ht->pListHead;
	while (p != NULL) {
		int result = apply_func(p->pData TSRMLS_CC);
		
		if (result & ZEND_HASH_APPLY_REMOVE) {
			p = zend_hash_apply_deleter(ht, p);
		} else {
			p = p->pListNext;
		}
		if (result & ZEND_HASH_APPLY_STOP) {
			break;
		}
	}
	HASH_UNPROTECT_RECURSION(ht);
}


ZEND_API void zend_hash_apply_with_argument(HashTable *ht, apply_func_arg_t apply_func, void *argument TSRMLS_DC)
{
	Bucket *p;

	IS_CONSISTENT(ht);

	HASH_PROTECT_RECURSION(ht);
	p = ht->pListHead;
	while (p != NULL) {
		int result = apply_func(p->pData, argument TSRMLS_CC);
		
		if (result & ZEND_HASH_APPLY_REMOVE) {
			p = zend_hash_apply_deleter(ht, p);
		} else {
			p = p->pListNext;
		}
		if (result & ZEND_HASH_APPLY_STOP) {
			break;
		}
	}
	HASH_UNPROTECT_RECURSION(ht);
}


ZEND_API void zend_hash_apply_with_arguments(HashTable *ht TSRMLS_DC, apply_func_args_t apply_func, int num_args, ...)
{
	Bucket *p;
	va_list args;
	zend_hash_key hash_key;

	IS_CONSISTENT(ht);

	HASH_PROTECT_RECURSION(ht);

	p = ht->pListHead;
	while (p != NULL) {
		int result;
		va_start(args, num_args);
		hash_key.arKey = p->arKey;
		hash_key.nKeyLength = p->nKeyLength;
		hash_key.h = p->h;
		result = apply_func(p->pData TSRMLS_CC, num_args, args, &hash_key);

		if (result & ZEND_HASH_APPLY_REMOVE) {
			p = zend_hash_apply_deleter(ht, p);
		} else {
			p = p->pListNext;
		}
		if (result & ZEND_HASH_APPLY_STOP) {
			va_end(args);
			break;
		}
		va_end(args);
	}

	HASH_UNPROTECT_RECURSION(ht);
}


ZEND_API void zend_hash_reverse_apply(HashTable *ht, apply_func_t apply_func TSRMLS_DC)
{
	Bucket *p, *q;

	IS_CONSISTENT(ht);

	HASH_PROTECT_RECURSION(ht);
	p = ht->pListTail;
	while (p != NULL) {
		int result = apply_func(p->pData TSRMLS_CC);

		q = p;
		p = p->pListLast;
		if (result & ZEND_HASH_APPLY_REMOVE) {
			zend_hash_apply_deleter(ht, q);
		}
		if (result & ZEND_HASH_APPLY_STOP) {
			break;
		}
	}
	HASH_UNPROTECT_RECURSION(ht);
}


ZEND_API void zend_hash_copy(HashTable *target, HashTable *source, copy_ctor_func_t pCopyConstructor, void *tmp, uint size)
{
	Bucket *p;
	void *new_entry;
	zend_bool setTargetPointer;

	IS_CONSISTENT(source);
	IS_CONSISTENT(target);

	setTargetPointer = !target->pInternalPointer;
	p = source->pListHead;
	while (p) {
		if (setTargetPointer && source->pInternalPointer == p) {
			target->pInternalPointer = NULL;
		}
		if (p->nKeyLength) {
			zend_hash_quick_update(target, p->arKey, p->nKeyLength, p->h, p->pData, size, &new_entry);
		} else {
			zend_hash_index_update(target, p->h, p->pData, size, &new_entry);
		}
		if (pCopyConstructor) {
			pCopyConstructor(new_entry);
		}
		p = p->pListNext;
	}
	if (!target->pInternalPointer) {
		target->pInternalPointer = target->pListHead;
	}
}


ZEND_API void _zend_hash_merge(HashTable *target, HashTable *source, copy_ctor_func_t pCopyConstructor, void *tmp, uint size, int overwrite ZEND_FILE_LINE_DC)
{
	Bucket *p;
	void *t;
	int mode = (overwrite?HASH_UPDATE:HASH_ADD);

	IS_CONSISTENT(source);
	IS_CONSISTENT(target);

	p = source->pListHead;
	while (p) {
		if (p->nKeyLength>0) {
			if (_zend_hash_quick_add_or_update(target, p->arKey, p->nKeyLength, p->h, p->pData, size, &t, mode ZEND_FILE_LINE_RELAY_CC)==SUCCESS && pCopyConstructor) {
				pCopyConstructor(t);
			}
		} else {
			if ((mode==HASH_UPDATE || !zend_hash_index_exists(target, p->h)) && zend_hash_index_update(target, p->h, p->pData, size, &t)==SUCCESS && pCopyConstructor) {
				pCopyConstructor(t);
			}
		}
		p = p->pListNext;
	}
	target->pInternalPointer = target->pListHead;
}


static zend_bool zend_hash_replace_checker_wrapper(HashTable *target, void *source_data, Bucket *p, void *pParam, merge_checker_func_t merge_checker_func)
{
	zend_hash_key hash_key;

	hash_key.arKey = p->arKey;
	hash_key.nKeyLength = p->nKeyLength;
	hash_key.h = p->h;
	return merge_checker_func(target, source_data, &hash_key, pParam);
}


ZEND_API void zend_hash_merge_ex(HashTable *target, HashTable *source, copy_ctor_func_t pCopyConstructor, uint size, merge_checker_func_t pMergeSource, void *pParam)
{
	Bucket *p;
	void *t;

	IS_CONSISTENT(source);
	IS_CONSISTENT(target);

	p = source->pListHead;
	while (p) {
		if (zend_hash_replace_checker_wrapper(target, p->pData, p, pParam, pMergeSource)) {
			if (zend_hash_quick_update(target, p->arKey, p->nKeyLength, p->h, p->pData, size, &t)==SUCCESS && pCopyConstructor) {
				pCopyConstructor(t);
			}
		}
		p = p->pListNext;
	}
	target->pInternalPointer = target->pListHead;
}


ZEND_API ulong zend_get_hash_value(const char *arKey, uint nKeyLength)
{
	return zend_inline_hash_func(arKey, nKeyLength);
}


/* Returns SUCCESS if found and FAILURE if not. The pointer to the
 * data is returned in pData. The reason is that there's no reason
 * someone using the hash table might not want to have NULL data
 */
ZEND_API int zend_hash_find(const HashTable *ht, const char *arKey, uint nKeyLength, void **pData)
{
	ulong h;
	uint nIndex;
	Bucket *p;

	IS_CONSISTENT(ht);

	h = zend_inline_hash_func(arKey, nKeyLength);
	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if (p->arKey == arKey ||
			((p->h == h) && (p->nKeyLength == nKeyLength) && !memcmp(p->arKey, arKey, nKeyLength))) {
				*pData = p->pData;
				return SUCCESS;
		}
		p = p->pNext;
	}
	return FAILURE;
}


ZEND_API int zend_hash_quick_find(const HashTable *ht, const char *arKey, uint nKeyLength, ulong h, void **pData)
{
	uint nIndex;
	Bucket *p;

	if (nKeyLength==0) {
		return zend_hash_index_find(ht, h, pData);
	}

	IS_CONSISTENT(ht);

	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if (p->arKey == arKey ||
			((p->h == h) && (p->nKeyLength == nKeyLength) && !memcmp(p->arKey, arKey, nKeyLength))) {
				*pData = p->pData;
				return SUCCESS;
		}
		p = p->pNext;
	}
	return FAILURE;
}


ZEND_API int zend_hash_exists(const HashTable *ht, const char *arKey, uint nKeyLength)
{
	ulong h;
	uint nIndex;
	Bucket *p;

	IS_CONSISTENT(ht);

	h = zend_inline_hash_func(arKey, nKeyLength);
	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if (p->arKey == arKey ||
			((p->h == h) && (p->nKeyLength == nKeyLength) && !memcmp(p->arKey, arKey, nKeyLength))) {
				return 1;
		}
		p = p->pNext;
	}
	return 0;
}


ZEND_API int zend_hash_quick_exists(const HashTable *ht, const char *arKey, uint nKeyLength, ulong h)
{
	uint nIndex;
	Bucket *p;

	if (nKeyLength==0) {
		return zend_hash_index_exists(ht, h);
	}

	IS_CONSISTENT(ht);

	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if (p->arKey == arKey ||
			((p->h == h) && (p->nKeyLength == nKeyLength) && !memcmp(p->arKey, arKey, nKeyLength))) {
				return 1;
		}
		p = p->pNext;
	}
	return 0;

}


ZEND_API int zend_hash_index_find(const HashTable *ht, ulong h, void **pData)
{
	uint nIndex;
	Bucket *p;

	IS_CONSISTENT(ht);

	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if ((p->h == h) && (p->nKeyLength == 0)) {
			*pData = p->pData;
			return SUCCESS;
		}
		p = p->pNext;
	}
	return FAILURE;
}


ZEND_API int zend_hash_index_exists(const HashTable *ht, ulong h)
{
	uint nIndex;
	Bucket *p;

	IS_CONSISTENT(ht);

	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if ((p->h == h) && (p->nKeyLength == 0)) {
			return 1;
		}
		p = p->pNext;
	}
	return 0;
}


ZEND_API int zend_hash_num_elements(const HashTable *ht)
{
	IS_CONSISTENT(ht);

	return ht->nNumOfElements;
}


ZEND_API int zend_hash_get_pointer(const HashTable *ht, HashPointer *ptr)
{
	ptr->pos = ht->pInternalPointer;
	if (ht->pInternalPointer) {
		ptr->h = ht->pInternalPointer->h;
		return 1;
	} else {
		ptr->h = 0;
		return 0;
	}
}

ZEND_API int zend_hash_set_pointer(HashTable *ht, const HashPointer *ptr)
{
	if (ptr->pos == NULL) {
		ht->pInternalPointer = NULL;
	} else if (ht->pInternalPointer != ptr->pos) {
		Bucket *p;

		IS_CONSISTENT(ht);
		p = ht->arBuckets[ptr->h & ht->nTableMask];
		while (p != NULL) {
			if (p == ptr->pos) {
				ht->pInternalPointer = p;
				return 1;
			}
			p = p->pNext;
		}
		return 0;
	}
	return 1;
}

ZEND_API void zend_hash_internal_pointer_reset_ex(HashTable *ht, HashPosition *pos)
{
	IS_CONSISTENT(ht);

	if (pos)
		*pos = ht->pListHead;
	else
		ht->pInternalPointer = ht->pListHead;
}


/* This function will be extremely optimized by remembering 
 * the end of the list
 */
ZEND_API void zend_hash_internal_pointer_end_ex(HashTable *ht, HashPosition *pos)
{
	IS_CONSISTENT(ht);

	if (pos)
		*pos = ht->pListTail;
	else
		ht->pInternalPointer = ht->pListTail;
}


ZEND_API int zend_hash_move_forward_ex(HashTable *ht, HashPosition *pos)
{
	HashPosition *current = pos ? pos : &ht->pInternalPointer;

	IS_CONSISTENT(ht);

	if (*current) {
		*current = (*current)->pListNext;
		return SUCCESS;
	} else
		return FAILURE;
}

ZEND_API int zend_hash_move_backwards_ex(HashTable *ht, HashPosition *pos)
{
	HashPosition *current = pos ? pos : &ht->pInternalPointer;

	IS_CONSISTENT(ht);

	if (*current) {
		*current = (*current)->pListLast;
		return SUCCESS;
	} else
		return FAILURE;
}


/* This function should be made binary safe  */
ZEND_API int zend_hash_get_current_key_ex(const HashTable *ht, char **str_index, uint *str_length, ulong *num_index, zend_bool duplicate, HashPosition *pos)
{
	Bucket *p;

	p = pos ? (*pos) : ht->pInternalPointer;

	IS_CONSISTENT(ht);

	if (p) {
		if (p->nKeyLength) {
			if (duplicate) {
				*str_index = estrndup(p->arKey, p->nKeyLength - 1);
			} else {
				*str_index = (char*)p->arKey;
			}
			if (str_length) {
				*str_length = p->nKeyLength;
			}
			return HASH_KEY_IS_STRING;
		} else {
			*num_index = p->h;
			return HASH_KEY_IS_LONG;
		}
	}
	return HASH_KEY_NON_EXISTENT;
}

ZEND_API void zend_hash_get_current_key_zval_ex(const HashTable *ht, zval *key, HashPosition *pos) {
	Bucket *p;

	IS_CONSISTENT(ht);

	p = pos ? (*pos) : ht->pInternalPointer;

	if (!p) {
		Z_TYPE_P(key) = IS_NULL;
	} else if (p->nKeyLength) {
		Z_TYPE_P(key) = IS_STRING;
		Z_STRVAL_P(key) = estrndup(p->arKey, p->nKeyLength - 1);
		Z_STRLEN_P(key) = p->nKeyLength - 1;
	} else {
		Z_TYPE_P(key) = IS_LONG;
		Z_LVAL_P(key) = p->h;
	}
}

ZEND_API int zend_hash_get_current_key_type_ex(HashTable *ht, HashPosition *pos)
{
	Bucket *p;

	p = pos ? (*pos) : ht->pInternalPointer;

	IS_CONSISTENT(ht);

	if (p) {
		if (p->nKeyLength) {
			return HASH_KEY_IS_STRING;
		} else {
			return HASH_KEY_IS_LONG;
		}
	}
	return HASH_KEY_NON_EXISTENT;
}


ZEND_API int zend_hash_get_current_data_ex(HashTable *ht, void **pData, HashPosition *pos)
{
	Bucket *p;

	p = pos ? (*pos) : ht->pInternalPointer;

	IS_CONSISTENT(ht);

	if (p) {
		*pData = p->pData;
		return SUCCESS;
	} else {
		return FAILURE;
	}
}

/* This function changes key of current element without changing elements'
 * order. If element with target key already exists, it will be deleted first.
 */
ZEND_API int zend_hash_update_current_key_ex(HashTable *ht, int key_type, const char *str_index, uint str_length, ulong num_index, int mode, HashPosition *pos)
{
	Bucket *p, *q;
	ulong h;
#ifdef ZEND_SIGNALS
	TSRMLS_FETCH();
#endif

	p = pos ? (*pos) : ht->pInternalPointer;

	IS_CONSISTENT(ht);

	if (p) {
		if (key_type == HASH_KEY_IS_LONG) {
			str_length = 0;
			if (!p->nKeyLength && p->h == num_index) {
				return SUCCESS;
			}

			q = ht->arBuckets[num_index & ht->nTableMask];
			while (q != NULL) {
				if (!q->nKeyLength && q->h == num_index) {
					break;
				}
				q = q->pNext;
			}
		} else if (key_type == HASH_KEY_IS_STRING) {
			if (IS_INTERNED(str_index)) {
				h = INTERNED_HASH(str_index);
			} else {
				h = zend_inline_hash_func(str_index, str_length);
			}

			if (p->arKey == str_index ||
			    (p->nKeyLength == str_length &&
			     p->h == h &&
			     memcmp(p->arKey, str_index, str_length) == 0)) {
				return SUCCESS;
			}

			q = ht->arBuckets[h & ht->nTableMask];

			while (q != NULL) {
				if (q->arKey == str_index ||
				    (q->h == h && q->nKeyLength == str_length &&
				     memcmp(q->arKey, str_index, str_length) == 0)) {
					break;
				}
				q = q->pNext;
			}
		} else {
			return FAILURE;
		}

		HANDLE_BLOCK_INTERRUPTIONS();

		if (q) {
			if (mode != HASH_UPDATE_KEY_ANYWAY) {
				Bucket *r = p->pListLast;
				int found = HASH_UPDATE_KEY_IF_BEFORE;

				while (r) {
					if (r == q) {
						found = HASH_UPDATE_KEY_IF_AFTER;
						break;
					}
					r = r->pListLast;
				}
				if (mode & found) {
					/* delete current bucket */
					if (p == ht->arBuckets[p->h & ht->nTableMask]) {
						ht->arBuckets[p->h & ht->nTableMask] = p->pNext;
					} else {
						p->pLast->pNext = p->pNext;
					}
					if (p->pNext) {
						p->pNext->pLast = p->pLast;
					}
					if (p->pListLast != NULL) {
						p->pListLast->pListNext = p->pListNext;
					} else {
						/* Deleting the head of the list */
						ht->pListHead = p->pListNext;
					}
					if (p->pListNext != NULL) {
						p->pListNext->pListLast = p->pListLast;
					} else {
						ht->pListTail = p->pListLast;
					}
					if (ht->pInternalPointer == p) {
						ht->pInternalPointer = p->pListNext;
					}
					ht->nNumOfElements--;
					if (ht->pDestructor) {
						ht->pDestructor(p->pData);
					}
					if (p->pData != &p->pDataPtr) {
						pefree(p->pData, ht->persistent);
					}
					pefree(p, ht->persistent);
					HANDLE_UNBLOCK_INTERRUPTIONS();
					return FAILURE;
				}
			}
			/* delete another bucket with the same key */
			if (q == ht->arBuckets[q->h & ht->nTableMask]) {
				ht->arBuckets[q->h & ht->nTableMask] = q->pNext;
			} else {
				q->pLast->pNext = q->pNext;
			}
			if (q->pNext) {
				q->pNext->pLast = q->pLast;
			}
			if (q->pListLast != NULL) {
				q->pListLast->pListNext = q->pListNext;
			} else {
				/* Deleting the head of the list */
				ht->pListHead = q->pListNext;
			}
			if (q->pListNext != NULL) {
				q->pListNext->pListLast = q->pListLast;
			} else {
				ht->pListTail = q->pListLast;
			}
			if (ht->pInternalPointer == q) {
				ht->pInternalPointer = q->pListNext;
			}
			ht->nNumOfElements--;
			if (ht->pDestructor) {
				ht->pDestructor(q->pData);
			}
			if (q->pData != &q->pDataPtr) {
				pefree(q->pData, ht->persistent);
			}
			pefree(q, ht->persistent);
		}

		if (p->pNext) {
			p->pNext->pLast = p->pLast;
		}
		if (p->pLast) {
			p->pLast->pNext = p->pNext;
		} else {
			ht->arBuckets[p->h & ht->nTableMask] = p->pNext;
		}

		if ((IS_INTERNED(p->arKey) != IS_INTERNED(str_index)) ||
		    (!IS_INTERNED(p->arKey) && p->nKeyLength != str_length)) {
			Bucket *q;

			if (IS_INTERNED(str_index)) {
				q = (Bucket *) pemalloc(sizeof(Bucket), ht->persistent);
			} else {
				q = (Bucket *) pemalloc(sizeof(Bucket) + str_length, ht->persistent);
			}

			q->nKeyLength = str_length;
			if (p->pData == &p->pDataPtr) {
				q->pData = &q->pDataPtr;
			} else {
				q->pData = p->pData;
			}
			q->pDataPtr = p->pDataPtr;
			q->pListNext = p->pListNext;
			q->pListLast = p->pListLast;
			if (q->pListNext) {
				p->pListNext->pListLast = q;
			} else {
				ht->pListTail = q;
			}
			if (q->pListLast) {
				p->pListLast->pListNext = q;
			} else {
				ht->pListHead = q;
			}
			if (ht->pInternalPointer == p) {
				ht->pInternalPointer = q;
			}
			if (pos) {
				*pos = q;
			}
			pefree(p, ht->persistent);
			p = q;
		}

		if (key_type == HASH_KEY_IS_LONG) {
			p->h = num_index;
		} else {
			p->h = h;
			p->nKeyLength = str_length;
			if (IS_INTERNED(str_index)) {
				p->arKey = str_index;
			} else {
				p->arKey = (const char*)(p+1);
				memcpy((char*)p->arKey, str_index, str_length);
			}
		}

		CONNECT_TO_BUCKET_DLLIST(p, ht->arBuckets[p->h & ht->nTableMask]);
		ht->arBuckets[p->h & ht->nTableMask] = p;
		HANDLE_UNBLOCK_INTERRUPTIONS();

		return SUCCESS;
	} else {
		return FAILURE;
	}
}

ZEND_API int zend_hash_sort(HashTable *ht, sort_func_t sort_func,
							compare_func_t compar, int renumber TSRMLS_DC)
{
	Bucket **arTmp;
	Bucket *p;
	int i, j;

	IS_CONSISTENT(ht);

	if (!(ht->nNumOfElements>1) && !(renumber && ht->nNumOfElements>0)) { /* Doesn't require sorting */
		return SUCCESS;
	}
	arTmp = (Bucket **) pemalloc(ht->nNumOfElements * sizeof(Bucket *), ht->persistent);
	if (!arTmp) {
		return FAILURE;
	}
	p = ht->pListHead;
	i = 0;
	while (p) {
		arTmp[i] = p;
		p = p->pListNext;
		i++;
	}

	(*sort_func)((void *) arTmp, i, sizeof(Bucket *), compar TSRMLS_CC);

	HANDLE_BLOCK_INTERRUPTIONS();
	ht->pListHead = arTmp[0];
	ht->pListTail = NULL;
	ht->pInternalPointer = ht->pListHead;

	arTmp[0]->pListLast = NULL;
	if (i > 1) {
		arTmp[0]->pListNext = arTmp[1];
		for (j = 1; j < i-1; j++) {
			arTmp[j]->pListLast = arTmp[j-1];
			arTmp[j]->pListNext = arTmp[j+1];
		}
		arTmp[j]->pListLast = arTmp[j-1];
		arTmp[j]->pListNext = NULL;
	} else {
		arTmp[0]->pListNext = NULL;
	}
	ht->pListTail = arTmp[i-1];

	pefree(arTmp, ht->persistent);
	HANDLE_UNBLOCK_INTERRUPTIONS();

	if (renumber) {
		p = ht->pListHead;
		i=0;
		while (p != NULL) {
			p->nKeyLength = 0;
			p->h = i++;
			p = p->pListNext;
		}
		ht->nNextFreeElement = i;
		zend_hash_rehash(ht);
	}
	return SUCCESS;
}


ZEND_API int zend_hash_compare(HashTable *ht1, HashTable *ht2, compare_func_t compar, zend_bool ordered TSRMLS_DC)
{
	Bucket *p1, *p2 = NULL;
	int result;
	void *pData2;

	IS_CONSISTENT(ht1);
	IS_CONSISTENT(ht2);

	HASH_PROTECT_RECURSION(ht1); 
	HASH_PROTECT_RECURSION(ht2); 

	result = ht1->nNumOfElements - ht2->nNumOfElements;
	if (result!=0) {
		HASH_UNPROTECT_RECURSION(ht1); 
		HASH_UNPROTECT_RECURSION(ht2); 
		return result;
	}

	p1 = ht1->pListHead;
	if (ordered) {
		p2 = ht2->pListHead;
	}

	while (p1) {
		if (ordered && !p2) {
			HASH_UNPROTECT_RECURSION(ht1); 
			HASH_UNPROTECT_RECURSION(ht2); 
			return 1; /* That's not supposed to happen */
		}
		if (ordered) {
			if (p1->nKeyLength==0 && p2->nKeyLength==0) { /* numeric indices */
				result = p1->h - p2->h;
				if (result!=0) {
					HASH_UNPROTECT_RECURSION(ht1); 
					HASH_UNPROTECT_RECURSION(ht2); 
					return result;
				}
			} else { /* string indices */
				result = p1->nKeyLength - p2->nKeyLength;
				if (result!=0) {
					HASH_UNPROTECT_RECURSION(ht1); 
					HASH_UNPROTECT_RECURSION(ht2); 
					return result;
				}
				result = memcmp(p1->arKey, p2->arKey, p1->nKeyLength);
				if (result!=0) {
					HASH_UNPROTECT_RECURSION(ht1); 
					HASH_UNPROTECT_RECURSION(ht2); 
					return result;
				}
			}
			pData2 = p2->pData;
		} else {
			if (p1->nKeyLength==0) { /* numeric index */
				if (zend_hash_index_find(ht2, p1->h, &pData2)==FAILURE) {
					HASH_UNPROTECT_RECURSION(ht1); 
					HASH_UNPROTECT_RECURSION(ht2); 
					return 1;
				}
			} else { /* string index */
				if (zend_hash_quick_find(ht2, p1->arKey, p1->nKeyLength, p1->h, &pData2)==FAILURE) {
					HASH_UNPROTECT_RECURSION(ht1); 
					HASH_UNPROTECT_RECURSION(ht2); 
					return 1;
				}
			}
		}
		result = compar(p1->pData, pData2 TSRMLS_CC);
		if (result!=0) {
			HASH_UNPROTECT_RECURSION(ht1); 
			HASH_UNPROTECT_RECURSION(ht2); 
			return result;
		}
		p1 = p1->pListNext;
		if (ordered) {
			p2 = p2->pListNext;
		}
	}
	
	HASH_UNPROTECT_RECURSION(ht1); 
	HASH_UNPROTECT_RECURSION(ht2); 
	return 0;
}


ZEND_API int zend_hash_minmax(const HashTable *ht, compare_func_t compar, int flag, void **pData TSRMLS_DC)
{
	Bucket *p, *res;

	IS_CONSISTENT(ht);

	if (ht->nNumOfElements == 0 ) {
		*pData=NULL;
		return FAILURE;
	}

	res = p = ht->pListHead;
	while ((p = p->pListNext)) {
		if (flag) {
			if (compar(&res, &p TSRMLS_CC) < 0) { /* max */
				res = p;
			}
		} else {
			if (compar(&res, &p TSRMLS_CC) > 0) { /* min */
				res = p;
			}
		}
	}
	*pData = res->pData;
	return SUCCESS;
}

ZEND_API ulong zend_hash_next_free_element(const HashTable *ht)
{
	IS_CONSISTENT(ht);

	return ht->nNextFreeElement;

}


#if ZEND_DEBUG
void zend_hash_display_pListTail(const HashTable *ht)
{
	Bucket *p;

	p = ht->pListTail;
	while (p != NULL) {
		zend_output_debug_string(0, "pListTail has key %s\n", p->arKey);
		p = p->pListLast;
	}
}

void zend_hash_display(const HashTable *ht)
{
	Bucket *p;
	uint i;

	if (UNEXPECTED(ht->nNumOfElements == 0)) {
		zend_output_debug_string(0, "The hash is empty");
		return;
	}
	for (i = 0; i < ht->nTableSize; i++) {
		p = ht->arBuckets[i];
		while (p != NULL) {
			zend_output_debug_string(0, "%s <==> 0x%lX\n", p->arKey, p->h);
			p = p->pNext;
		}
	}

	p = ht->pListTail;
	while (p != NULL) {
		zend_output_debug_string(0, "%s <==> 0x%lX\n", p->arKey, p->h);
		p = p->pListLast;
	}
}
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
