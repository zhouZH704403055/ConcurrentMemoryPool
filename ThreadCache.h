#pragma once

#include"Common.h"

class ThreadCache
{
public:
	void* Allocate(size_t size);//分配内存
	void Deallocate(void* ptr,size_t size);//解除分配，释放内存
	void* FetchFromCentralCache(size_t index, size_t size);//从central cache中获取内存对象
	void ListTooLong(FreeList* memory_list, size_t memory_size);//当内存对象链表太长时，回收到central cache中
private:
	//自由链表（指针数组，数组的每个元素指向一个链表，链表中储存空闲可用的内存对象，不同元素指向的链表中储存的内存对象大小不同）
	FreeList _freelist[NLISTS];//管理内存
};

//定义一个静态指针变量，但是这个变量每个线程都独有一份，每次创建一个新线程时，copy一份给新线程，就不存在竞争问题
static _declspec (thread) ThreadCache* tls_thread_cache = nullptr;
