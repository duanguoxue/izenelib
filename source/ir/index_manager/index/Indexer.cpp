#include <boost/timer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>
#include <boost/assert.hpp>

#include <ir/index_manager/index/Indexer.h>
#include <ir/index_manager/index/IndexReader.h>
#include <ir/index_manager/index/TermReader.h>
#include <ir/index_manager/index/ParallelTermPosition.h>
#include <ir/index_manager/index/IndexMergeManager.h>
#include <ir/index_manager/store/FSDirectory.h>
#include <ir/index_manager/store/RAMDirectory.h>
#include <ir/index_manager/utility/StringUtils.h>

#include <util/hashFunction.h>

#include <algorithm>


using namespace izenelib::ir::indexmanager;

static int degree = 16;
static size_t cacheSize = 5000;
static size_t maxDataSize = 1000;

Indexer::Indexer()
        :pDirectory_(NULL)
        ,dirty_(false)
        ,pBarrelsInfo_(NULL)
        ,pIndexWriter_(NULL)
        ,pIndexReader_(NULL)
        ,pConfigurationManager_(NULL)
        ,pBTreeIndexer_(NULL)
        ,realTime_(false)
{
}

Indexer::~Indexer()
{
    close();

    if (pConfigurationManager_)
        delete pConfigurationManager_;
    pConfigurationManager_ = NULL;
}

const std::map<std::string, IndexerCollectionMeta>& 
Indexer::getCollectionsMeta()
{
    return pConfigurationManager_->getCollectionMetaNameMap();
}

void Indexer::setIndexManagerConfig(
    const IndexManagerConfig& config,
    const std::map<std::string, uint32_t>& collectionIdMapping
)
{
    close();

    if (pConfigurationManager_)
        delete pConfigurationManager_;
    pConfigurationManager_ = NULL;

    pConfigurationManager_ = new IndexManagerConfig();

    *pConfigurationManager_ = config;

    const std::map<std::string, IndexerCollectionMeta>& collectionsMeta =
        pConfigurationManager_->getCollectionMetaNameMap();
    std::map<std::string, uint32_t>::const_iterator idIter;
    std::map<std::string, IndexerCollectionMeta>::const_iterator iter;
    map<std::string, IndexerCollectionMeta> collectionList;
    for (iter = collectionsMeta.begin(); iter != collectionsMeta.end(); iter++)
    {
        idIter = collectionIdMapping.find(iter->first);
        if (idIter == collectionIdMapping.end())
        {
            SF1V5_THROW(ERROR_FILEIO,"Invalid input paramter" );
        }

        IndexerCollectionMeta collection(iter->second);
        collection.setColId(idIter->second);
        collectionList.insert( pair<std::string, IndexerCollectionMeta>(iter->first, collection) );
    }
    pConfigurationManager_->setCollectionMetaNameMap( collectionList );

    collectionid_t colID;

    for (std::map<std::string, IndexerCollectionMeta>::const_iterator iter = collectionList.begin(); iter != collectionList.end(); ++iter)
    {
        colID = iter->second.getColId();
 
        std::map<std::string, fieldid_t> propertyMap;
 
        std::set<IndexerPropertyConfig, IndexerPropertyConfigComp> documentSchema = (iter->second).getDocumentSchema();
 
        for (std::set<IndexerPropertyConfig, IndexerPropertyConfigComp>::const_iterator it = 
                documentSchema.begin(); it != documentSchema.end(); it++ )
             if (it->getPropertyId() != BAD_PROPERTY_ID)
                 propertyMap.insert(make_pair(it->getName(), it->getPropertyId()));
         property_name_id_map_.insert(make_pair(colID, propertyMap));
    }
 
    VariantDataPool::UPTIGHT_ALLOC_MEMSIZE = 10*1024*1024;

    skipInterval_ = pConfigurationManager_->indexStrategy_.skipInterval_;
    maxSkipLevel_ = pConfigurationManager_->indexStrategy_.maxSkipLevel_;

    std::string storagePolicy = pConfigurationManager_->storeStrategy_.param_;
    openDirectory(storagePolicy);

    if(pConfigurationManager_->indexStrategy_.isIndexBTree_)
      if ((!strcasecmp(storagePolicy.c_str(),"file"))||(!strcasecmp(storagePolicy.c_str(),"mmap")))
      {
          pBTreeIndexer_ = new BTreeIndexer(pDirectory_, pConfigurationManager_->indexStrategy_.indexLocation_, degree, cacheSize, maxDataSize);
          if (pDirectory_->fileExists(BTREE_DELETED_DOCS))
          {
                boost::shared_ptr<BitVector> pBTreeFilter(new BitVector);
                pBTreeFilter->read(pDirectory_, BTREE_DELETED_DOCS);
                pBTreeIndexer_->setFilter(pBTreeFilter);
           }
      }

    if(!strcasecmp(pConfigurationManager_->indexStrategy_.indexMode_.c_str(),"realtime"))
    {
        realTime_ = true;
        indexingType_ = BYTEALIGN;
    }
    else
    {
        realTime_ = false;
        std::vector<std::string> indexingParams = izenelib::ir::indexmanager::split(pConfigurationManager_->indexStrategy_.indexMode_,":");
        ///  default:block  or default:chunk
        if(indexingParams.size() == 2)
      	{
            if(!strcasecmp(indexingParams[1].c_str(),"block"))
                indexingType_ = BLOCK;
            else if(!strcasecmp(indexingParams[1].c_str(),"chunk"))
            {
                indexingType_ = CHUNK;
                skipInterval_ = CHUNK_SIZE;
            }
            else
                indexingType_ = BYTEALIGN;
       	}
        else
           indexingType_ = BYTEALIGN;
    }

    pIndexWriter_ = new IndexWriter(this);
    pIndexReader_ = new IndexReader(this);

    if(! pConfigurationManager_->indexStrategy_.optimizeSchedule_.empty())
    {
        using namespace izenelib::util;
        int32_t uuid =  (int32_t)HashFunction<std::string>::generateHash32(pConfigurationManager_->indexStrategy_.indexLocation_);
        char uuidstr[10];
        memset(uuidstr,0,10);
        sprintf(uuidstr,"%d",uuid);
        pIndexWriter_->scheduleOptimizeTask(pConfigurationManager_->indexStrategy_.optimizeSchedule_, uuidstr);
    }
}

