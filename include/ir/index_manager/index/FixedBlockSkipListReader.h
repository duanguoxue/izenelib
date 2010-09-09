/**
* @file        FixedBlockSkipListReader.h
* @author     Yingfeng Zhang
* @version     SF1 v5.0
* @brief Reading skiplist data for fixed block posting
*/

#ifndef FIXED_BLOCK_SKIPLIST_READER_H
#define FIXED_BLOCK_SKIPLIST_READER_H

#include <ir/index_manager/utility/MemCache.h>
#include <ir/index_manager/store/IndexInput.h>

NS_IZENELIB_IR_BEGIN

namespace indexmanager{

/**
 * @brief FixedBlockSkipListReader
 */
class FixedBlockSkipListReader
{
public:
    FixedBlockSkipListReader(IndexInput* pSkipInput);

    virtual ~FixedBlockSkipListReader();

public:
    docid_t skipTo(docid_t docID);

    bool nextSkip(docid_t docID);

    docid_t getDoc() { return lastDoc_;}

    fileoffset_t getOffset() { return lastOffset_; }
	
    fileoffset_t getPOffset() { return lastPOffset_; }

    int getNumSkipped() { return totalSkipped_; }

private:
    bool loadNextSkip();

private:
    IndexInput* skipStream_;

    int totalSkipped_;

    docid_t skipDoc_;
    docid_t lastDoc_; 
    fileoffset_t offset_;
    fileoffset_t lastOffset_;
    fileoffset_t pOffset_;
    fileoffset_t lastPOffset_;
};

}
NS_IZENELIB_IR_END

#endif


