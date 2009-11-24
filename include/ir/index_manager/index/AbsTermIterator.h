/**
* @file        AbsTermIterator.h
* @author     Yingfeng Zhang
* @version     SF1 v5.0
* @brief Abstract base class for TermIterator
*/
#ifndef ABS_TERM_ITERATOR_H
#define ABS_TERM_ITERATOR_H

#include <ir/index_manager/utility/system.h>
#include <ir/index_manager/index/Term.h>
#include <ir/index_manager/index/TermInfo.h>

NS_IZENELIB_IR_BEGIN

namespace indexmanager{

class Posting;
/**
* @brief TermIterator is used to iterate terms, if necessary, it could provide the posting relevant to the term iterated.
* It is the base class of InMemoryTermIterator and DiskTermIterator.
* currently SkipList has not been added, it could be an improvement in future.
* After the IndexManager API has been modified, the Indexer does not need such an utility, however, The existence of
* TermIterator should be reasonable, and perhaps will be needed in future, therefore it is reserved, although currently
* it is only used during index merging process. TermIterator only iterator terms in a single barrel, if multi barrels exist,
* MultiTermIterator should be used.
*/
class TermIterator
{
public:
    TermIterator(void);

    virtual ~TermIterator(void);
public:
    /// whether there exists term
    virtual bool next() = 0;
    /// get current term
    virtual const Term* term() = 0;
    /// get current term info
    virtual const TermInfo* termInfo() = 0;
    /// get current term's posting (for merge only)
    virtual Posting* termPosting() = 0;

    virtual size_t setBuffer(char* pBuffer,size_t bufSize);

protected:

    char* pBuffer_;      ///buffer for iterator

    size_t nBuffSize_;   ///size of buffer
};


}

NS_IZENELIB_IR_END

#endif