void Indexer::openDirectory(const std::string& storagePolicy)
{
    close();
    string path = pConfigurationManager_->indexStrategy_.indexLocation_;
    if (!strcasecmp(storagePolicy.c_str(),"file"))
        pDirectory_ = new FSDirectory(path,true);
    else if (!strcasecmp(storagePolicy.c_str(),"mmap"))
    {
        pDirectory_ = new FSDirectory(path,true);
        static_cast<FSDirectory*>(pDirectory_)->setMMapFlag(true);
    }
    else
        pDirectory_ = new RAMDirectory();

    pBarrelsInfo_ = new BarrelsInfo();

    pBarrelsInfo_->read(pDirectory_);
    if (strcasecmp(pBarrelsInfo_->getVersion(),SF1_VERSION))
    {
        delete pBarrelsInfo_;
        pBarrelsInfo_ = NULL;
        SF1V5_THROW(ERROR_VERSION,"incompatible version.");
    }
}

void Indexer::setBasePath(std::string basePath)
{
    if (pDirectory_)
        delete pDirectory_;
    pDirectory_ = new FSDirectory(basePath,true);

    pIndexReader_ = new IndexReader(this);
    pIndexWriter_ = new IndexWriter(this);
}

std::string Indexer::getBasePath()
{
    string path = pConfigurationManager_->indexStrategy_.indexLocation_;
    if (path[path.length()-1] != '/' )
    {
        path += "/";
    }
    return path;
}

void Indexer::close()
{
    // in case of merge thread is using IndexReader,
    // IndexWriter::close(), which waits for the end of merge thread,
    // is called before deleting IndexReader
    if (pIndexWriter_)
    {
        pIndexWriter_->flush();
        delete pIndexWriter_;
        pIndexWriter_ = NULL;
    }
    if (pIndexReader_)
    {
        delete pIndexReader_;
        pIndexReader_ = NULL;
    }
    if (pBarrelsInfo_)
    {
        delete pBarrelsInfo_;
        pBarrelsInfo_ = NULL;
    }

    if (pBTreeIndexer_)
    {
        pBTreeIndexer_->flush();
        delete pBTreeIndexer_;
        pBTreeIndexer_ = NULL;
    }

    if (pDirectory_)
    {
        delete pDirectory_;
        pDirectory_ = NULL;
    }
    dirty_ = false;
}

IndexReader* Indexer::getIndexReader()
{
    if (dirty_)
    {
        izenelib::util::ScopedWriteLock<izenelib::util::ReadWriteLock> lock(mutex_);
        // double check in case of another thread has reset the flag
        if (dirty_)
        {
            pIndexReader_->reopen();
            dirty_ = false;
        }
    }

    return pIndexReader_;
}

void Indexer::setDirty()
{
    izenelib::util::ScopedWriteLock<izenelib::util::ReadWriteLock> lock(mutex_);
    dirty_ = true;
}

int Indexer::insertDocument(IndexerDocument& doc)
{
    boost::mutex::scoped_lock lock(indexMutex_);
    pIndexWriter_->indexDocument(doc);
    return 1;
}

