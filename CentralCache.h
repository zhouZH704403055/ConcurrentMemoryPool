#pragma once

#include"Common.h"

//使用单例模式，保证全局只有一个central cache
class CentralCache
{
public:
	//从central cache中获取一定数量的内存对象给thread cache
	//start指向获取到的内存对象的第一个，end指向最后一个，n是需要获取到的对象个数，memory_size是对象大小
	size_t FetchRangeObj(void*& start, void*& end, size_t num, size_t memory_size);
	//释放一定数量的内存对象到span中
	void ReleaseListToSpans(void* start, size_t memory_size);
	//从central cache的自由链表中对应的span链表中获取一个span
	Span* GetOneSpan(SpanList* span_list, size_t memory_size);

	//获取静态的对象
	static CentralCache* GetInstance()
	{
		return &_inst;
	}
private:
	SpanList _freelist[NLISTS];

	CentralCache() = default;
	CentralCache(const CentralCache&) = delete;
	CentralCache& operator=(const CentralCache&) = delete;
	//饿汉模式
	static CentralCache _inst;
};
