#pragma once
#include"Common.h"
#include<unordered_map>

//ʹ�õ���ģʽ����֤ȫ��ֻ��һ��page cache
class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_inst;
	}
	//page_quantityΪҳ������
	Span* NewSpan(size_t page_quantity);//��ȡһ���µ�span
	Span* MapObjectToSpan(void* obj);//��ȡ�ڴ����span��ӳ���ϵ
	void ReleaseSpanToPageCache(Span* span);//��span�ͷŻ�page cache

private:
	PageCache() = default;
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;

	Span* _NewSpan(size_t page_quantity);
private:
	SpanList _freelist[MAX_PAGE];
	std::unordered_map<PageId, Span*> _id_span_map;//����ҳ�ŵ�span��ӳ���ϵ
	std::mutex _mutex;
	static PageCache _inst;
};
