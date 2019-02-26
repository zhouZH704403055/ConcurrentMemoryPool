#include"ThreadCache.h"
#include"CentralCache.h"

//�����ڴ�
void* ThreadCache::Allocate(size_t memory_size)
{
	assert(memory_size <= MAX_SIZE);
	//�������ȡ������ڴ�����С
	memory_size = ClassSize::Roundup(memory_size);
	//���������С���ڴ�����������������������е�����
	size_t index = ClassSize::Index(memory_size);
	FreeList* memory_list = &_freelist[index];
	//�������������ֱ�Ӵ�������Pop��һ������
	if (!memory_list->Empty())
	{
		return memory_list->Pop();
	}
	//���������û����Ӧ�Ķ������central cache������
	else
	{
		return FetchFromCentralCache(index, memory_size);
	}
}

//������䣬�ͷ��ڴ�
void ThreadCache::Deallocate(void* ptr, size_t memory_size)
{
	assert(memory_size <= MAX_SIZE);
	memory_size = ClassSize::Roundup(memory_size);
	//�������ͷŵ��ڴ���������������е��������������У����������������Ӧ������
	size_t index = ClassSize::Index(memory_size);
	FreeList* memory_list = &_freelist[index];
	memory_list->Push(ptr);
	//���thread cache�е��ڴ���������д�����ڴ�����Ѿ�����������������ͽ��ڴ���������д�����ڴ������յ�central cache��
	if (memory_list->Size() >= memory_list->MaxSize())
	{
		ListTooLong(memory_list, memory_size);
	}

}

//��central cache�л�ȡ�ڴ����
void* ThreadCache::FetchFromCentralCache(size_t index, size_t memory_size)
{
	FreeList* memory_list = &_freelist[index];
	//����thread cache һ�δ�central cache�л�ȡ���ڴ���������
	size_t num = min(ClassSize::QuantityToFetch(memory_size), memory_list->MaxSize());//��ȡ����Ϊ�������õ������Ͷ����������ܴ������������н�С���Ǹ�
	//�����ȡ����С�ڵ��ڶ������������ܴ����������������������������Դ���ʵ����������Ķ�̬�仯
	if (num == memory_list->MaxSize())
	{
		memory_list->SetMaxSize(num + 1);
	}
	void* start = nullptr;
	void* end = nullptr;
	//fetch_num�����ȡ�����ڴ��������
	size_t fetch_num = CentralCache::GetInstance()->FetchRangeObj(start, end, num, memory_size);
	if (fetch_num > 1)
	{
		//������Ķ���ڴ������뵽����������
		memory_list->PushRange(ObjNext(start), end, fetch_num - 1);
	}
	//�������뵽�ĵ�һ���ڴ����
	return start;
}

//���ڴ��������̫��ʱ�����յ�central cache��
void ThreadCache::ListTooLong(FreeList* memory_list, size_t memory_size)
{
	void* start = memory_list->Clear();
	CentralCache::GetInstance()->ReleaseListToSpans(start, memory_size);
}

