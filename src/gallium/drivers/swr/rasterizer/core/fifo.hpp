/****************************************************************************
* Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
* @file fifo.hpp
*
* @brief Definitions for our fifos used for thread communication.
*
******************************************************************************/
#pragma once


#include "common/os.h"
#include "arena.h"

#include <vector>
#include <cassert>

template<class T>
struct QUEUE
{
    OSALIGNLINE(volatile uint32_t) mLock{ 0 };
    OSALIGNLINE(volatile uint32_t) mNumEntries{ 0 };
    std::vector<T*> mBlocks;
    T* mCurBlock{ nullptr };
    uint32_t mHead{ 0 };
    uint32_t mTail{ 0 };
    uint32_t mCurBlockIdx{ 0 };

    // power of 2
    static const uint32_t mBlockSizeShift = 6;
    static const uint32_t mBlockSize = 1 << mBlockSizeShift;

    void clear(Arena& arena)
    {
        mHead = 0;
        mTail = 0;
        mBlocks.clear();
        T* pNewBlock = (T*)arena.Alloc(sizeof(T)*mBlockSize);
        mBlocks.push_back(pNewBlock);
        mCurBlock = pNewBlock;
        mCurBlockIdx = 0;

        mNumEntries = 0;
        _ReadWriteBarrier();
        mLock = 0;
    }

    uint32_t getNumQueued()
    {
        return mNumEntries;
    }

    bool tryLock()
    {
        if (mLock)
        {
            return false;
        }

        // try to lock the FIFO
        LONG initial = InterlockedCompareExchange(&mLock, 1, 0);
        return (initial == 0);
    }
        
    void unlock()
    {
        mLock = 0;
    }

    T* peek()
    {
        if (mNumEntries == 0)
        {
            return nullptr;
        }
        uint32_t block = mHead >> mBlockSizeShift;
        return &mBlocks[block][mHead & (mBlockSize-1)];
    }

    void dequeue_noinc()
    {
        mHead ++;
        mNumEntries --;
    }

    bool enqueue_try_nosync(Arena& arena, const T* entry)
    {
        memcpy(&mCurBlock[mTail], entry, sizeof(T));

        mTail ++;
        if (mTail == mBlockSize)
        {
            if (++mCurBlockIdx < mBlocks.size())
            {
                mCurBlock = mBlocks[mCurBlockIdx];
            }
            else
            {
                T* newBlock = (T*)arena.Alloc(sizeof(T)*mBlockSize);
                SWR_ASSERT(newBlock);

                mBlocks.push_back(newBlock);
                mCurBlock = newBlock;
            }

            mTail = 0;
        }

        mNumEntries ++;
        return true;
    }

    void destroy()
    {
    }

};