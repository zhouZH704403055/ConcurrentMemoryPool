#pragma once
#include"Common.h"
#include<unordered_map>

//使用单例模式，保证全局只有一个page cache
class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_inst;
	}
	//page_quantity为页的数量
	Span* NewSpan(size_t page_quantity);//获取一个新的span
	Span* MapObjectToSpan(void* obj);//获取内存对象到span的映射关系
	void ReleaseSpanToPageCache(Span* span);//将span释放回page cache

private:
	PageCache() = default;
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;

	Span* _NewSpan(size_t page_quantity);
private:
	SpanList _freelist[MAX_PAGE];
	std::unordered_map<PageId, Span*> _id_span_map;//储存页号到span的映射关系
	std::mutex _mutex;
	static PageCache _inst;
};
