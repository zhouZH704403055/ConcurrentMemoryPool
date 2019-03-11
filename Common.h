#pragma once

#include<mutex>
#include<assert.h>

#ifdef _WIN32
#include<Windows.h>
#endif //_WIN32


//��������ĳ���
const size_t NLISTS = 240;
//������������������ڴ���������ֽ���
const size_t MAX_SIZE = 64 * 1024;//64Kb
//����pageid��ҳ����ʼ��ַ��ʱ����Ҫ���Ƶ�λ����ҳ�Ĵ�СΪ4K��Ϊ12��8K��Ϊ14
const size_t PAGE_SHIFT = 12;
//page cache�������������
const size_t MAX_PAGE = 128;

inline static void* SystemAlloc(size_t page_quantity)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(NULL, page_quantity << PAGE_SHIFT, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	//�������ʧ�ܣ����쳣
	if (ptr == nullptr)
	{
		throw std::bad_alloc();
	}
#else
	//brk \ mmap
#endif
	return ptr;
}

inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	//...
#endif
}

//�൱��obj->next������obj�ڴ�������һ���ڴ����
static inline void*& ObjNext(void* obj)
{
	return *((void**)obj);
}

typedef size_t PageId;
struct Span
{
	PageId _pageid = 0;//ҳ��
	size_t _pagequantity = 0;//ҳ������

	Span* _next = nullptr;
	Span* _prev = nullptr;

	void* _objlist = nullptr;//��������
	size_t _objsize = 0;//���������д�����ڴ�����С
	size_t _usecount = 0;//ʹ�ü���
};

class SpanList
{
public:
	SpanList()
	{
		//��ͷ����˫������
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	//��curλ�õ�ǰһ������new_span
	void Insert(Span* cur,Span* new_span)
	{
		assert(cur);
		Span* prev = cur->_prev;
		prev->_next = new_span;
		new_span->_prev = prev;
		new_span->_next = cur;
		cur->_prev = new_span;
	}

	void Erase(Span* cur)
	{
		assert(cur != nullptr && cur != _head);
		Span* prev = cur->_prev;
		Span* next = cur->_next;
		prev->_next = next;
		next->_prev = prev;
	}

	void PushFront(Span* new_span)
	{
		Insert(Begin(), new_span);
	}

	void PushBack(Span* span)
	{
		Insert(End(), span);
	}

	Span* PopFront()
	{
		Span* span = Begin();
		Erase(span);
		return span;
	}

	Span* PopBack()
	{
		Span* tail = End();
		tail = tail->_prev;
		Erase(tail);
		return tail;
	}

	Span* Begin()
	{
		return _head->_next;
	}
	Span* End()
	{
		return _head;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}
public:
	std::mutex _mutex;
private:
	Span* _head = nullptr;
};

class FreeList
{
public:
	bool Empty()
	{
		return _memorylist == nullptr;
	}

	void Push(void* obj)
	{
		ObjNext(obj) = _memorylist;
		++_size;
		_memorylist = obj;
	}

	void PushRange(void* start, void* end, size_t num)
	{
		ObjNext(end) = _memorylist;
		_memorylist = start;
		_size += num;
	}

	void* Clear()
	{
		_size = 0;
		void* ret = _memorylist;
		_memorylist = nullptr;
		return ret;
	}

	void* Pop()
	{
		void* obj = _memorylist;
		_memorylist = ObjNext(obj);
		--_size;
		return obj;
	}

	size_t Size()
	{
		return _size;
	}

	size_t MaxSize()
	{
		return _maxsize;
	}

	void SetMaxSize(size_t max_size)
	{
		_maxsize = max_size;
	}

private:
	void* _memorylist = nullptr;//�����ڴ���������
	size_t _size = 0;//�����д�����ڴ����ĸ���
	size_t _maxsize = 2;//���������д����ڴ����������������������������ͻ���central cache���Ƕ�̬�仯��
};

//�������ӳ��
class ClassSize
{
	// ������������12%���ҵ�����Ƭ�˷�
	// [1,128]				8byte����		freelist[0,16)
	// [129,1024]			16byte����		freelist[16,72)
	// [1025,8*1024]		128byte����		freelist[72,128)
	// [8*1024+1,64*1024]	512byte����		freelist[128,240)
public:
	//align�Ƕ�����
	static inline size_t _Roundup(size_t memory_size,size_t align)
	{
		return (memory_size + align - 1)&(~(align - 1));
	}

	//������Ҫ���ٵ��ڴ��С��������֮��Ӧ�ÿ��ٵ��ڴ��С������ȡ��
	static inline size_t Roundup(size_t memory_size)
	{
		assert(memory_size <= MAX_SIZE);
		if (memory_size <= 128)
		{
			return _Roundup(memory_size, 8);
		}
		else if (memory_size <= 1024)
		{
			return _Roundup(memory_size, 16);
		}
		else if (memory_size <= 8192)
		{
			return _Roundup(memory_size, 128);
		}
		else if (memory_size <= 65536)
		{
			return _Roundup(memory_size, 512);
		}
		return -1;
	}

	//alignΪ��������align_leftΪ1��Ϊ����������Ҫ���Ƶ�λ��
	static inline size_t _Index(size_t memory_size, size_t align_left)
	{
		//�൱��(memory_size+align-1)/align-1��ʹ��δ�����������������Ч�ʸ���
		return ((memory_size + (1 << align_left) - 1) >> align_left) - 1;
	}

	//���������С���ڴ�����������������������е�����
	static inline size_t Index(size_t memory_size)
	{
		assert(memory_size <= MAX_SIZE);
		static int group_array[3] = { 16,56,56 };
		if (memory_size <= 128)
		{
			return _Index(memory_size, 3);
		}
		else if (memory_size <= 1024)
		{
			return _Index(memory_size - 128, 4) + group_array[0];
		}
		else if (memory_size <= 8192)
		{
			return _Index(memory_size - 1024, 7) + group_array[0] + group_array[1];
		}
		else if (memory_size <= 65536)
		{
			return _Index(memory_size - 8196, 9) + group_array[0] + group_array[1] + group_array[2];
		}
		return -1;
	}

	//��������Ҫ���ڴ����Ĵ�С������һ����Ҫ��ȡ���ڴ���������
	static size_t QuantityToFetch(size_t memory_size)
	{
		if (memory_size == 0)
		{
			return 0;
		}

		//��Ҫ������ڴ�������������������2�����������512��
		int quantity = static_cast<int>(MAX_SIZE / memory_size);
		if (quantity < 2)
		{
			quantity = 2;
		}
		else if (quantity > 512)
		{
			quantity = 512;
		}
		return quantity;
	}

	//��Ҫ�����ҳ��
	static size_t QuantityMovePage(size_t memory_size)
	{
		size_t quantity = QuantityToFetch(memory_size);
		size_t page_quantity = quantity * memory_size;

		page_quantity >>= 12;
		if (page_quantity == 0)
		{
			page_quantity = 1;
		}

		return page_quantity;
	}
};