int Indexer::updateDocument(IndexerDocument& doc)
{
    boost::mutex::scoped_lock lock(indexMutex_);
    pIndexWriter_->updateDocument(doc);
    return 1;
}

int Indexer::updateRtypeDocument(IndexerDocument& oldDoc, IndexerDocument& doc)
{
    boost::mutex::scoped_lock lock(indexMutex_);
    pIndexWriter_->updateRtypeDocument(oldDoc, doc);
    return 1;
}

int Indexer::removeDocument(collectionid_t colID, docid_t docId)
{
    boost::mutex::scoped_lock lock(indexMutex_);
    pIndexWriter_->removeDocument(colID, docId);
    return 1;
}

void Indexer::flush()
{
    pIndexWriter_->flush();
    if(pBTreeIndexer_) pBTreeIndexer_->flush();
    pIndexReader_->flush();

    setDirty();
}

void Indexer::optimizeIndex()
{
    pIndexWriter_->optimizeIndex();
}

IndexStatus Indexer::checkIntegrity()
{
    if (0 == pBarrelsInfo_->getBarrelCount() ||
            0 == pBarrelsInfo_->maxDocId()
       )
        return EMPTY;

    TermReader* pTermReader = NULL;

    try
    {
        const std::map<std::string, IndexerCollectionMeta> & collectionMeta =
            pConfigurationManager_->getCollectionMetaNameMap();

        std::map<std::string, IndexerCollectionMeta>::const_iterator iter = collectionMeta.begin();
        if (iter == collectionMeta.end())
            return CORRUPT;


        collectionid_t colID = iter->second.getColId();

        const std::set<IndexerPropertyConfig, IndexerPropertyConfigComp> & schema =
            iter->second.getDocumentSchema();

        std::string property;
        std::set<IndexerPropertyConfig, IndexerPropertyConfigComp>::const_iterator it;
        for ( it = schema.begin(); it != schema.end(); it++ )
        {
            if (it->isIndex() && it->isAnalyzed())
            {
                property = it->getName();
                continue;
            }
        }

        pTermReader = pIndexReader_->getTermReader(colID);
        ///seek the posting for the last term
        Term term(property.c_str(), MAX_TERMID);
        if (pTermReader->seek(&term))
        {
            TermPositions* pTermPositions = pTermReader->termPositions();
            while (pTermPositions->next())
            {
            }
            delete pTermPositions;
        }
        else
        {
            delete pTermReader;
            return CORRUPT;
        }

        delete pTermReader;
    }
    catch (std::exception& e)
    {
        if (pTermReader) delete pTermReader;
        return CORRUPT;
    }

    return CONSISTENT;
}

size_t Indexer::getDistinctNumTermsByProperty(collectionid_t colID, const std::string& property)
{
    return pIndexReader_->getDistinctNumTerms(colID, property);
}

///To be optimized: Using TermDocFreqs instead of TermPositions
bool Indexer::getDocsByTermInProperties(termid_t termID, collectionid_t colID, vector<string> properties, deque<docid_t>& docIds)
{
    if (properties.size() > 1)
    {
        ParallelTermPosition parallelTermPosition(colID, pIndexReader_,properties);
        if (!parallelTermPosition.isValid())
            return false;
        if (parallelTermPosition.seek(termID))
        {
            while (parallelTermPosition.next())
            {
                docIds.push_back(parallelTermPosition.doc());
            }
        }
        else
        {
            return false;
        }
    }
    else
    {
        TermReader* pTermReader = pIndexReader_->getTermReader(colID);
        if (NULL == pTermReader)
            return false;


        if (isDirty())
        {
            delete pTermReader;
            return false;
        }

        Term term(properties[0].c_str(), termID);
        if (pTermReader->seek(&term))
        {

            if (isDirty())
            {
                delete pTermReader;
                return false;
            }

            TermDocFreqs* pTermDocFreqs = pTermReader->termDocFreqs();

            while (pTermDocFreqs->next())
            {
                docIds.push_back(pTermDocFreqs->doc());
            }

            delete pTermDocFreqs;
        }
        else
        {
            delete pTermReader;
            return false;
        }
        delete pTermReader;
    }

    return true;
}

