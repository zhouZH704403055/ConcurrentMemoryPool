#include"ThreadCache.h"
#include"CentralCache.h"

//分配内存
void* ThreadCache::Allocate(size_t memory_size)
{
	assert(memory_size <= MAX_SIZE);
	//计算对齐取整后的内存对象大小
	memory_size = ClassSize::Roundup(memory_size);
	//计算所需大小的内存对象所在链表在自由链表中的坐标
	size_t index = ClassSize::Index(memory_size);
	FreeList* memory_list = &_freelist[index];
	//如果链表中有则直接从链表中Pop出一个就行
	if (!memory_list->Empty())
	{
		return memory_list->Pop();
	}
	//如果链表中没有相应的对象则从central cache中申请
	else
	{
		return FetchFromCentralCache(index, memory_size);
	}
}

//解除分配，释放内存
void ThreadCache::Deallocate(void* ptr, size_t memory_size)
{
	assert(memory_size <= MAX_SIZE);
	memory_size = ClassSize::Roundup(memory_size);
	//计算所释放的内存对象在自由链表中的哪条对象链表中，并将对象链接入对应链表中
	size_t index = ClassSize::Index(memory_size);
	FreeList* memory_list = &_freelist[index];
	memory_list->Push(ptr);
	//如果thread cache中的内存对象链表中储存的内存对象已经超过了最大数量，就将内存对象链表中储存的内存对象回收到central cache中
	if (memory_list->Size() >= memory_list->MaxSize())
	{
		ListTooLong(memory_list, memory_size);
	}

}

//从central cache中获取内存对象
void* ThreadCache::FetchFromCentralCache(size_t index, size_t memory_size)
{
	FreeList* memory_list = &_freelist[index];
	//计算thread cache 一次从central cache中获取的内存对象的数量
	size_t num = min(ClassSize::QuantityToFetch(memory_size), memory_list->MaxSize());//获取数量为计算所得的数量和对象链表中能储存的最大数量中较小的那个
	//如果获取数量小于等于对象链表中所能储存的最大数量，则更改最大数量，以此来实现最大数量的动态变化
	if (num == memory_list->MaxSize())
	{
		memory_list->SetMaxSize(num + 1);
	}
	void* start = nullptr;
	void* end = nullptr;
	//fetch_num具体获取到的内存对象数量
	size_t fetch_num = CentralCache::GetInstance()->FetchRangeObj(start, end, num, memory_size);
	if (fetch_num > 1)
	{
		//将申请的多的内存对象插入到自由链表中
		memory_list->PushRange(ObjNext(start), end, fetch_num - 1);
	}
	//返回申请到的第一个内存对象
	return start;
}

//当内存对象链表太长时，回收到central cache中
void ThreadCache::ListTooLong(FreeList* memory_list, size_t memory_size)
{
	void* start = memory_list->Clear();
	CentralCache::GetInstance()->ReleaseListToSpans(start, memory_size);
}

