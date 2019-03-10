#include"PageCache.h"

PageCache PageCache::_inst;

//��ȡһ����span
Span* PageCache::NewSpan(size_t page_quantity)
{
	std::unique_lock<std::mutex> lock(_mutex);
	Span* span = nullptr;
	//����������ҳ��������page cache���ܹ�������ҳ������ֱ����ϵͳ�����ڴ�
	if (page_quantity > MAX_PAGE)
	{
		void* ptr = SystemAlloc(page_quantity);
		span = new Span;
		span->_pageid = (PageId)ptr >> PAGE_SHIFT;
		span->_pagequantity = page_quantity;
		_id_span_map[span->_pageid] = span;
	}
	//����������page cache�����ڴ�
	else
	{
		span = _NewSpan(page_quantity);
	}
	span->_objsize = page_quantity << PAGE_SHIFT;
	return span;
}

Span* PageCache::_NewSpan(size_t page_quantity)
{
	//�����Ҫ��page_quantityҳ��span�����������������У���ֱ�ӷ�����Ӧspan
	if (!_freelist[page_quantity - 1].Empty())
	{
		return _freelist[page_quantity - 1].PopFront();
	}
	//û�ж�Ӧҳ����span���������ұ���û�и����span
	for (int i = page_quantity; i < MAX_PAGE; ++i)
	{
		SpanList* page_list = &_freelist[i];
		//����оʹӴ��span����һ��page_quantity��С��span����
		if (!page_list->Empty())
		{
			Span* span = page_list->PopFront();
			Span* new_span = new Span;
			//�Ӵ��span��β���г���������Ҫ��page_quantity��С��span
			new_span->_pageid = span->_pageid + span->_pagequantity - page_quantity;
			new_span->_pagequantity = page_quantity;
			//Ϊnew_span�е�ÿһҳ�����µ�ӳ���ϵ
			for (size_t i = 0; i < new_span->_pagequantity; i++)
			{
				_id_span_map[new_span->_pageid + i] = new_span;
			}
			span->_pagequantity -= page_quantity;
			//����ʣ�µ�span���ӵ����������϶�Ӧ��С��span������
			_freelist[span->_pagequantity - 1].PushFront(span);
			return new_span;
		}
	}
	//û���ҵ��и����span��ֱ����ϵͳ����page cache���ܹ��������MAX_PAGEҳ���ڴ�
	void* ptr = SystemAlloc(MAX_PAGE);
	//�����뵽��MAX_PAGEҳ��max_span���ӵ����������ж�Ӧ��span������
	Span* max_span = new Span;
	max_span->_pageid = (PageId)ptr >> PAGE_SHIFT;
	max_span->_pagequantity = MAX_PAGE;
	_freelist[MAX_PAGE - 1].PushFront(max_span);
	//����ҳ�ź�max_span��Ĺ�ϵ
	for (size_t i = 0; i < max_span->_pagequantity; ++i)
	{
		_id_span_map[max_span->_pageid + i] = max_span;
	}
	//ͨ���ݹ���ã���max_span�и������Ҫ��page_quantity��С��span����
	return _NewSpan(page_quantity);
}

//��ȡ�ڴ����span��ӳ���ϵ
Span* PageCache::MapObjectToSpan(void* obj)
{
	PageId page_id = ((PageId)obj) >> PAGE_SHIFT;
	//std::unordered_map<PageId, Span*>::iterator it = _id_span_map.find(page_id);
	auto it = _id_span_map.find(page_id);
	assert(it != _id_span_map.end());
	return it->second;
}

//��span�ͷŻ�page cache
void PageCache::ReleaseSpanToPageCache(Span* span)
{	
	std::unique_lock<std::mutex> lock(_mutex);
	span->_usecount = 0;
	//����ͷŵ��ڴ泬����page cache���ܹ�������ҳ����ֱ�ӻ���ϵͳ
	if (span->_pagequantity > MAX_PAGE)
	{
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		_id_span_map.erase(span->_pageid);
		SystemFree(ptr);
		delete span;
	}
	//�������ͻ���page cache���кϲ�
	else
	{
		//Ѱ�ҵ�ǰspan�Ĺ�����ڴ��ǰһҳ���ڵ�span
		//std::unordered_map<PageId, Span*>::iterator prev_it = _id_span_map.find(span->_pageid - 1);
		auto prev_it = _id_span_map.find(span->_pageid - 1);
		while (prev_it != _id_span_map.end())
		{
			Span* prev_span = prev_it->second;
			//���ǰһ��span����ʹ�û�������span�ϲ���ҳ������������MAX_PAGE�Ͳ��ϲ�
			if (prev_span->_usecount != 0 || (prev_span->_pagequantity + span->_pagequantity) > MAX_PAGE)
			{
				break;
			}
			//�ϲ�����span
			_freelist[prev_span->_pagequantity - 1].Erase(prev_span);
			prev_span->_pagequantity += span->_pagequantity;
			delete span;
			span = prev_span;
			//����Ѱ��ǰһ��
			prev_it = _id_span_map.find(span->_pageid - 1);
		}
		//Ѱ�ҵ�ǰspan������ڴ�ĺ�һҳ���ڵ�span
		//std::unordered_map<PageId, Span*>::iterator next_it = _id_span_map.find(span->_pageid + span->_pagequantity);
		auto next_it = _id_span_map.find(span->_pageid + span->_pagequantity);
		while (next_it != _id_span_map.end())
		{
			Span* next_span = next_it->second;
			//�����һ��span����ʹ�û�������span�ϲ���ҳ������������MAX_PAGE�Ͳ��ϲ�
			if (next_span->_usecount != 0 || (next_span->_pagequantity + span->_pagequantity) > MAX_PAGE)
			{
				break;
			}
			//�ϲ�����span
			_freelist[next_span->_pagequantity - 1].Erase(next_span);
			span->_pagequantity += next_span->_pagequantity;
			delete next_span;
			//����Ѱ�Һ�һ��
			next_it = _id_span_map.find(span->_pageid + span->_pagequantity);
		}
		//�ϲ���󣬽����պϲ��ɵ�span���ӵ�������������Ӧ��span������
		_freelist[span->_pagequantity - 1].PushFront(span);
		//����ӳ��span��ҳ��֮��Ĺ�ϵ
		for (size_t i = 0; i < span->_pagequantity; ++i)
		{
			_id_span_map[span->_pageid + i] = span;
		}
	}
}