bool Indexer::getDocsByTermInProperties(termid_t termID, collectionid_t colID, vector<string> properties, deque<CommonItem>& commonSet)
{
    if (properties.size() > 1)
    {
        ParallelTermPosition parallelTermPosition(colID, pIndexReader_,properties);
        if (!parallelTermPosition.isValid())
            return false;
        if (parallelTermPosition.seek(termID))
        {
            while (parallelTermPosition.next())
            {
                CommonItem item;
                item.setDocID(parallelTermPosition.doc());
                item.setCollectionID(colID);
                std::map<string, PropertyItem> result;
                parallelTermPosition.getPositions(result);
                item.add(result);
                commonSet.push_back(item);
            }

        }
        else
        {
            return false;
        }
    }
    else
    {
        TermReader* pTermReader = pIndexReader_->getTermReader(colID);
        if (NULL == pTermReader)
            return false;

        Term term(properties[0].c_str(), termID);
        if (pTermReader->seek(&term))
        {
            TermPositions* pPositions = pTermReader->termPositions();

            while (pPositions->next())
            {
                CommonItem item;
                item.setDocID(pPositions->doc());
                boost::shared_ptr<std::deque<unsigned int> > positions(new std::deque<unsigned int>);
                loc_t pos = pPositions->nextPosition();
                while (pos != BAD_POSITION)
                {
                    positions->push_back(pos);
                    pos = pPositions->nextPosition();
                }
                item.addProperty(properties[0], positions, pPositions->freq());
                commonSet.push_back(item);
            }

            delete pPositions;
        }
        else
        {
            delete pTermReader;
            return false;
        }
        delete pTermReader;
    }

    return true;
}

bool Indexer::getTermFrequencyInCollectionByTermId( const vector<termid_t>& termIdList, const unsigned int collectionId, const vector<string>& propertyList, vector<unsigned int>& termFrequencyList )
{
    TermReader* pTermReader = pIndexReader_->getTermReader(collectionId);
    if (NULL == pTermReader)
        return false;

    size_t propertySize = propertyList.size();
    size_t termSize = termIdList.size();
    for (size_t i = 0; i < termSize; i++)
    {
        unsigned int ctf = 0;
        for (size_t j = 0; j < propertySize; j++)
        {
            Term term(propertyList[j].c_str(), termIdList[i]);
            if (pTermReader->seek(&term))
            {
                TermPositions* pPositions = pTermReader->termPositions();
                ctf+=pPositions->getCTF();
                delete pPositions;
            }
        }
        termFrequencyList.push_back(ctf);
    }
    delete pTermReader;

    return true;
}

bool Indexer::seek(collectionid_t colID, string property, PropertyType value)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    return pBTreeIndexer_->seek(colID, fid, value);
}

bool Indexer::getDocList(collectionid_t colID, std::string property, PropertyType value, BitVector& docs)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getNoneEmptyList(colID, fid, value, docs);
    return true;
}

bool Indexer::getDocsByPropertyValue(collectionid_t colID, string property, PropertyType value, BitVector&docs)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValue(colID, fid, value, docs);
    return true;
}

bool Indexer::getDocsByPropertyValue(collectionid_t colID, std::string property, PropertyType value, std::vector<docid_t>& docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValue(colID, fid, value, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueRange(collectionid_t colID, string property, PropertyType value1, PropertyType value2, BitVector&docs)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueBetween(colID, fid, value1, value2, docs);
    return true;
}

bool Indexer::getDocsByPropertyValueLessThan(collectionid_t colID, string property, PropertyType value, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueLess(colID, fid, value, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueLessThanOrEqual(collectionid_t colID, string property, PropertyType value, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueLessEqual(colID, fid, value, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueGreaterThan(collectionid_t colID, string property, PropertyType value, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueGreat(colID, fid, value, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueGreaterThanOrEqual(collectionid_t colID, string property, PropertyType value, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueGreatEqual(colID, fid, value, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueIn(collectionid_t colID, string property, vector<PropertyType> values, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueIn(colID, fid, values, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueNotIn(collectionid_t colID, string property, vector<PropertyType> values, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueNotIn(colID, fid, values, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueNotEqual(collectionid_t colID, string property, PropertyType value, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueNotEqual(colID, fid, value, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueStart(collectionid_t colID, string property, PropertyType value, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueStart(colID, fid, value, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueEnd(collectionid_t colID, string property, PropertyType value, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueEnd(colID, fid, value, docList);
    return true;
}

bool Indexer::getDocsByPropertyValueSubString(collectionid_t colID, string property, PropertyType value, BitVector&docList)
{
    BOOST_ASSERT(pConfigurationManager_->indexStrategy_.isIndexBTree_);
    fieldid_t fid = getPropertyIDByName(colID,property);
    pBTreeIndexer_->getValueSubString(colID, fid, value, docList);
    return true;
}

void Indexer::pauseMerge()
{
    IndexMergeManager* pMergeManager = pIndexWriter_->getMergeManager();
    pMergeManager->pauseMerge();
}

void Indexer::resumeMerge()
{
    IndexMergeManager* pMergeManager = pIndexWriter_->getMergeManager();
    pMergeManager->resumeMerge();
}

void Indexer::waitForMergeFinish()
{
    IndexMergeManager* pMergeManager = pIndexWriter_->getMergeManager();
    pMergeManager->waitForMergeFinish();
}

