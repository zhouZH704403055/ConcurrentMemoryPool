#include"CentralCache.h"
#include"PageCache.h"

//静态成员必须在类外初始化
CentralCache CentralCache::_inst;

//从central cache中获取一定数量的内存对象给thread cache
//start指向获取到的一串内存对象的第一个，end指向最后一个，n是需要获取到的对象个数，memory_size是对象大小
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t num, size_t memory_size)
{
	//计算出要取的内存对象所在对象链表所在的span链表在自由链表中的下标
	size_t index = ClassSize::Index(memory_size);
	SpanList* span_list = &_freelist[index];
	std::unique_lock<std::mutex> lock(span_list->_mutex);
	//从span链表中获取一个span
	Span* span = GetOneSpan(span_list, memory_size);
	//从span中的对象链表中取出num个内存对象，如果不够则全部取出
	void* cur = span->_objlist;
	void* prev = cur;
	size_t fetch_num = 0;//实际所取到的内存对象的个数
	while (cur != nullptr && fetch_num < num)
	{
		prev = cur;
		cur = ObjNext(cur);
		++fetch_num;
	}
	span->_usecount += fetch_num;
	start = span->_objlist;
	end = prev;
	ObjNext(end) = nullptr;
	//如果span中的对象已经被全部取完，则将当前span移动到span链表的最后，以此提高查找可用span时的效率
	if (span->_objlist == nullptr)
	{
		span_list->Erase(span);
		span_list->PushBack(span);
	}
	//将取完剩余的对象链表重新连接到span中
	span->_objlist = cur;

	return fetch_num;
}

//释放一定数量内存对象到span中
void CentralCache::ReleaseListToSpans(void* start, size_t memory_size)
{
	size_t index = ClassSize::Index(memory_size);
	SpanList* span_list = &_freelist[index];
	std::unique_lock<std::mutex> lock(span_list->_mutex);
	while (start)
	{
		void* next = ObjNext(start);
		//计算要释放的内存对象属于哪个span中
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		//如果本来span已经为空，当内存对象回到span后，span中就又有了内存对象，则将span移动回span链表的首部
		if (span->_objlist == nullptr)
		{
			span_list->Erase(span);
			span_list->PushFront(span);
		}
		//将内存对象插入到span中
		ObjNext(start) = span->_objlist;
		span->_objlist = start;
		//当--usecount为0时则表示当前span中的所有内存都已经归还，将span释放回到page cache中
		if (--span->_usecount == 0)
		{
			span_list->Erase(span);
			span->_objlist = nullptr;
			span->_objsize = 0;
			span->_prev = nullptr;
			span->_next = nullptr;
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}
		start = next;
	}
}

//从central cache的自由链表中对应的span链表中获取一个span
Span* CentralCache::GetOneSpan(SpanList* span_list, size_t memory_size)
{
	//如果span链表中有可以用的span则直接返回
	Span* span = span_list->Begin();
	while (span != span_list->End())
	{
		if (span->_objlist != nullptr)
		{
			return span;
		}
		span = span->_next;
	}
	//向page cache申请一个新的span
	size_t page_quantity = ClassSize::QuantityMovePage(memory_size);
	Span* new_span = PageCache::GetInstance()->NewSpan(page_quantity);
	//将新申请的span中的整块内存切割成大小为memory_size的内存对象，并链接起来
	char* start = (char*)((new_span->_pageid) << PAGE_SHIFT);//通过页码计算内存的起始地址
	char* end = start + ((new_span->_pagequantity) << PAGE_SHIFT);
	char* cur = start;
	char* next = cur + memory_size;
	while (next < end)
	{
		ObjNext(cur) = (void*)next;
		cur = next;
		next = cur + memory_size;
	}
	ObjNext(cur) = nullptr;
	new_span->_objlist = (void*)start;
	new_span->_objsize = memory_size;
	//将新申请的span插入到自由链表对应的span链表中
	span_list->PushFront(new_span);
	return new_span;
}

