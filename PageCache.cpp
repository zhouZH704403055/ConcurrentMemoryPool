#include"PageCache.h"

PageCache PageCache::_inst;

//获取一个新span
Span* PageCache::NewSpan(size_t page_quantity)
{
	std::unique_lock<std::mutex> lock(_mutex);
	Span* span = nullptr;
	//如果所申请的页数超过了page cache所能管理的最大页数，则直接向系统申请内存
	if (page_quantity > MAX_PAGE)
	{
		void* ptr = SystemAlloc(page_quantity);
		span = new Span;
		span->_pageid = (PageId)ptr >> PAGE_SHIFT;
		span->_pagequantity = page_quantity;
		_id_span_map[span->_pageid] = span;
	}
	//不超过就向page cache申请内存
	else
	{
		span = _NewSpan(page_quantity);
	}
	span->_objsize = page_quantity << PAGE_SHIFT;
	return span;
}

Span* PageCache::_NewSpan(size_t page_quantity)
{
	//如果需要的page_quantity页的span在自由链表中正好有，则直接返回相应span
	if (!_freelist[page_quantity - 1].Empty())
	{
		return _freelist[page_quantity - 1].PopFront();
	}
	//没有对应页数的span则往后面找比有没有更大的span
	for (int i = page_quantity; i < MAX_PAGE; ++i)
	{
		SpanList* page_list = &_freelist[i];
		//如果有就从大的span上切一个page_quantity大小的span返回
		if (!page_list->Empty())
		{
			Span* span = page_list->PopFront();
			Span* new_span = new Span;
			//从大的span的尾部切出我们所需要的page_quantity大小的span
			new_span->_pageid = span->_pageid + span->_pagequantity - page_quantity;
			new_span->_pagequantity = page_quantity;
			//为new_span中的每一页建立新的映射关系
			for (size_t i = 0; i < new_span->_pagequantity; i++)
			{
				_id_span_map[new_span->_pageid + i] = new_span;
			}
			span->_pagequantity -= page_quantity;
			//将切剩下的span链接到自由链表上对应大小的span链表中
			_freelist[span->_pagequantity - 1].PushFront(span);
			return new_span;
		}
	}
	//没有找到有更大的span，直接向系统申请page cache所能管理的最大的MAX_PAGE页的内存
	void* ptr = SystemAlloc(MAX_PAGE);
	//将申请到的MAX_PAGE页的max_span链接到自由链表中对应的span链表中
	Span* max_span = new Span;
	max_span->_pageid = (PageId)ptr >> PAGE_SHIFT;
	max_span->_pagequantity = MAX_PAGE;
	_freelist[MAX_PAGE - 1].PushFront(max_span);
	//建立页号和max_span间的关系
	for (size_t i = 0; i < max_span->_pagequantity; ++i)
	{
		_id_span_map[max_span->_pageid + i] = max_span;
	}
	//通过递归调用，将max_span切割成所需要的page_quantity大小的span返回
	return _NewSpan(page_quantity);
}

//获取内存对象到span的映射关系
Span* PageCache::MapObjectToSpan(void* obj)
{
	PageId page_id = ((PageId)obj) >> PAGE_SHIFT;
	//std::unordered_map<PageId, Span*>::iterator it = _id_span_map.find(page_id);
	auto it = _id_span_map.find(page_id);
	assert(it != _id_span_map.end());
	return it->second;
}

//将span释放回page cache
void PageCache::ReleaseSpanToPageCache(Span* span)
{	
	std::unique_lock<std::mutex> lock(_mutex);
	span->_usecount = 0;
	//如果释放的内存超过了page cache所能管理的最大页数，直接还给系统
	if (span->_pagequantity > MAX_PAGE)
	{
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		_id_span_map.erase(span->_pageid);
		SystemFree(ptr);
		delete span;
	}
	//不超过就还给page cache进行合并
	else
	{
		//寻找当前span的管理的内存的前一页所在的span
		//std::unordered_map<PageId, Span*>::iterator prev_it = _id_span_map.find(span->_pageid - 1);
		auto prev_it = _id_span_map.find(span->_pageid - 1);
		while (prev_it != _id_span_map.end())
		{
			Span* prev_span = prev_it->second;
			//如果前一个span还在使用或者两个span合并后页的数量超过了MAX_PAGE就不合并
			if (prev_span->_usecount != 0 || (prev_span->_pagequantity + span->_pagequantity) > MAX_PAGE)
			{
				break;
			}
			//合并两个span
			_freelist[prev_span->_pagequantity - 1].Erase(prev_span);
			prev_span->_pagequantity += span->_pagequantity;
			delete span;
			span = prev_span;
			//继续寻找前一个
			prev_it = _id_span_map.find(span->_pageid - 1);
		}
		//寻找当前span管理的内存的后一页所在的span
		//std::unordered_map<PageId, Span*>::iterator next_it = _id_span_map.find(span->_pageid + span->_pagequantity);
		auto next_it = _id_span_map.find(span->_pageid + span->_pagequantity);
		while (next_it != _id_span_map.end())
		{
			Span* next_span = next_it->second;
			//如果后一个span还在使用或者两个span合并后页的数量超过了MAX_PAGE就不合并
			if (next_span->_usecount != 0 || (next_span->_pagequantity + span->_pagequantity) > MAX_PAGE)
			{
				break;
			}
			//合并两个span
			_freelist[next_span->_pagequantity - 1].Erase(next_span);
			span->_pagequantity += next_span->_pagequantity;
			delete next_span;
			//继续寻找后一个
			next_it = _id_span_map.find(span->_pageid + span->_pagequantity);
		}
		//合并完后，将最终合并成的span链接到自由链表中相应的span链表中
		_freelist[span->_pagequantity - 1].PushFront(span);
		//重新映射span和页号之间的关系
		for (size_t i = 0; i < span->_pagequantity; ++i)
		{
			_id_span_map[span->_pageid + i] = span;
		}
	}
}