#include"CentralCache.h"
#include"PageCache.h"

//��̬��Ա�����������ʼ��
CentralCache CentralCache::_inst;

//��central cache�л�ȡһ���������ڴ�����thread cache
//startָ���ȡ����һ���ڴ����ĵ�һ����endָ�����һ����n����Ҫ��ȡ���Ķ��������memory_size�Ƕ����С
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t num, size_t memory_size)
{
	//�����Ҫȡ���ڴ�������ڶ����������ڵ�span���������������е��±�
	size_t index = ClassSize::Index(memory_size);
	SpanList* span_list = &_freelist[index];
	std::unique_lock<std::mutex> lock(span_list->_mutex);
	//��span�����л�ȡһ��span
	Span* span = GetOneSpan(span_list, memory_size);
	//��span�еĶ���������ȡ��num���ڴ�������������ȫ��ȡ��
	void* cur = span->_objlist;
	void* prev = cur;
	size_t fetch_num = 0;//ʵ����ȡ�����ڴ����ĸ���
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
	//���span�еĶ����Ѿ���ȫ��ȡ�꣬�򽫵�ǰspan�ƶ���span���������Դ���߲��ҿ���spanʱ��Ч��
	if (span->_objlist == nullptr)
	{
		span_list->Erase(span);
		span_list->PushBack(span);
	}
	//��ȡ��ʣ��Ķ��������������ӵ�span��
	span->_objlist = cur;

	return fetch_num;
}

//�ͷ�һ�������ڴ����span��
void CentralCache::ReleaseListToSpans(void* start, size_t memory_size)
{
	size_t index = ClassSize::Index(memory_size);
	SpanList* span_list = &_freelist[index];
	std::unique_lock<std::mutex> lock(span_list->_mutex);
	while (start)
	{
		void* next = ObjNext(start);
		//����Ҫ�ͷŵ��ڴ���������ĸ�span��
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		//�������span�Ѿ�Ϊ�գ����ڴ����ص�span��span�о��������ڴ������span�ƶ���span������ײ�
		if (span->_objlist == nullptr)
		{
			span_list->Erase(span);
			span_list->PushFront(span);
		}
		//���ڴ������뵽span��
		ObjNext(start) = span->_objlist;
		span->_objlist = start;
		//��--usecountΪ0ʱ���ʾ��ǰspan�е������ڴ涼�Ѿ��黹����span�ͷŻص�page cache��
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

//��central cache�����������ж�Ӧ��span�����л�ȡһ��span
Span* CentralCache::GetOneSpan(SpanList* span_list, size_t memory_size)
{
	//���span�������п����õ�span��ֱ�ӷ���
	Span* span = span_list->Begin();
	while (span != span_list->End())
	{
		if (span->_objlist != nullptr)
		{
			return span;
		}
		span = span->_next;
	}
	//��page cache����һ���µ�span
	size_t page_quantity = ClassSize::QuantityMovePage(memory_size);
	Span* new_span = PageCache::GetInstance()->NewSpan(page_quantity);
	//���������span�е������ڴ��и�ɴ�СΪmemory_size���ڴ���󣬲���������
	char* start = (char*)((new_span->_pageid) << PAGE_SHIFT);//ͨ��ҳ������ڴ����ʼ��ַ
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
	//���������span���뵽���������Ӧ��span������
	span_list->PushFront(new_span);
	return new_span;
}

