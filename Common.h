#pragma once

#include<mutex>
#include<assert.h>

#ifdef _WIN32
#include<Windows.h>
#endif //_WIN32


//自由链表的长度
const size_t NLISTS = 240;
//自由链表中所管理的内存对象的最大字节数
const size_t MAX_SIZE = 64 * 1024;//64Kb
//利用pageid求页的起始地址的时候需要左移的位数，页的大小为4K则为12，8K则为14
const size_t PAGE_SHIFT = 12;
//page cache中所管理的最大的
const size_t MAX_PAGE = 128;

inline static void* SystemAlloc(size_t page_quantity)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(NULL, page_quantity << PAGE_SHIFT, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	//如果申请失败，抛异常
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

//相当于obj->next，返回obj内存对象的下一个内存对象
static inline void*& ObjNext(void* obj)
{
	return *((void**)obj);
}

typedef size_t PageId;
struct Span
{
	PageId _pageid = 0;//页码
	size_t _pagequantity = 0;//页的数量

	Span* _next = nullptr;
	Span* _prev = nullptr;

	void* _objlist = nullptr;//自由链表
	size_t _objsize = 0;//对象链表中储存的内存对象大小
	size_t _usecount = 0;//使用计数
};

class SpanList
{
public:
	SpanList()
	{
		//带头结点的双向链表
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	//在cur位置的前一个插入new_span
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
	void* _memorylist = nullptr;//储存内存对象的链表
	size_t _size = 0;//链表中储存的内存对象的个数
	size_t _maxsize = 2;//自由链表中储存内存对象的最大个数，如果超过这个数量就还给central cache，是动态变化的
};

//管理对齐映射
class ClassSize
{
	// 对齐规则控制在12%左右的内碎片浪费
	// [1,128]				8byte对齐		freelist[0,16)
	// [129,1024]			16byte对齐		freelist[16,72)
	// [1025,8*1024]		128byte对齐		freelist[72,128)
	// [8*1024+1,64*1024]	512byte对齐		freelist[128,240)
public:
	//align是对齐数
	static inline size_t _Roundup(size_t memory_size,size_t align)
	{
		return (memory_size + align - 1)&(~(align - 1));
	}

	//计算需要开辟的内存大小经过对齐之后应该开辟的内存大小，向上取整
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

	//align为对齐数，align_left为1变为对齐数所需要左移的位数
	static inline size_t _Index(size_t memory_size, size_t align_left)
	{
		//相当于(memory_size+align-1)/align-1，使用未操作代替除法操作，效率更高
		return ((memory_size + (1 << align_left) - 1) >> align_left) - 1;
	}

	//计算所需大小的内存对象所在链表在自由链表中的坐标
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

	//根据所需要的内存对象的大小来计算一次所要获取的内存对象的数量
	static size_t QuantityToFetch(size_t memory_size)
	{
		if (memory_size == 0)
		{
			return 0;
		}

		//需要申请的内存对象的数量，至少申请2个，最多申请512个
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

	//需要申请的页数
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
