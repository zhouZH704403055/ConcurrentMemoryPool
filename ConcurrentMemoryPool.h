#pragma once
#include"ThreadCache.h"
#include"PageCache.h"

static void* ConcurrentAlloc(size_t memory_size)
{
	if (memory_size > MAX_SIZE)
	{
		memory_size = ClassSize::_Roundup(memory_size, 1 << PAGE_SHIFT);
		size_t page_quantity = memory_size >> PAGE_SHIFT;
		Span* span = PageCache::GetInstance()->NewSpan(page_quantity);
		++span->_usecount;
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		if (tls_thread_cache == nullptr)
		{
			tls_thread_cache = new ThreadCache;
		}
		return tls_thread_cache->Allocate(memory_size);
	}
}

static void ConcurrentFree(void* ptr)
{
	assert(ptr);
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t memory_size = span->_objsize;
	if (memory_size > MAX_SIZE)
	{
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
	}
	else
	{
		tls_thread_cache->Deallocate(ptr, memory_size);
	}
}
