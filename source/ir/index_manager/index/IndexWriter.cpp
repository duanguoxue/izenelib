#include <ir/index_manager/index/IndexWriter.h>
#include <ir/index_manager/index/Indexer.h>
#include <ir/index_manager/index/IndexReader.h>
#include <ir/index_manager/index/IndexMerger.h>
#include <ir/index_manager/index/OfflineIndexMerger.h>
#include <ir/index_manager/index/ImmediateMerger.h>
#include <ir/index_manager/index/MultiWayMerger.h>
#include <ir/index_manager/index/GPartitionMerger.h>
#include <ir/index_manager/index/IndexBarrelWriter.h>
#include <ir/index_manager/index/IndexerPropertyConfig.h>
#include <ir/index_manager/index/IndexMergeManager.h>

#include <util/scheduler.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/local_time_adjustor.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>

using namespace std;

NS_IZENELIB_IR_BEGIN

namespace indexmanager{

#define NUM_CACHED_DOC 1

IndexWriter::IndexWriter(Indexer* pIndex)
        :pIndexBarrelWriter_(NULL)
        ,ppCachedDocs_(NULL)
        ,nNumCachedDocs_(NUM_CACHED_DOC)
        ,nNumCacheUsed_(0)
        ,pCurBarrelInfo_(NULL)
        ,pIndexMerger_(NULL)
        ,pMemCache_(NULL)
        ,pIndexer_(pIndex)
        ,pCurDocCount_(NULL)
        ,pIndexMergeManager_(NULL)
{
    pBarrelsInfo_ = pIndexer_->getBarrelsInfo();
    setupCache();
    pIndexMergeManager_ = new IndexMergeManager(pIndex);
    pIndexMergeManager_->run();
}

IndexWriter::~IndexWriter()
{
    destroyCache();
    if (pMemCache_)
    {
        delete pMemCache_;
        pMemCache_ = NULL;
    }
    if (pIndexBarrelWriter_)
        delete pIndexBarrelWriter_;
    if (pIndexMerger_)
        delete pIndexMerger_;
    if (pIndexMergeManager_)
        delete pIndexMergeManager_;
}

void IndexWriter::setupCache()
{
    if (ppCachedDocs_)
        destroyCache();
    ppCachedDocs_ = new IndexerDocument*[nNumCachedDocs_];
    for (int i = 0;i<nNumCachedDocs_;i++)
        ppCachedDocs_[i] = NULL;

}

void IndexWriter::clearCache()
{
    if (ppCachedDocs_)
    {
        for (int i = 0;i<nNumCachedDocs_;i++)
        {
            if (ppCachedDocs_[i] != NULL)
            {
                delete ppCachedDocs_[i];
                ppCachedDocs_[i] = NULL;
            }
        }
    }
    nNumCacheUsed_ = 0;
}

void IndexWriter::destroyCache()
{
    clearCache();
    delete[] ppCachedDocs_;
    ppCachedDocs_ = NULL;
    nNumCacheUsed_ = 0;
}

void IndexWriter::close()
{
    if (!pIndexBarrelWriter_)
        return;
    flush();
}

void IndexWriter::flush()
{
    if((!pIndexBarrelWriter_)&&(nNumCacheUsed_ <=0 ))
        return;
    BarrelInfo* pLastBarrel = pBarrelsInfo_->getLastBarrel();
    if (pLastBarrel == NULL)
        return;
    flushDocuments(pLastBarrel->hasUpdateDocs);
    pLastBarrel->setBaseDocID(baseDocIDMap_);
    baseDocIDMap_.clear();
    if (pIndexBarrelWriter_->cacheEmpty() == false)///memory index has not been written to database yet.
    {
        pIndexBarrelWriter_->close();
        if (pIndexMerger_)
            pIndexMerger_->flushBarrelToDisk(pIndexBarrelWriter_->barrelName_);
    }
    pLastBarrel->setWriter(NULL);
    pBarrelsInfo_->write(pIndexer_->getDirectory());
    delete pIndexBarrelWriter_;
    pIndexBarrelWriter_ = NULL;
}

void IndexWriter::createMerger()
{
    if(!strcasecmp(pIndexer_->pConfigurationManager_->mergeStrategy_.param_.c_str(),"no"))
        pIndexMerger_ = NULL;
    else if(!strcasecmp(pIndexer_->pConfigurationManager_->mergeStrategy_.param_.c_str(),"imm"))
        pIndexMerger_ = new ImmediateMerger(pIndexer_);
    else if(!strcasecmp(pIndexer_->pConfigurationManager_->mergeStrategy_.param_.c_str(),"mway"))
        pIndexMerger_ = new MultiWayMerger(pIndexer_);	
    else
        pIndexMerger_ = new GPartitionMerger(pIndexer_);
}

void IndexWriter::createBarrelWriter(bool update)
{
    pBarrelsInfo_->addBarrel(pBarrelsInfo_->newBarrel().c_str(),0);
    pCurBarrelInfo_ = pBarrelsInfo_->getLastBarrel();
    pCurDocCount_ = &(pCurBarrelInfo_->nNumDocs);
    *pCurDocCount_ = 0;

    if (!pMemCache_)
        pMemCache_ = new MemCache((size_t)pIndexer_->getIndexManagerConfig()->indexStrategy_.memory_);
    pIndexBarrelWriter_ = new IndexBarrelWriter(pIndexer_,pMemCache_,pCurBarrelInfo_->getName().c_str());
    pCurBarrelInfo_->setWriter(pIndexBarrelWriter_);
    pIndexBarrelWriter_->setCollectionsMeta(pIndexer_->getCollectionsMeta());
    if(update)
    {
        bool* pHasUpdateDocs = &(pCurBarrelInfo_->hasUpdateDocs);
        *pHasUpdateDocs = true;
    }
}

void IndexWriter::optimizeIndex()
{
    if(pIndexer_->getIndexerType()&MANAGER_INDEXING_STANDALONE_MERGER)
    {
        ///optimize asynchronously
        pIndexMergeManager_->optimizeIndex();
    }
    else
    {
        ///optimize synchronously
        IndexMerger* pIndexMerger = new OfflineIndexMerger(pIndexer_, pBarrelsInfo_->getBarrelCount());
        mergeIndex(pIndexMerger);
        delete pIndexMerger;
    }
}

void IndexWriter::lazyOptimizeIndex()
{
    using namespace boost::posix_time;
    using namespace boost::gregorian;
    using namespace izenelib::util;

    boost::posix_time::ptime now = second_clock::local_time();
    boost::gregorian::date date = now.date();
    boost::posix_time::time_duration du = now.time_of_day();
    int dow = date.day_of_week();
    int month = date.month();
    int day = date.day();
    int hour = du.hours();
    int minute = du.minutes();
    if(scheduleExpression_.matches(minute, hour, day, month, dow))
    {
        pIndexMergeManager_->optimizeIndex();
    }
}

void IndexWriter::scheduleOptimizeTask(std::string expression, string uuid)
{
    scheduleExpression_.setExpression(expression);

    const string optimizeJob = "optimizeindex"+uuid;
    ///we need an uuid here because scheduler is an singleton in the system
    Scheduler::removeJob(optimizeJob);
    
    boost::function<void (void)> task = boost::bind(&IndexWriter::lazyOptimizeIndex,this);
    Scheduler::addJob(optimizeJob, 60*1000, 0, task);
}

void IndexWriter::mergeIndex(IndexMerger* pMerger)
{
    pMerger->setDirectory(pIndexer_->getDirectory());

    if(pIndexer_->getIndexReader()->getDocFilter())
        pMerger->setDocFilter(pIndexer_->getIndexReader()->getDocFilter());
    ///there is a in-memory index
    if ((pIndexBarrelWriter_) && pCurDocCount_ && ((*pCurDocCount_) > 0))
    {
        IndexMerger* pTmp = pIndexMerger_;
        pIndexMerger_ = pMerger;
        mergeAndWriteCachedIndex();
        pIndexMerger_ = pTmp;
    }
    else
    {
        if (pCurBarrelInfo_)
        {
            //pBarrelsInfo_->deleteLastBarrel();
            pCurBarrelInfo_ = NULL;
            pCurDocCount_ = NULL;
        }
        pMerger->merge(pBarrelsInfo_);
    }
    pIndexer_->getIndexReader()->delDocFilter();
    delete pIndexMerger_;
    pIndexMerger_ = NULL;
}


void IndexWriter::mergeUpdatedBarrel(docid_t currDocId)
{
    if(!pIndexMerger_) return;
    if(pIndexer_->getIndexReader()->getDocFilter())
        pIndexMerger_->setDocFilter(pIndexer_->getIndexReader()->getDocFilter());
    ///there is a in-memory index
    
    if ((pIndexBarrelWriter_) && pCurDocCount_ && ((*pCurDocCount_) > 0))
    {
        mergeAndWriteCachedIndex(true);

        bool* pHasUpdateDocs =  &(pCurBarrelInfo_->hasUpdateDocs);
        *pHasUpdateDocs = true;
    }
    else
        return;

    docid_t lastSetDoc = pIndexer_->getIndexReader()->getDocFilter()->getMaxSet();

    if((currDocId > pBarrelsInfo_->maxDocId())||
            (lastSetDoc > pBarrelsInfo_->maxDocId()))
    {
        bool* pHasUpdateDocs = &(pCurBarrelInfo_->hasUpdateDocs);
        *pHasUpdateDocs = false;
    }
    pIndexer_->getIndexReader()->delDocFilter();	
    pIndexMerger_->setDocFilter(NULL);
}

void IndexWriter::mergeAndWriteCachedIndex(bool mergeUpdateOnly)
{
    BarrelInfo* pLastBarrel = pBarrelsInfo_->getLastBarrel();
    pLastBarrel->setBaseDocID(baseDocIDMap_);

    if (pIndexBarrelWriter_->cacheEmpty() == false)///memory index has not been written to database yet.
    {
        pIndexBarrelWriter_->close();
        pLastBarrel->setWriter(NULL);
        pBarrelsInfo_->write(pIndexer_->getDirectory());
    }
    pIndexMerger_->merge(pBarrelsInfo_, mergeUpdateOnly);

    pBarrelsInfo_->addBarrel(pBarrelsInfo_->newBarrel().c_str(),0);
    pCurBarrelInfo_ = pBarrelsInfo_->getLastBarrel();
    pCurBarrelInfo_->setWriter(pIndexBarrelWriter_);
    pCurDocCount_ = &(pCurBarrelInfo_->nNumDocs);
    *pCurDocCount_ = 0;
}

void IndexWriter::addToMergeAndWriteCachedIndex()
{
    BarrelInfo* pLastBarrel = pBarrelsInfo_->getLastBarrel();
    pLastBarrel->setBaseDocID(baseDocIDMap_);

    if (pIndexBarrelWriter_->cacheEmpty() == false)///memory index has not been written to database yet.
    {
        pIndexBarrelWriter_->close();
        pLastBarrel->setWriter(NULL);
        pBarrelsInfo_->write(pIndexer_->getDirectory());
    }

    if (pIndexMerger_)
        pIndexMerger_->addToMerge(pBarrelsInfo_,pBarrelsInfo_->getLastBarrel());
	
    if (pIndexMerger_)
        pIndexMerger_->flushBarrelToDisk(pIndexBarrelWriter_->barrelName_);

    pBarrelsInfo_->addBarrel(pBarrelsInfo_->newBarrel().c_str(),0);
    pCurBarrelInfo_ = pBarrelsInfo_->getLastBarrel();
    pCurBarrelInfo_->setWriter(pIndexBarrelWriter_);
    pCurDocCount_ = &(pCurBarrelInfo_->nNumDocs);
    *pCurDocCount_ = 0;
}

void IndexWriter::writeCachedIndex()
{
    pBarrelsInfo_->wait_for_barrels_ready();
    pCurBarrelInfo_->setBaseDocID(baseDocIDMap_);
    if (pIndexBarrelWriter_->cacheEmpty() == false)
    {
        pIndexBarrelWriter_->close();
        pCurBarrelInfo_->setWriter(NULL);
        pBarrelsInfo_->write(pIndexer_->getDirectory());
    }
    pIndexMergeManager_->triggerMerge(pCurBarrelInfo_);
    pBarrelsInfo_->setLock(true);
    if(pIndexer_->getIndexerType()&MANAGER_INDEXING_STANDALONE_MERGER)
    {
        pBarrelsInfo_->addBarrel(pBarrelsInfo_->newBarrel().c_str(),0);
    }
    pCurBarrelInfo_ = pBarrelsInfo_->getLastBarrel();
    pCurBarrelInfo_->setSearchable(true);
    pCurBarrelInfo_->setWriter(pIndexBarrelWriter_);
    pBarrelsInfo_->setLock(false);

    pCurDocCount_ = &(pCurBarrelInfo_->nNumDocs);
    *pCurDocCount_ = 0;
}

void IndexWriter::addDocument(IndexerDocument* pDoc, bool update)
{
    if(!pIndexBarrelWriter_)
        createBarrelWriter(update);
    if(!pIndexMerger_)
        createMerger();

    ppCachedDocs_[nNumCacheUsed_++] = pDoc;

    if (isCacheFull())
        flushDocuments(update);
}

void IndexWriter::indexDocument(IndexerDocument* pDoc, bool update)
{
    DocId uniqueID;
    pDoc->getDocId(uniqueID);
    if(update)
        pIndexer_->getIndexReader()->delDocument(uniqueID.colId,uniqueID.docId);

    if (pIndexBarrelWriter_->cacheFull())
    {
         if(pCurBarrelInfo_->hasUpdateDocs)
         {
             mergeUpdatedBarrel(uniqueID.docId);
         }
         else
         {
            if((pIndexer_->getIndexerType() & MANAGER_TYPE_CLIENTPROCESS)||
                (pIndexer_->getIndexerType()&MANAGER_INDEXING_STANDALONE_MERGER))
                writeCachedIndex();
            else
                ///merge index
                addToMergeAndWriteCachedIndex();
         }
        baseDocIDMap_.clear();
        baseDocIDMap_[uniqueID.colId] = uniqueID.docId;
        pIndexBarrelWriter_->open(pCurBarrelInfo_->getName().c_str());

    }
    pCurBarrelInfo_->updateMaxDoc(uniqueID.docId);
    pBarrelsInfo_->updateMaxDoc(uniqueID.docId);
    pIndexBarrelWriter_->addDocument(pDoc);
    (*pCurDocCount_)++;
}

void IndexWriter::flushDocuments(bool update)
{
    if (nNumCacheUsed_ <=0 )
        return;
    for (int i=0;i<nNumCacheUsed_;i++)
    {
        DocId uniqueID;
        ppCachedDocs_[i]->getDocId(uniqueID);
        if(ppCachedDocs_[i]->empty())
            continue;
        if (baseDocIDMap_.find(uniqueID.colId) == baseDocIDMap_.end())
            baseDocIDMap_.insert(make_pair(uniqueID.colId,uniqueID.docId));

        indexDocument(ppCachedDocs_[i],update);
    }
    clearCache();
}

}

NS_IZENELIB_IR_END
