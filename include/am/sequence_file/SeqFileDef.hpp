///
/// @file   SeqFileDef.hpp
/// @brief  
/// @author Jia Guo
/// @date   Created 2010-03-22
/// @date   Updated 2010-03-22
///
#ifndef SEQFILEDEF_HPP_
#define SEQFILEDEF_HPP_

#include <string>
#include <algorithm>
#include <fstream>

#include <boost/filesystem.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/bind.hpp>
#include <boost/serialization/deque.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/thread.hpp> 
#include <boost/format.hpp>

#include <am/graph_index/dyn_array.hpp>
#include <am/tokyo_cabinet/tc_fixdb.h>
#include <cache/IzeneCache.h>
#include <util/izene_serialization.h>
#include <util/Exception.h>
#include <util/bzip.h>

NS_IZENELIB_AM_BEGIN


template <typename T>
class CommonSeqFileSerializeHandler
{
    public:
    static char* serialize(const T& data, std::size_t& vsize)
    {
        char* ptr;
        izenelib::util::izene_serialization<T> izs(data);
        izs.write_image(ptr, vsize);
        char* result = (char*)malloc(vsize);
        memcpy(result, ptr, vsize);
        return result;
    }
    
    static void deserialize(char*& ptr, std::size_t vsize, T& data)
    {
        izenelib::util::izene_deserialization<T> izd_value(ptr, vsize);
        izd_value.read_image(data);
    }
};

template <typename T>
class CompressSeqFileSerializeHandler
{
    public:
    static char* serialize(const T& data, std::size_t& vsize)
    {
        char* ptr;
        izenelib::util::izene_serialization<T> izs(data);
        std::size_t ivsize;
        izs.write_image(ptr, ivsize);
        int sp;
        char* result = _tc_bzcompress(ptr, (int)ivsize, &sp);
        vsize = (std::size_t) sp;
        return result;
    }
    
    static void deserialize(char* ptr, std::size_t vsize, T& data)
    {
        int sp;
        char* result = _tc_bzdecompress(ptr, (int)vsize, &sp);
        izenelib::util::izene_deserialization<T> izd_value(result, (std::size_t)sp);
        izd_value.read_image(data);
        free(result);
    }
};

template <>
class CompressSeqFileSerializeHandler<std::vector<uint32_t> >
{
    public:
    static char* serialize(const std::vector<uint32_t>& data, std::size_t& len)
    {
        char* result = NULL;
        if (data.size()==0)
        {
            len = 0;
            return result;
        }
        
        
        uint32_t count = data.size();
        uint16_t max = 65535;
        if(count >= max/4)
        {
            count = max/4;
        }
        
        izenelib::am::DynArray<uint32_t> ar;
        for(uint32_t i=0;i<count;i++)
            ar.push_back(data[i]);
        ar.sort();
        uint32_t last = ar.at(0);
        //std::cout<<"[ID]: "<<ar.at(0)<<", ";
        for(uint32_t i=1;i<ar.length();i++)
        {
            //std::cout<<ar.at(i)<<", ";
            ar[i] = ar.at(i)-last;
            last += ar.at(i);
        }
        //std::cout<<std::endl;
        
        result = (char*)malloc((uint16_t) (count*4));
        char* pResult = result;
        len = 0;
        
        uint32_t ui = 0;
        for(uint32_t i=0;i<ar.length();i++)
        {
            ui = ar.at(i);
            while ((ui & ~0x7F) != 0)
            {
                *(pResult + len) = ((uint8_t)((ui & 0x7f) | 0x80));
                ui >>= 7;
                ++len;
            }
            *(pResult + len) = (uint8_t)ui;
            ++len;
        }
        return result;
    }
    
