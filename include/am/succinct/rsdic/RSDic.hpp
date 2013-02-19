/*
 *  Copyright (c) 2012 Daisuke Okanohara
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *   1. Redistributions of source code must retain the above Copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above Copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the authors nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 */

#ifndef RSDIC_RSDIC_HPP_
#define RSDIC_RSDIC_HPP_

#include <types.h>

#include <vector>
#include <iostream>


NS_IZENELIB_AM_BEGIN

namespace succinct
{
namespace rsdic
{

typedef uint64_t rsdic_uint; // use uint64_t for bitvec >= 4GB

class RSDic
{
public:
    RSDic(bool support_select);
    ~RSDic();

    void Build(const std::vector<uint64_t>& bv, size_t len);
    void Clear();

    bool GetBit(size_t pos) const;
    bool GetBit(size_t pos, size_t& rank) const;

    size_t Rank0(size_t pos) const;
    size_t Rank1(size_t pos) const;
    size_t Rank(size_t pos, bool bit) const;

    size_t Select0(size_t ind) const;
    size_t Select1(size_t ind) const;
    size_t Select(size_t ind, bool bit) const;

    void Save(std::ostream& os) const;
    void Load(std::istream& is);
    size_t GetUsageBytes() const;

    bool support_select() const
    {
        return support_select_;
    }

    rsdic_uint num() const
    {
        return num_;
    }

    rsdic_uint one_num() const
    {
        return one_num_;
    }

    rsdic_uint zero_num() const
    {
        return num_ - one_num_;
    }

    bool operator == (const RSDic& bv) const;

private:
    void BuildBlock_(uint64_t block, size_t offset, size_t& global_offset);

    template <class T>
    void Save(std::ostream& os, const std::vector<T>& vs) const
    {
        size_t size = vs.size();
        os.write((const char*)&size, sizeof(size));
        os.write((const char*)&vs[0], sizeof(vs[0]) * size);
    }

    template <class T>
    void Load(std::istream& is, std::vector<T>& vs)
    {
        size_t size = 0;
        is.read((char*)&size, sizeof(size));
        vs.resize(size);
        is.read((char*)&vs[0], sizeof(vs[0]) * size);
    }

private:
    bool support_select_;
    rsdic_uint num_;
    rsdic_uint one_num_;

    std::vector<uint64_t> bits_;
    std::vector<rsdic_uint> pointer_blocks_;
    std::vector<rsdic_uint> rank_blocks_;
    std::vector<rsdic_uint> select_one_inds_;
    std::vector<rsdic_uint> select_zero_inds_;
    std::vector<uint8_t> rank_small_blocks_;
};

}
}

NS_IZENELIB_AM_END

#endif // RSDIC_BITVEC_HPP_
