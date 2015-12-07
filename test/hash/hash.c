/*************************************************************************
	> File Name: hash.c
	> Author: 
	> Mail: 
	> Created Time: Sat 28 Nov 2015 09:27:14 AM CST
 ************************************************************************/

#include "hash.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#define uint unsigned int
#define ulong unsigned long

//检测是否分配空间 没有分配的话 分配必要的空间
#define CHECK_INIT(ht) do {                                                          \
    if (((ht)->nTableMask == 0)) {                                                   \
            (ht)->arBuckets = (Bucket **) malloc((ht)->nTableSize * sizeof(Bucket *)); \
            (ht)->nTableMask = (ht)->nTableSize - 1;                                 \
        }                                                                            \
} while (0)

// 更新一个节点的数值 
#define UPDATE_DATA(ht, p, pData, nDataSize)											\
	if (nDataSize == sizeof(void*)) {													\
		if ((p)->pData != &(p)->pDataPtr) {												\
			free((p)->pData);									                        \
		}																				\
		memcpy(&(p)->pDataPtr, pData, sizeof(void *));									\
		(p)->pData = &(p)->pDataPtr;													\
	} else {																			\
		if ((p)->pData == &(p)->pDataPtr) {												\
			(p)->pData = (void *) malloc(nDataSize);			                        \
			(p)->pDataPtr=NULL;															\
		} else {																		\
			(p)->pData = (void *) realloc((p)->pData, nDataSize);	\
			/* (p)->pDataPtr is already NULL so no need to initialize it */				\
		}																				\
		memcpy((p)->pData, pData, nDataSize);											\
	}

#define INIT_DATA(ht, p, _pData, nDataSize);								\
	if (nDataSize == sizeof(void*)) {									\
		memcpy(&(p)->pDataPtr, (_pData), sizeof(void *));					\
		(p)->pData = &(p)->pDataPtr;									\
	} else {															\
		(p)->pData = (void *) malloc(nDataSize);\
		memcpy((p)->pData, (_pData), nDataSize);							\
		(p)->pDataPtr=NULL;												\
	}
#define CONNECT_TO_BUCKET_DLLIST(element, list_head)		\
	(element)->pNext = (list_head);							\
	(element)->pLast = NULL;								\
	if ((element)->pNext) {									\
		(element)->pNext->pLast = (element);				\
	}

#define CONNECT_TO_GLOBAL_DLLIST_EX(element, ht, last, next)\
	(element)->pListLast = (last);							\
	(element)->pListNext = (next);							\
	if ((last) != NULL) {									\
		(last)->pListNext = (element);						\
	} else {												\
		(ht)->pListHead = (element);						\
	}														\
	if ((next) != NULL) {									\
		(next)->pListLast = (element);						\
	} else {												\
		(ht)->pListTail = (element);						\
	}														\

#define CONNECT_TO_GLOBAL_DLLIST(element, ht)									\
	CONNECT_TO_GLOBAL_DLLIST_EX(element, ht, (ht)->pListTail, (Bucket *) NULL);	\
	if ((ht)->pInternalPointer == NULL) {										\
		(ht)->pInternalPointer = (element);										\
	}

// 空的桶都收敛到这里
static const Bucket *uninitialized_bucket = NULL;

int zend_hash_init(HashTable *ht, uint nSize)
{
	int i = 3;

	if (nSize >= 0x80000000) {
		ht->nTableSize = 0x80000000;
	} else {
		while ((1U << i) < nSize) {
			i++;
		}
		ht->nTableSize = 1 << i;
	}

	ht->nTableMask = 0;	/* 0 means that ht->arBuckets is uninitialized */
	ht->arBuckets = (Bucket**)&uninitialized_bucket;
	ht->pListHead = NULL;
	ht->pListTail = NULL;
	ht->nNumOfElements = 0;
	ht->nNextFreeElement = 0;
	ht->pInternalPointer = NULL;
	//ht->persistent = persistent;
	ht->nApplyCount = 0;
	//ht->bApplyProtection = 1;
	return SUCCESS;
}

int zend_bucket_show(Bucket* bucket) {

    printf("h: %d\n", bucket->h);
        
    if (bucket->arKey) {
        printf("key : %s\n", bucket->arKey);
    }
    if (bucket->pData) {
        printf("value :  %s\n", bucket->pData);
    }
    if (bucket->pDataPtr) {
        printf("value ptr:  %s\n", bucket->pDataPtr);
    }
}

int zend_hash_show(HashTable *ht) {
    int i = 0;

    printf("Table Mask : %d\n", ht->nTableMask);
    printf("Table Size : %d\n", ht->nTableSize);
    printf("Table Element Num : %d\n", ht->nNumOfElements);
    printf("Table Next Free Element : %d\n", ht->nNextFreeElement);

    Bucket* pCur = ht->pListHead;
    while(pCur) {
        zend_bucket_show(pCur);
        pCur = pCur->pListNext;
    }

    printf("*********************************************\n");

    for(i = 0; i < ht->nTableSize; i++) {
        pCur = ht->arBuckets[i];
        if (pCur) {
            printf("bucket num: %d\n", i);
        }
        while(pCur) {
            zend_bucket_show(pCur);
            pCur = pCur->pNext;
        }
    }
}

#define MOD  5

int zend_inline_hash_func(const char* arKey, const int len) {
    int hash = -1;
    int i    = 0;

    // 空的 怎么返回
    if (NULL == arKey) {
        return hash;
    }

    // 长度不为0，索引是字符串 
    if (len) {
        for(i = 0; i < len; i++) {
            hash += arKey[i];
        }
    } else {
        hash = atoi(arKey);
    }

    hash = hash % MOD; 

    return hash;
}

