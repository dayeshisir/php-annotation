/*************************************************************************
	> File Name: hash.h
	> Author: 
	> Mail: 
	> Created Time: Sat 28 Nov 2015 02:07:12 PM CST
 ************************************************************************/

#ifndef _HASH_H
#define _HASH_H

#include<stdio.h>

#define uint unsigned int
#define ulong unsigned long
#define zend_bool bool

#define SUCCESS           0
#define FAILURE           1

#define HASH_UPDATE       0
#define HASH_ADD          1
#define HASH_NEXT_INSERT  2
#define LONG_MAX          2147483647L

typedef struct _simple_bucket {
    ulong h;
    uint nKeyLength;
    void * pData;
    void * pDataPtr;
    struct _simple_bucket *pListNext;
    struct _simple_bucket *pListLast;
    struct _simple_bucket *pNext;
    struct _simple_bucket *pLast;
    const char* arKey;
}Bucket;

typedef struct _hashtable {
    uint nTableSize;
    uint nTableMask;
    uint nNumOfElements;
    ulong nNextFreeElement;
    Bucket *pInternalPointer;
    Bucket *pListHead;
    Bucket *pListTail;
    Bucket **arBuckets;
    unsigned char nApplyCount;
    int bApplyProtection;
    //zend_bool bApplyProtection;
    uint inconsistent;
}HashTable;

/*
typedef struct _hashtable {
        uint nTableSize;                  // hash Bucket的大小，最小为8，以2x增长
        uint nTableMask;                  // nTableSize-1 ， 索引取值的优化
        uint nNumOfElements;              // hash Bucket中当前存在的元素个数，count()函数会直接返回此值 
        ulong nNextFreeElement;           // 下一个数字索引的位置
        Bucket *pInternalPointer;      // 当前遍历的指针（foreach比for快的原因之一）
        Bucket *pListHead;                // 存储数组头元素指针
        Bucket *pListTail;                // 存储数组尾元素指针
        Bucket **arBuckets;               // 存储hash数组
        //dtor_func_t pDestructor;          // 在删除元素时执行的回调函数，用于资源的释放
        bool persistent;             // 指出了Bucket内存分配的方式。如果persisient为TRUE，则使用操作系统本身的内存分配函数为Bucket分配内存，否则使用PHP的内存分配函数。
        unsigned char nApplyCount;        // 标记当前hash Bucket被递归访问的次数（防止多次递归）
        bool bApplyProtection;       // 标记当前hash桶允许不允许多次访问，不允许时，最多只能递归3次
        uint inconsistent;                 // 判断hash是否一致
} HashTable;
*/

#endif
