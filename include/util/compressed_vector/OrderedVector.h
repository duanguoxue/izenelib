#ifndef IZENELIB_UTIL_COMPRESSED_VECTOR_ORDERED_H
#define IZENELIB_UTIL_COMPRESSED_VECTOR_ORDERED_H

#include "detail/Compressor.h"
#include <util/MemPool.h>

#include <boost/iterator/iterator_facade.hpp>
#include <limits> 
#include <iostream>

NS_IZENELIB_UTIL_BEGIN

namespace compressed_vector{

class OrderedVector
{
    MemPool* pMemPool_;
    detail::DataChunk* pHeadChunk_;
    detail::DataChunk* pTailChunk_;
    uint32_t nTotalSize_;
    uint32_t nPosInCurChunk_;
    uint32_t nTotalUsed_;
    uint32_t nLastVal_;

    uint32_t nCount_;
    void add_v_data(uint32_t val);

    void add_chunk();

    void read_internal(
        detail::DataChunk* &pDataChunk, 
        int32_t& curr_pos_in_chunk, 
        uint8_t* buffer, 
        size_t length) const;
public:
    OrderedVector(MemPool* pMemPool);

    ~OrderedVector();

    void push_back(uint32_t val);

    size_t size(){ return nCount_;}

    template<typename Element>
    class vector_iterator
       : public boost::iterator_facade< 
                       vector_iterator<Element>, 
                       Element, 
                       boost::forward_traversal_tag >
    {
        const OrderedVector * vector_;
        uint32_t data_len_;
        detail::DataChunk* p_data_chunk_;
        int32_t curr_pos_in_chunk_;
        mutable uint32_t curr_val_;
        uint8_t buffer_[CHUNK_ALLOC_LOWER_LIMIT];
        uint32_t buffer_len_;
        uint32_t buffer_start_;
        uint32_t buffer_pos_;

    public:
        vector_iterator()
        : vector_(0)
        , data_len_(0)
        , p_data_chunk_(0)
        , curr_pos_in_chunk_(0)
        , curr_val_(0)
        , buffer_len_(0)
        , buffer_start_(0)
        , buffer_pos_(0) {}

        explicit vector_iterator(const OrderedVector& p)
        : vector_(&p)
        , data_len_(p.nTotalUsed_)
        , p_data_chunk_(p.pHeadChunk_)
        , curr_pos_in_chunk_(0)
        , curr_val_(0)
        , buffer_len_(0)
        , buffer_start_(0)
        , buffer_pos_(0) 
        {
            increment();
        }

    private:
        friend class boost::iterator_core_access;

        void increment() 
        { 
            if(vector_)
            {
                uint32_t v = read_vint32();
                curr_val_ += v;
                if(buffer_start_ >= data_len_) vector_ = NULL;
            }
        }

        bool equal(vector_iterator<Element> const& other) const
        {
            return this->vector_ == other.vector_;
        }

        Element& dereference() const {return curr_val_; }

        uint32_t read_vint32()
        {
            uint8_t b = read_byte();
            uint32_t i = b & 0x7F;
            for (uint32_t shift = 7; (b & 0x80) != 0; shift += 7)
            {
                b = read_byte();
                i |= (b & 0x7FL) << shift;
            }
            return i;
        }

        uint8_t read_byte()
        {
            if (buffer_pos_ >= buffer_len_)
                refill();
            return buffer_[buffer_pos_++];
        }

        void refill()
        {
            buffer_start_ += buffer_pos_;
            buffer_pos_ = buffer_len_ = 0;
	
            assert(buffer_start_ < data_len_);
	
            if(buffer_start_ + CHUNK_ALLOC_LOWER_LIMIT > data_len_)
                buffer_len_ = data_len_ - buffer_start_;
            else
                buffer_len_ =  CHUNK_ALLOC_LOWER_LIMIT;

            vector_->read_internal(p_data_chunk_,curr_pos_in_chunk_,buffer_,buffer_len_);
        }
    };
    typedef vector_iterator<uint32_t> iterator;
    typedef vector_iterator<uint32_t const > const_iterator;
    iterator begin()
    {
        return iterator(*this);
    }
    iterator end()
    {
        return iterator();
    }
    const_iterator begin() const
    {
        return const_iterator(*this);
    }
    const_iterator end() const
    {
        return const_iterator();
    }
};

}

NS_IZENELIB_UTIL_END


#endif