    static void deserialize(char* ptr, std::size_t len, std::vector<uint32_t>& vdata)
    {
        if (len ==0)
        return;
        char* data = ptr;
        izenelib::am::DynArray<uint32_t> ar;
        for (uint16_t p = 0; p<len;)
        { 
            uint8_t b = *(data+p);
            ++p;
            
            uint32_t i = b & 0x7F;
            for (uint32_t shift = 7; (b & 0x80) != 0; shift += 7)
            {
                b = *(data+p);
                ++p;
                i |= (b & 0x7FL) << shift;
            }
            
            ar.push_back(i);
        }
        
        vdata.push_back(ar.at(0));
        
        for (uint32_t i =1; i<ar.length(); ++i)
        {
            uint32_t id = ar.at(i)+vdata[i-1];
            vdata.push_back(id);
        }
    }
};

template <typename T, template <class SerialType> class SerialHandler >
class SeqFileObjectCacheHandler
{
    public:
    SeqFileObjectCacheHandler():cache_(0), cacheSize_(0), maxCacheId_(0)
    {
    }
    ~SeqFileObjectCacheHandler()
    {
    }
    
    bool insertToCache(std::size_t id, const T& data)
    {
        if( id == 0 ) return false;
        if( id > cacheSize_ ) return false;
        cache_[id-1] = data;
        if( id > maxCacheId_ )
        {
            maxCacheId_ = id;
        }
        return true;
    }
    
    bool getInCache(std::size_t id, T& data)
    {
        if( id == 0 ) return false;
        if( id > maxCacheId_ ) return false;
        data = cache_[id-1];
        return true;
    }
    
    void setCacheSize(std::size_t cacheSize)
    {
        cacheSize_ = cacheSize;
    }
    
    std::size_t getCacheSize() const
    {
        return cacheSize_;
    }

    
    protected:
        void initCache_()
        {
            cache_.resize(cacheSize_);
        }
        
    
    private:
        std::vector<T> cache_;
        std::size_t cacheSize_;
        std::size_t maxCacheId_;
};

template <typename T, template <class SerialType> class SerialHandler>
class SeqFileCharCacheHandler
{
    typedef SerialHandler<T> SerType;
    typedef std::pair<char*, uint32_t> ValueType;
    public:
    SeqFileCharCacheHandler():cache_(0), cacheSize_(0), maxCacheId_(0)
    {
    }
    ~SeqFileCharCacheHandler()
    {
        for(uint32_t i=0;i<cache_.size();i++)
        {
            if( cache_[i].first!=NULL )
            {
                free(cache_[i].first);
            }
        }
    }
    
    bool insertToCache(std::size_t id, const T& data)
    {
        if( id == 0 ) return false;
        if( id > cacheSize_ ) return false;
        ValueType& value = cache_[id-1];
        if (cache_[id-1].second > 0 )
        {
            free(cache_[id-1].first);
            cache_[id-1].first = NULL;
            cache_[id-1].second = 0;
        }
        std::size_t vsize;
        char* ptr = SerType::serialize(data, vsize);
        if( vsize>0 )
        {
            cache_[id-1].first = ptr;
            cache_[id-1].second = (uint32_t)vsize;
        }
        
        if( id > maxCacheId_ )
        {
            maxCacheId_ = id;
        }
        return true;
    }
    
    bool getInCache(std::size_t id, T& data)
    {
        if( id == 0 ) return false;
        if( id > maxCacheId_ ) return false;
        if( cache_[id-1].first == NULL || cache_[id-1].second==0 )
        {
            return false;
        }
        SerType::serialize(cache_[id-1].first, cache_[id-1].second, data);
        return true;
    }
    
    void setCacheSize(std::size_t cacheSize)
    {
        cacheSize_ = cacheSize;
    }

    
    protected:
        void initCache_()
        {
            ValueType defaultValue(NULL, 0);
            cache_.resize(cacheSize_, defaultValue);
        }
        
    
    private:
        std::vector<ValueType > cache_;
        std::size_t cacheSize_;
        std::size_t maxCacheId_;
    
};

NS_IZENELIB_AM_END
#endif