int zend_hash_add_or_update(HashTable *ht, const char *arKey, uint nKeyLength, void *pData, uint nDataSize, void **pDest, int flag)
{
	ulong h;
	uint nIndex;
	Bucket *p;

    CHECK_INIT(ht);

	h = zend_inline_hash_func(arKey, nKeyLength);
	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if (p->arKey == arKey ||
			((p->h == h) && (p->nKeyLength == nKeyLength) && !memcmp(p->arKey, arKey, nKeyLength))) {
				if (flag & HASH_ADD) {
					return FAILURE;
				}
				if (p->pData) {
                    free(p->pData);
				}
				UPDATE_DATA(ht, p, pData, nDataSize);
				if (pDest) {
					*pDest = p->pData;
				}
				return SUCCESS;
		}
		p = p->pNext;
	}
	
	//if (IS_INTERNED(arKey)) {
	if (0 == nKeyLength) {
		p = (Bucket *) malloc(sizeof(Bucket));
		p->arKey = arKey;
	} else {
		p = (Bucket *) malloc(sizeof(Bucket) + nKeyLength);
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

	CONNECT_TO_GLOBAL_DLLIST(p, ht);
	ht->arBuckets[nIndex] = p;

	ht->nNumOfElements++;
	//ZEND_HASH_IF_FULL_DO_RESIZE(ht);		/* If the Hash table is full, resize it */
	return SUCCESS;
}

int _zend_hash_index_update_or_next_insert(HashTable *ht, ulong h, void *pData, uint nDataSize, void **pDest, int flag)
{
	uint nIndex;
	Bucket *p;

	CHECK_INIT(ht);

	if (flag & HASH_NEXT_INSERT) {
		h = ht->nNextFreeElement;
	}
	nIndex = h & ht->nTableMask;

	p = ht->arBuckets[nIndex];
	while (p != NULL) {
		if ((p->nKeyLength == 0) && (p->h == h)) {
			if (flag & HASH_NEXT_INSERT || flag & HASH_ADD) {
				return FAILURE;
			}

			if (p->pData) {
                free(p->pData);
			}
			UPDATE_DATA(ht, p, pData, nDataSize);

			if (pDest) {
				*pDest = p->pData;
			}
			return SUCCESS;
		}
		p = p->pNext;
	}

	p = (Bucket *) malloc(sizeof(Bucket));
	p->arKey = NULL;
	p->nKeyLength = 0; /* Numeric indices are marked by making the nKeyLength == 0 */
	p->h = h;
	INIT_DATA(ht, p, pData, nDataSize);
	if (pDest) {
		*pDest = p->pData;
	}

	CONNECT_TO_BUCKET_DLLIST(p, ht->arBuckets[nIndex]);

	ht->arBuckets[nIndex] = p;
	CONNECT_TO_GLOBAL_DLLIST(p, ht);

	if ((long)h >= (long)ht->nNextFreeElement) {
		ht->nNextFreeElement = h < LONG_MAX ? h + 1 : LONG_MAX;
	}
	ht->nNumOfElements++;
	//ZEND_HASH_IF_FULL_DO_RESIZE(ht);
	return SUCCESS;
}

#define zend_hash_index_update(ht, h, pData, nDataSize, pDest) \
                _zend_hash_index_update_or_next_insert(ht, h, pData, nDataSize, pDest, HASH_UPDATE)
#define zend_hash_next_index_insert(ht, pData, nDataSize, pDest) \
                _zend_hash_index_update_or_next_insert(ht, 0, pData, nDataSize, pDest, HASH_NEXT_INSERT)

void stringHashTest() {
    HashTable *ht = (HashTable* )malloc(sizeof(HashTable));
    int nSize = 10;
    zend_hash_init(ht, nSize);

    zend_hash_show(ht);

    char *pKey = "hello";
    char *pValue = "world";

    char* pDest = (char*) malloc(sizeof(char*));

    zend_hash_add_or_update(ht, pKey, strlen(pKey), pValue, strlen(pValue), &pDest, HASH_ADD);

    pKey = "ellho";
    pValue = "sb";

    zend_hash_add_or_update(ht, pKey, strlen(pKey), pValue, strlen(pValue), &pDest, HASH_ADD);

    pKey = "lelho";
    pValue = "sb11111";
    zend_hash_add_or_update(ht, pKey, strlen(pKey), pValue, strlen(pValue), &pDest, HASH_ADD);

    pKey = "yaokun";
    pValue = "xiaobao";
    zend_hash_add_or_update(ht, pKey, strlen(pKey), pValue, strlen(pValue), &pDest, HASH_ADD);
    zend_hash_show(ht);
}

void intHashTest() {
   HashTable *ht = (HashTable* )malloc(sizeof(HashTable));
    int nSize = 3;
    int i;
    zend_hash_init(ht, nSize);

    zend_hash_show(ht);

    char* pValue = "jack";
    char* pDest  = (char*)malloc(sizeof(char*));
    for(i = 0; i < 10; i++) {
        zend_hash_next_index_insert(ht, pValue, strlen(pValue), &pDest);
    }
    i = 18;
    pValue = "yaokun";
    zend_hash_index_update(ht, i, pValue, strlen(pValue), &pDest);

    i = 16;
    pValue = "shichao";
    zend_hash_index_update(ht, i, pValue, strlen(pValue), &pDest);
    /*zend_hash_next_index_insert(ht, pValue, strlen(pValue), &pDest);

    pKey = "25";
    pValue = "huangling";
    zend_hash_next_index_insert(ht, pValue, strlen(pValue), &pDest);
    */

    zend_hash_show(ht);
}


int main(int argc, char** argv) {
    //stringHashTest();
    intHashTest();
}
